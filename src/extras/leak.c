#include "cly.h"
#include <limits.h>

#define UNK INT_MIN
#define MAXA 16
#define MAXH 6
#define MAXOPS 4

typedef struct {
    char *lab;
    char mn[24];
    char *op[MAXOPS];
    int nop;
    int kind;
    int fn;
    int line;
} Ins;

enum { K_CODE, K_LABEL, K_SKIP };

typedef struct { int ins, kind; char fn[40]; long len; int base; } Site;
enum { SK_CALL, SK_MMAP, SK_UFN, SK_PARAM, SK_PREV };

typedef struct { short site; unsigned char nh; short h[MAXH]; } Al;

typedef struct {
    int valid;
    int delta, rbpd;
    int nal;
    Al al[MAXA];
    int nullcmp;
    long cv[16];
} St;

typedef struct { int site, k, kind; } Ev;
enum { EV_EXIT, EV_LOST, EV_PREV };

typedef struct { int line, where; char *text; int repl; } Fix;

static LineVec *g_lv;
static Ins *I;
static int nI;
static int g_bits;
static int W;
static Site *S;
static int nS, capS;
static char **loc_names;
static int nloc, caploc;
static int *fn_entry, nfn, capfn;
static char **fn_name;
static int *fn_end;
static int *fn_ret_alloc, *fn_frees_arg;
static Ev *EV;
static int nEV, capEV;
static int g_report;

static const char *rn64[16] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
static const char *rn32[16] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" };
static const int argreg[6] = { 7, 6, 2, 1, 8, 9 };

static const char *skipw(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int loc_id(const char *name)
{
    for (int i = 0; i < nloc; i++) if (!strcmp(loc_names[i], name)) return i;
    if (nloc == caploc) { caploc = caploc ? caploc * 2 : 64; loc_names = xrealloc(loc_names, sizeof(char *) * (size_t)caploc); }
    loc_names[nloc] = xstrdup(name);
    return nloc++;
}

static int reg_loc(int r)
{
    char b[16];
    snprintf(b, sizeof b, "R%d", r);
    return loc_id(b);
}

static int slot_loc(int off)
{
    char b[32];
    snprintf(b, sizeof b, "S%d", off);
    return loc_id(b);
}

static int glob_loc(const char *sym)
{
    char b[160];
    snprintf(b, sizeof b, "G%s", sym);
    return loc_id(b);
}

static int loc_is_reg(int l) { return loc_names[l][0] == 'R'; }
static int loc_is_slot(int l) { return loc_names[l][0] == 'S'; }
static int loc_reg(int l) { return atoi(loc_names[l] + 1); }
static int loc_slot(int l) { return atoi(loc_names[l] + 1); }

static int reg_index(const char *s, size_t n)
{
    char t[16];
    if (n == 0 || n > 8) return -1;
    memcpy(t, s, n);
    t[n] = 0;
    for (size_t i = 0; i < n; i++) t[i] = (char)tolower((unsigned char)t[i]);
    for (int i = 0; i < 16; i++) if (!strcmp(t, rn64[i]) || !strcmp(t, rn32[i])) return i;
    static const char *w16[8] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
    static const char *b8[8] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
    static const char *b8x[8] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil" };
    for (int i = 0; i < 8; i++) if (!strcmp(t, w16[i]) || !strcmp(t, b8[i]) || !strcmp(t, b8x[i])) return i;
    if (t[0] == 'r' && isdigit((unsigned char)t[1])) {
        int v = atoi(t + 1);
        if (v >= 8 && v <= 15) {
            const char *e = t + 1;
            while (isdigit((unsigned char)*e)) e++;
            if (!*e || ((*e == 'd' || *e == 'w' || *e == 'b' || *e == 'l') && !e[1])) return v;
        }
    }
    return -1;
}

static const char *rname(int r) { return g_bits == 64 ? rn64[r] : rn32[r]; }

typedef struct {
    int regs[4];
    int nregs;
    long disp;
    char sym[64];
    int ok;
} Mem;

static int parse_mem(const char *op, Mem *m)
{
    memset(m, 0, sizeof *m);
    const char *p = strchr(op, '[');
    if (!p) return 0;
    p++;
    const char *e = strrchr(p, ']');
    if (!e) return 0;
    m->ok = 1;
    int sign = 1;
    while (p < e) {
        p = skipw(p);
        if (p >= e) break;
        if (*p == '+') { sign = 1; p++; continue; }
        if (*p == '-') { sign = -1; p++; continue; }
        if (isdigit((unsigned char)*p)) {
            char *q;
            long v = strtol(p, &q, 0);
            if (*q == '*') { m->ok = 0; return 1; }
            if ((*q == 'h' || *q == 'H') && !ident_char((unsigned char)q[1])) { v = strtol(p, NULL, 16); q++; }
            m->disp += sign * v;
            p = q;
            sign = 1;
            continue;
        }
        if (ident_start((unsigned char)*p) || *p == '.' || *p == '$') {
            const char *s = p;
            while (ident_char((unsigned char)*p) || *p == '.' || *p == '$') p++;
            size_t l = (size_t)(p - s);
            const char *q = skipw(p);
            if (*q == '*') {
                m->ok = 0;
                m->regs[m->nregs < 4 ? m->nregs++ : 0] = 0;
                q = skipw(q + 1);
                while (isdigit((unsigned char)*q)) q++;
                p = q;
                continue;
            }
            if ((l == 3 && !strncasecmp(s, "rel", 3)) || (l == 3 && !strncasecmp(s, "abs", 3))) continue;
            int r = reg_index(s, l);
            if (r >= 0) {
                if (m->nregs < 4) m->regs[m->nregs++] = r;
                continue;
            }
            if (l == 3 && !strncasecmp(s, "wrt", 3)) { while (p < e) p++; break; }
            if (l < sizeof m->sym) { memcpy(m->sym, s, l); m->sym[l] = 0; }
            continue;
        }
        p++;
    }
    return 1;
}

enum { L_NONE, L_REG, L_STACK, L_GLOBAL, L_DEREF };

static int op_loc(const char *op, const St *st, int *cls)
{
    if (!op || !*op) { *cls = L_NONE; return -1; }
    const char *p = skipw(op);
    int r = reg_index(p, strlen(p));
    if (r >= 0 && !strchr(p, '[')) { *cls = L_REG; return reg_loc(r); }
    Mem m;
    if (!parse_mem(p, &m)) { *cls = L_NONE; return -1; }
    if (!m.ok) { *cls = L_DEREF; return -1; }
    if (m.nregs == 1 && !m.sym[0]) {
        int base = m.regs[0];
        if (base == 5 && st->rbpd != UNK) { *cls = L_STACK; return slot_loc(st->rbpd + (int)m.disp); }
        if (base == 4 && st->delta != UNK) { *cls = L_STACK; return slot_loc(st->delta + (int)m.disp); }
        if (base == 5 || base == 4) { *cls = L_DEREF; return -1; }
    }
    if (m.nregs == 0 && m.sym[0]) {
        char k[96];
        snprintf(k, sizeof k, "%s%+ld", m.sym, m.disp);
        *cls = L_GLOBAL;
        return glob_loc(k);
    }
    *cls = L_DEREF;
    return -1;
}

static long op_imm(const char *op, int *ok)
{
    const char *p = skipw(op);
    *ok = 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (!isdigit((unsigned char)*p)) return 0;
    char *e;
    long v = strtol(p, &e, 0);
    if ((*e == 'h' || *e == 'H') && !ident_char((unsigned char)e[1])) { v = strtol(p, &e, 16); e++; }
    if (ident_char((unsigned char)*e)) return 0;
    *ok = 1;
    return neg ? -v : v;
}

static void st_init(St *s)
{
    memset(s, 0, sizeof *s);
    s->delta = 0;
    s->rbpd = UNK;
    s->nullcmp = -1;
    for (int i = 0; i < 16; i++) s->cv[i] = LONG_MIN;
}

static Al *find_al(St *s, int site)
{
    for (int i = 0; i < s->nal; i++) if (s->al[i].site == site) return &s->al[i];
    return NULL;
}

static int al_has(const Al *a, int l)
{
    for (int i = 0; i < a->nh; i++) if (a->h[i] == l) return 1;
    return 0;
}

static void al_add(Al *a, int l)
{
    if (al_has(a, l) || a->nh >= MAXH) return;
    a->h[a->nh++] = (short)l;
}

static void al_del(Al *a, int l)
{
    for (int i = 0; i < a->nh; i++)
        if (a->h[i] == l) { a->h[i] = a->h[--a->nh]; return; }
}

static void st_drop(St *s, int idx)
{
    s->al[idx] = s->al[--s->nal];
}

static int site_new(int ins, int kind, const char *fn, long len)
{
    for (int i = 0; i < nS; i++) if (S[i].ins == ins && S[i].kind == kind) return i;
    if (nS == capS) { capS = capS ? capS * 2 : 32; S = xrealloc(S, sizeof(Site) * (size_t)capS); }
    S[nS].ins = ins;
    S[nS].kind = kind;
    snprintf(S[nS].fn, sizeof S[nS].fn, "%s", fn ? fn : "");
    S[nS].len = len;
    S[nS].base = -1;
    return nS++;
}

static void ev_add(int site, int k, int kind)
{
    if (!g_report) return;
    if (S[site].base >= 0) { site = S[site].base; kind = EV_PREV; }
    for (int i = 0; i < nEV; i++) if (EV[i].site == site && EV[i].k == k && EV[i].kind == kind) return;
    if (nEV == capEV) { capEV = capEV ? capEV * 2 : 32; EV = xrealloc(EV, sizeof(Ev) * (size_t)capEV); }
    EV[nEV].site = site;
    EV[nEV].k = k;
    EV[nEV].kind = kind;
    nEV++;
}

static int merge_into(St *d, const St *s)
{
    int ch = 0;
    if (!d->valid) { *d = *s; return 1; }
    if (d->delta != s->delta && d->delta != UNK) { d->delta = UNK; ch = 1; }
    if (d->rbpd != s->rbpd && d->rbpd != UNK) { d->rbpd = UNK; ch = 1; }
    for (int i = 0; i < s->nal; i++) {
        Al *a = find_al(d, s->al[i].site);
        if (!a) {
            if (d->nal < MAXA) { d->al[d->nal++] = s->al[i]; ch = 1; }
            continue;
        }
        for (int k = 0; k < s->al[i].nh; k++) {
            if (!al_has(a, s->al[i].h[k]) && a->nh < MAXH) { a->h[a->nh++] = s->al[i].h[k]; ch = 1; }
        }
    }
    if (d->nullcmp != s->nullcmp && d->nullcmp != -1) { d->nullcmp = -1; ch = 1; }
    for (int i = 0; i < 16; i++) if (d->cv[i] != s->cv[i] && d->cv[i] != LONG_MIN) { d->cv[i] = LONG_MIN; ch = 1; }
    return ch;
}

static int is_callee_saved(int r)
{
    if (g_bits == 64) return r == 3 || r == 5 || r == 4 || (r >= 12 && r <= 15);
    return r == 3 || r == 5 || r == 4 || r == 6 || r == 7;
}

static void orphan(St *s, int site_idx, int k, int lost)
{
    Al *a = &s->al[site_idx];
    if (S[a->site].kind != SK_PARAM && lost) ev_add(a->site, k, EV_LOST);
    st_drop(s, site_idx);
}

static void kill_loc(St *s, int l, int k)
{
    for (int i = 0; i < s->nal; i++) {
        Al *a = &s->al[i];
        if (al_has(a, l)) {
            al_del(a, l);
            if (a->nh == 0) { orphan(s, i, k, 1); i--; }
        }
    }
}

static void escape_loc(St *s, int l)
{
    for (int i = 0; i < s->nal; i++) {
        if (al_has(&s->al[i], l)) { st_drop(s, i); i--; }
    }
}

static void alias_loc(St *s, int from, int to)
{
    for (int i = 0; i < s->nal; i++) if (al_has(&s->al[i], from)) al_add(&s->al[i], to);
}

static int holds(const St *s, int l)
{
    for (int i = 0; i < s->nal; i++) if (al_has(&s->al[i], l)) return 1;
    return 0;
}

static void drop_stack_holders(St *s, int below, int k)
{
    for (int i = 0; i < s->nal; i++) {
        Al *a = &s->al[i];
        for (int h = 0; h < a->nh; h++) {
            int l = a->h[h];
            if (loc_is_slot(l) && loc_slot(l) < below) { a->h[h] = a->h[--a->nh]; h--; }
        }
        if (a->nh == 0) { orphan(s, i, k, 1); i--; }
    }
}

static void unknown_stack(St *s)
{
    for (int i = 0; i < s->nal; i++) {
        Al *a = &s->al[i];
        for (int h = 0; h < a->nh; h++) {
            if (loc_is_slot(a->h[h])) { a->h[h] = a->h[--a->nh]; h--; }
        }
        if (a->nh == 0) { st_drop(s, i); i--; }
    }
}

static void kill_regs_mask(St *s, int k, int caller_saved_only)
{
    for (int r = 0; r < 16; r++) {
        if (r == 4) continue;
        if (caller_saved_only && is_callee_saved(r)) continue;
        kill_loc(s, reg_loc(r), k);
        s->cv[r] = LONG_MIN;
    }
}

static const char *known_alloc(const char *n)
{
    static const char *const f[] = { "malloc", "calloc", "strdup", "strndup", "aligned_alloc", "memalign", "valloc", NULL };
    for (int i = 0; f[i]; i++) if (!strcmp(n, f[i])) return f[i];
    return NULL;
}

static int known_safe(const char *n)
{
    static const char *const f[] = { "printf", "puts", "putchar", "strlen", "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp", "strchr", "strrchr", "strstr", "memcpy", "memmove", "memset", "memcmp", "scanf", "sscanf", "fgets", "fputs", "fprintf", "sprintf", "snprintf", "write", "read", "atoi", "atol", "strtol", "gets", "toupper", "tolower", "fwrite", "fread", "fputc", "putc", "fflush", "isdigit", "isalpha", "abs", "labs", "sqrt", "time", "srand", "rand", "usleep", "sleep", "getchar", "fgetc", "getc", "strtod", "atof", "qsort", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "exp", "exp2", "log", "log10", "log2", "pow", "fmod", "hypot", "fmin", "fmax", "trunc", "round", "floor", "ceil", "fabs", NULL };
    for (int i = 0; f[i]; i++) if (!strcmp(n, f[i])) return 1;
    return 0;
}

static int find_fn_by_label(const char *name)
{
    for (int i = 0; i < nfn; i++) if (!strcmp(fn_name[i], name)) return i;
    return -1;
}

static void call_name(const char *op, char *out, size_t sz)
{
    out[0] = 0;
    const char *p = skipw(op);
    {
        static const char *const kw[] = { "near", "far", "qword", "dword", NULL };
        for (int i = 0; kw[i]; i++) {
            size_t l = strlen(kw[i]);
            if (!strncasecmp(p, kw[i], l) && (p[l] == ' ' || p[l] == '\t')) { p = skipw(p + l); break; }
        }
    }
    if (*p == '[') {
        Mem m;
        parse_mem(p, &m);
        if (m.sym[0] && m.nregs == 0) snprintf(out, sz, "%s", m.sym);
        return;
    }
    if (!ident_start((unsigned char)*p) && *p != '.') return;
    size_t n = 0;
    while ((ident_char((unsigned char)*p) || *p == '.' || *p == '$') && n + 1 < sz) out[n++] = *p++;
    out[n] = 0;
    char *at = strchr(out, '@');
    if (at) *at = 0;
}

static int arg_loc_of(int i, const St *s)
{
    if (g_bits == 64) return i < 6 ? reg_loc(argreg[i]) : -1;
    if (s->delta == UNK) return -1;
    return slot_loc(s->delta + 4 * i);
}

static void handle_arg_escape(St *s, int nargs)
{
    for (int i = 0; i < nargs; i++) {
        int l = arg_loc_of(i, s);
        if (l >= 0) escape_loc(s, l);
    }
}

static void new_alloc(St *s, int site, int where_reg)
{
    Al *old = find_al(s, site);
    if (old) {
        int sh = site_new(S[site].ins, SK_PREV, S[site].fn, S[site].len);
        S[sh].base = site;
        Al *ex = find_al(s, sh);
        if (ex) {
            for (int i = 0; i < old->nh; i++) if (!al_has(ex, old->h[i]) && ex->nh < MAXH) ex->h[ex->nh++] = old->h[i];
            st_drop(s, (int)(old - s->al));
        } else old->site = (short)sh;
    }
    if (s->nal >= MAXA) return;
    Al *a = &s->al[s->nal++];
    memset(a, 0, sizeof *a);
    a->site = (short)site;
    al_add(a, reg_loc(where_reg));
}

static int const_reg(const St *s, int r, long *v)
{
    if (s->cv[r] == LONG_MIN) return 0;
    *v = s->cv[r];
    return 1;
}

static void free_arg(St *s, int i, int k, int fn_frees_mark)
{
    int l = arg_loc_of(i, s);
    if (l < 0) return;
    for (int a = 0; a < s->nal; a++) {
        if (al_has(&s->al[a], l)) {
            if (S[s->al[a].site].kind == SK_PARAM && fn_frees_mark >= 0) fn_frees_arg[fn_frees_mark] = 1;
            st_drop(s, a);
            a--;
        }
    }
    (void)k;
}

static int cur_fn;

static int has_global_holder(const Al *a)
{
    for (int i = 0; i < a->nh; i++) if (loc_names[a->h[i]][0] == 'G') return 1;
    return 0;
}

static void do_exit_event(St *s, int k)
{
    for (int i = 0; i < s->nal; i++) {
        if (S[s->al[i].site].kind == SK_PARAM) continue;
        if (has_global_holder(&s->al[i])) continue;
        ev_add(s->al[i].site, k, EV_EXIT);
    }
}

static int is_jcc(const char *m)
{
    if (m[0] != 'j' || !strcmp(m, "jmp")) return 0;
    return 1;
}

static void transfer(St *s, int k)
{
    const Ins *in = &I[k];
    const char *m = in->mn;
    int cls1 = L_NONE, cls2 = L_NONE;
    int d = -1, sr = -1;
    s->nullcmp = s->nullcmp;
    int wasnull = s->nullcmp;
    s->nullcmp = -1;
    if (in->nop >= 1) d = op_loc(in->op[0], s, &cls1);
    if (in->nop >= 2) sr = op_loc(in->op[1], s, &cls2);

    if (!strcmp(m, "mov") || !strcmp(m, "movq") || !strcmp(m, "movabs")) {
        if (in->nop < 2) return;
        int ok;
        long imm = op_imm(in->op[1], &ok);
        if (cls1 == L_REG || cls1 == L_STACK || cls1 == L_GLOBAL) {
            int aliasfrom = (sr >= 0 && holds(s, sr) && (cls2 == L_REG || cls2 == L_STACK || cls2 == L_GLOBAL)) ? sr : -1;
            if (aliasfrom >= 0 && d == aliasfrom) return;
            int tmp_sites[MAXA], nts = 0;
            if (aliasfrom >= 0) {
                for (int i = 0; i < s->nal; i++) if (al_has(&s->al[i], aliasfrom)) tmp_sites[nts++] = s->al[i].site;
            }
            kill_loc(s, d, k);
            for (int i = 0; i < nts; i++) {
                Al *a = find_al(s, tmp_sites[i]);
                if (a) al_add(a, d);
            }
            if (cls1 == L_REG) {
                int r = loc_reg(d);
                s->cv[r] = ok ? imm : LONG_MIN;
                if (cls2 == L_REG && s->cv[loc_reg(sr)] != LONG_MIN) s->cv[r] = s->cv[loc_reg(sr)];
                if (cls1 == L_REG && loc_reg(d) == 4 && cls2 == L_REG && loc_reg(sr) == 5) s->delta = s->rbpd;
                if (loc_reg(d) == 5 && cls2 == L_REG && loc_reg(sr) == 4) s->rbpd = s->delta;
                else if (loc_reg(d) == 5) s->rbpd = UNK;
                if (loc_reg(d) == 4 && !(cls2 == L_REG && loc_reg(sr) == 5)) { unknown_stack(s); s->delta = UNK; }
            }
            return;
        }
        if (cls1 == L_DEREF) {
            if (sr >= 0 && cls2 == L_REG) escape_loc(s, sr);
            else if (sr >= 0 && holds(s, sr)) escape_loc(s, sr);
            return;
        }
        return;
    }
    if (!strcmp(m, "lea")) {
        if (in->nop < 2 || cls1 != L_REG) return;
        Mem mm;
        int fromreg = -1;
        if (parse_mem(in->op[1], &mm) && mm.ok && mm.nregs >= 1 && !mm.sym[0]) {
            for (int i = 0; i < mm.nregs; i++) if (holds(s, reg_loc(mm.regs[i]))) { fromreg = reg_loc(mm.regs[i]); break; }
        }
        int tmp_sites[MAXA], nts = 0;
        if (fromreg >= 0) for (int i = 0; i < s->nal; i++) if (al_has(&s->al[i], fromreg)) tmp_sites[nts++] = s->al[i].site;
        int self = (fromreg == d);
        if (!self) kill_loc(s, d, k);
        for (int i = 0; i < nts; i++) { Al *a = find_al(s, tmp_sites[i]); if (a) al_add(a, d); }
        s->cv[loc_reg(d)] = LONG_MIN;
        if (loc_reg(d) == 5) s->rbpd = UNK;
        if (loc_reg(d) == 4) { unknown_stack(s); s->delta = UNK; }
        return;
    }
    if (!strcmp(m, "push")) {
        if (s->delta != UNK) {
            s->delta -= W;
            int slot = slot_loc(s->delta);
            kill_loc(s, slot, k);
            if (in->nop >= 1 && d >= 0 && (cls1 == L_REG || cls1 == L_STACK || cls1 == L_GLOBAL) && holds(s, d)) alias_loc(s, d, slot);
        }
        return;
    }
    if (!strcmp(m, "pop")) {
        if (s->delta != UNK) {
            int slot = slot_loc(s->delta);
            int has = holds(s, slot);
            int tmp_sites[MAXA], nts = 0;
            if (has) for (int i = 0; i < s->nal; i++) if (al_has(&s->al[i], slot)) tmp_sites[nts++] = s->al[i].site;
            if (cls1 == L_REG) kill_loc(s, d, k);
            else if (cls1 == L_STACK || cls1 == L_GLOBAL) kill_loc(s, d, k);
            for (int i = 0; i < nts; i++) {
                Al *a = find_al(s, tmp_sites[i]);
                if (a && (cls1 == L_REG || cls1 == L_STACK || cls1 == L_GLOBAL)) al_add(a, d);
                else if (a && cls1 == L_DEREF) { a = NULL; }
            }
            if (cls1 == L_DEREF) escape_loc(s, slot);
            s->delta += W;
            drop_stack_holders(s, s->delta, k);
            if (cls1 == L_REG) {
                s->cv[loc_reg(d)] = LONG_MIN;
                if (loc_reg(d) == 5) s->rbpd = UNK;
            }
        } else if (cls1 == L_REG) kill_loc(s, d, k);
        return;
    }
    if (!strcmp(m, "leave")) {
        if (s->rbpd != UNK) {
            s->delta = s->rbpd + W;
            drop_stack_holders(s, s->delta, k);
            kill_loc(s, reg_loc(5), k);
            s->rbpd = UNK;
        } else { unknown_stack(s); s->delta = UNK; }
        return;
    }
    if ((!strcmp(m, "add") || !strcmp(m, "sub")) && cls1 == L_REG && loc_reg(d) == 4) {
        int ok;
        long v = op_imm(in->nop > 1 ? in->op[1] : "", &ok);
        if (ok && s->delta != UNK) {
            if (!strcmp(m, "sub")) s->delta -= (int)v;
            else {
                s->delta += (int)v;
                drop_stack_holders(s, s->delta, k);
            }
        } else { unknown_stack(s); s->delta = UNK; }
        return;
    }
    if (!strcmp(m, "cmp") || !strcmp(m, "test")) {
        if (in->nop >= 2 && (cls1 == L_REG || cls1 == L_STACK || cls1 == L_GLOBAL)) {
            int ok;
            long v = op_imm(in->op[1], &ok);
            if (!strcmp(m, "test") && sr == d && cls2 == cls1) s->nullcmp = d;
            else if (!strcmp(m, "cmp") && ok && v == 0) s->nullcmp = d;
        }
        return;
    }
    if (!strcmp(m, "call")) {
        char nm[64];
        call_name(in->nop ? in->op[0] : "", nm, sizeof nm);
        int ufn = nm[0] ? find_fn_by_label(nm) : -1;
        int indirect = !nm[0];
        if (!strcmp(nm, "free") || !strcmp(nm, "cfree")) {
            free_arg(s, 0, k, cur_fn);
            kill_regs_mask(s, k, 1);
            return;
        }
        if (known_alloc(nm)) {
            handle_arg_escape(s, 0);
            kill_regs_mask(s, k, 1);
            int site = site_new(k, SK_CALL, nm, -1);
            new_alloc(s, site, 0);
            return;
        }
        if (!strcmp(nm, "realloc") || !strcmp(nm, "reallocarray")) {
            free_arg(s, 0, k, cur_fn);
            kill_regs_mask(s, k, 1);
            int site = site_new(k, SK_CALL, nm, -1);
            new_alloc(s, site, 0);
            return;
        }
        if (!strcmp(nm, "exit") || !strcmp(nm, "_exit") || !strcmp(nm, "_Exit")) {
            do_exit_event(s, k);
            s->nal = 0;
            return;
        }
        if (!strcmp(nm, "abort")) { s->nal = 0; return; }
        if (ufn >= 0) {
            int frees = fn_frees_arg[ufn];
            int retal = fn_ret_alloc[ufn];
            if (frees) free_arg(s, 0, k, cur_fn);
            else handle_arg_escape(s, g_bits == 64 ? 6 : 4);
            kill_regs_mask(s, k, 1);
            if (retal) {
                int site = site_new(k, SK_UFN, nm, -1);
                new_alloc(s, site, 0);
            }
            if (g_bits == 32) {}
            return;
        }
        if (!indirect && known_safe(nm)) {
            kill_regs_mask(s, k, 1);
            return;
        }
        handle_arg_escape(s, g_bits == 64 ? 6 : 4);
        kill_regs_mask(s, k, 1);
        return;
    }
    if (!strcmp(m, "syscall") || !strcmp(m, "sysenter") || (!strcmp(m, "int") && in->nop >= 1 && (!strcasecmp(in->op[0], "0x80") || !strcmp(in->op[0], "128")))) {
        long no;
        int have = const_reg(s, 0, &no);
        int is64 = g_bits == 64 && !strcmp(m, "syscall");
        if (have && ((is64 && (no == 60 || no == 231)) || (!is64 && (no == 1 || no == 252)))) {
            do_exit_event(s, k);
            s->nal = 0;
            return;
        }
        if (have && ((is64 && no == 9) || (!is64 && (no == 192 || no == 90)))) {
            long len = LONG_MIN;
            if (is64) const_reg(s, 6, &len);
            else const_reg(s, 1, &len);
            if (!is64 && no == 90) len = LONG_MIN;
            long fl = LONG_MIN;
            if (is64) const_reg(s, 10, &fl);
            else const_reg(s, 6, &fl);
            int anon = fl != LONG_MIN ? ((fl & 0x20) != 0) : 1;
            kill_regs_mask(s, k, 1);
            if (anon) {
                int site = site_new(k, SK_MMAP, "mmap", len == LONG_MIN ? -1 : len);
                new_alloc(s, site, 0);
            }
            return;
        }
        if (have && ((is64 && no == 11) || (!is64 && no == 91))) {
            free_arg(s, 0, k, -1);
            int l = reg_loc(is64 ? 7 : 3);
            escape_loc(s, l);
            kill_regs_mask(s, k, 1);
            return;
        }
        {
            int regs64[6] = { 7, 6, 2, 10, 8, 9 };
            int regs32[6] = { 3, 1, 2, 6, 7, 5 };
            for (int i = 0; i < 6; i++) {
                int r = is64 ? regs64[i] : regs32[i];
                if (have && ((is64 && (no == 0 || no == 1)) || (!is64 && (no == 3 || no == 4))) && i == 1) continue;
                escape_loc(s, reg_loc(r));
            }
        }
        kill_loc(s, reg_loc(0), k);
        s->cv[0] = LONG_MIN;
        if (g_bits == 64) { kill_loc(s, reg_loc(1), k); kill_loc(s, reg_loc(11), k); s->cv[1] = LONG_MIN; s->cv[11] = LONG_MIN; }
        return;
    }
    if (!strcmp(m, "ret") || !strcmp(m, "retn")) {
        int rl = reg_loc(0);
        for (int i = 0; i < s->nal; i++) {
            if (al_has(&s->al[i], rl)) { st_drop(s, i); i--; }
        }
        do_exit_event(s, k);
        s->nal = 0;
        return;
    }
    if (!strcmp(m, "jmp")) {
        if (in->nop >= 1) {
            const char *t = skipw(in->op[0]);
            if (!(ident_start((unsigned char)*t) || *t == '.')) { s->nal = 0; }
        }
        return;
    }
    if (is_jcc(m) || !strcmp(m, "loop") || !strcmp(m, "loope") || !strcmp(m, "loopne")) {
        (void)wasnull;
        return;
    }
    if (!strcmp(m, "nop") || !strcmp(m, "cld") || !strcmp(m, "std") || !strcmp(m, "cdqe") || !strcmp(m, "cdq") || !strcmp(m, "cqo") || !strcmp(m, "cwd") || !strcmp(m, "cbw") || !strcmp(m, "cwde")) {
        if (!strcmp(m, "cqo") || !strcmp(m, "cdq") || !strcmp(m, "cwd")) { kill_loc(s, reg_loc(2), k); s->cv[2] = LONG_MIN; }
        if (!strcmp(m, "cdqe") || !strcmp(m, "cbw") || !strcmp(m, "cwde")) { kill_loc(s, reg_loc(0), k); s->cv[0] = LONG_MIN; }
        return;
    }
    if (!strcmp(m, "mul") || !strcmp(m, "div") || !strcmp(m, "idiv") || (!strcmp(m, "imul") && in->nop == 1)) {
        kill_loc(s, reg_loc(0), k);
        kill_loc(s, reg_loc(2), k);
        s->cv[0] = s->cv[2] = LONG_MIN;
        return;
    }
    if (!strncmp(m, "movs", 4) && (m[4] == 'b' || m[4] == 'w' || m[4] == 'd' || m[4] == 'q') && !m[5] && in->nop == 0) {
        kill_loc(s, reg_loc(6), k); kill_loc(s, reg_loc(7), k); kill_loc(s, reg_loc(1), k);
        return;
    }
    if (!strncmp(m, "stos", 4) || !strncmp(m, "lods", 4) || !strncmp(m, "scas", 4) || !strncmp(m, "cmps", 4)) {
        kill_loc(s, reg_loc(7), k); kill_loc(s, reg_loc(1), k); kill_loc(s, reg_loc(6), k); kill_loc(s, reg_loc(0), k);
        return;
    }
    if (!strcmp(m, "xchg") && in->nop >= 2) {
        if (cls1 == L_REG) kill_loc(s, d, k);
        if (cls2 == L_REG) kill_loc(s, sr, k);
        return;
    }
    if (!strcmp(m, "cpuid") || !strcmp(m, "rdtsc")) {
        kill_loc(s, reg_loc(0), k); kill_loc(s, reg_loc(2), k); kill_loc(s, reg_loc(1), k); kill_loc(s, reg_loc(3), k);
        return;
    }
    if (!strcmp(m, "xor") || !strcmp(m, "sub")) {
        if (in->nop >= 2 && cls1 == L_REG && cls2 == L_REG && d == sr) {
            kill_loc(s, d, k);
            s->cv[loc_reg(d)] = 0;
            return;
        }
    }
    if (!strncmp(m, "ucomis", 6) || !strncmp(m, "comis", 5) || !strcmp(m, "bt") || !strncmp(m, "fcom", 4)) return;
    if (in->nop >= 1) {
        int keep = !strcmp(m, "add") || !strcmp(m, "sub") || !strcmp(m, "inc") || !strcmp(m, "dec");
        if (cls1 == L_REG) {
            int r = loc_reg(d);
            if (keep) {
                s->cv[r] = LONG_MIN;
                if (r == 5) s->rbpd = UNK;
                if (r == 4) { unknown_stack(s); s->delta = UNK; }
                return;
            }
            if (r == 4) { unknown_stack(s); s->delta = UNK; return; }
            if (r == 5) s->rbpd = UNK;
            if (!strcmp(m, "and") && in->nop == 2) {
                int ok;
                op_imm(in->op[1], &ok);
            }
            kill_loc(s, d, k);
            s->cv[r] = LONG_MIN;
        } else if (cls1 == L_STACK || cls1 == L_GLOBAL) {
            if (!keep) kill_loc(s, d, k);
        }
    }
}

static int fn_of_ins(int k) { return I[k].fn; }

typedef struct { int to; St st; int taken; } Edge;

static int find_label_ins(const char *name, int from)
{
    char full[160];
    if (name[0] == '.') {
        const char *par = "";
        for (int i = from; i >= 0; i--) {
            if (I[i].lab && I[i].lab[0] != '.' && !strchr(I[i].lab, '.')) { par = I[i].lab; break; }
            if (I[i].lab && I[i].lab[0] != '.') {
                char tmp[160];
                snprintf(tmp, sizeof tmp, "%s", I[i].lab);
                char *dot = strchr(tmp, '.');
                if (dot) { *dot = 0; par = I[i].lab; }
                break;
            }
        }
        (void)par;
    }
    snprintf(full, sizeof full, "%s", name);
    for (int i = 0; i < nI; i++) {
        if (!I[i].lab) continue;
        if (!strcmp(I[i].lab, full)) return i;
    }
    return -1;
}

static char *canon_target(const char *name, int k)
{
    if (name[0] != '.') return xstrdup(name);
    const char *par = "";
    for (int i = k; i >= 0; i--) {
        if (I[i].lab && I[i].lab[0] != '.') {
            char *dot = strchr(I[i].lab, '.');
            static char buf[160];
            if (dot) { size_t l = (size_t)(dot - I[i].lab); memcpy(buf, I[i].lab, l); buf[l] = 0; par = buf; }
            else par = I[i].lab;
            break;
        }
    }
    Str s = {0};
    sfmt(&s, "%s%s", par, name);
    return sget(&s);
}

static const char *skip_jmp_kw(const char *p)
{
    static const char *const kw[] = { "short", "near", "far", "dword", "qword", NULL };
    for (;;) {
        p = skipw(p);
        int hit = 0;
        for (int i = 0; kw[i]; i++) {
            size_t l = strlen(kw[i]);
            if (!strncasecmp(p, kw[i], l) && (p[l] == ' ' || p[l] == '\t')) { p += l; hit = 1; break; }
        }
        if (!hit) return p;
    }
}

static int target_ins(const char *op, int k)
{
    const char *p = skip_jmp_kw(op);
    if (!(ident_start((unsigned char)*p) || *p == '.')) return -2;
    char nm[128];
    size_t n = 0;
    while ((ident_char((unsigned char)*p) || *p == '.' || *p == '$') && n + 1 < sizeof nm) nm[n++] = *p++;
    nm[n] = 0;
    if (reg_index(nm, n) >= 0) return -2;
    char *c = canon_target(nm, k);
    int t = find_label_ins(c, nI - 1);
    free(c);
    return t < 0 ? -1 : t;
}

static int refine_null(St *s, const Ins *jin, int taken)
{
    if (s->nullcmp < 0) return 0;
    const char *m = jin->mn;
    int zero_on_taken = (!strcmp(m, "jz") || !strcmp(m, "je"));
    int zero_on_fall = (!strcmp(m, "jnz") || !strcmp(m, "jne"));
    if (!zero_on_taken && !zero_on_fall) return 0;
    int isnull = (zero_on_taken && taken) || (zero_on_fall && !taken);
    if (!isnull) return 0;
    int l = s->nullcmp;
    for (int i = 0; i < s->nal; i++) {
        if (al_has(&s->al[i], l) && S[s->al[i].site].kind != SK_PARAM) { st_drop(s, i); i--; }
    }
    return 1;
}

static int succ_edges(int k, const St *out_st_nonref, const St *pre, Edge *e)
{
    int n = 0;
    const Ins *in = &I[k];
    const char *m = in->mn;
    int fnend = fn_end[I[k].fn];
    if (!strcmp(m, "ret") || !strcmp(m, "retn") || !strcmp(m, "hlt")) return 0;
    if (!strcmp(m, "jmp")) {
        int t = in->nop ? target_ins(in->op[0], k) : -2;
        if (t >= 0 && I[t].fn == I[k].fn) { e[n].to = t; e[n].st = *out_st_nonref; e[n].taken = 1; n++; }
        return n;
    }
    if (is_jcc(m) || !strcmp(m, "loop") || !strcmp(m, "loope") || !strcmp(m, "loopne")) {
        int t = in->nop ? target_ins(in->op[0], k) : -2;
        if (t >= 0 && I[t].fn == I[k].fn) {
            e[n].to = t;
            e[n].st = *out_st_nonref;
            e[n].st.nullcmp = pre->nullcmp;
            refine_null(&e[n].st, in, 1);
            e[n].st.nullcmp = -1;
            e[n].taken = 1;
            n++;
        }
        int nx = k + 1;
        if (nx < fnend) {
            e[n].to = nx;
            e[n].st = *out_st_nonref;
            e[n].st.nullcmp = pre->nullcmp;
            refine_null(&e[n].st, in, 0);
            e[n].st.nullcmp = -1;
            e[n].taken = 0;
            n++;
        }
        return n;
    }
    {
        int nx = k + 1;
        if (nx < fnend) { e[n].to = nx; e[n].st = *out_st_nonref; e[n].taken = 0; n++; }
    }
    return n;
}

static St *IN;

static void analyze_fn(int f)
{
    int a = fn_entry[f], b = fn_end[f];
    cur_fn = f;
    for (int i = a; i < b; i++) IN[i].valid = 0;
    St s0;
    st_init(&s0);
    s0.valid = 1;
    {
        int site = site_new(a, SK_PARAM, "", -1);
        s0.al[0].site = (short)site;
        s0.al[0].nh = 0;
        s0.nal = 1;
        int l = g_bits == 64 ? reg_loc(7) : slot_loc(4);
        al_add(&s0.al[0], l);
    }
    IN[a] = s0;
    int *wl = xmalloc(sizeof(int) * (size_t)(b - a + 1) * 64);
    int cap = (b - a + 1) * 64, wn = 0;
    unsigned char *inq = xcalloc((size_t)(b - a + 1), 1);
    wl[wn++] = a;
    inq[0] = 1;
    long guard = 0;
    while (wn > 0 && guard++ < 2000000) {
        int k = wl[--wn];
        inq[k - a] = 0;
        if (I[k].kind != K_CODE) {
            if (k + 1 < b) {
                if (merge_into(&IN[k + 1], &IN[k]) && !inq[k + 1 - a] && wn < cap) { wl[wn++] = k + 1; inq[k + 1 - a] = 1; }
            }
            continue;
        }
        St pre = IN[k];
        St out = pre;
        transfer(&out, k);
        Edge e[2];
        int ne = succ_edges(k, &out, &pre, e);
        for (int i = 0; i < ne; i++) {
            if (merge_into(&IN[e[i].to], &e[i].st) && !inq[e[i].to - a] && wn < cap) { wl[wn++] = e[i].to; inq[e[i].to - a] = 1; }
        }
    }
    free(wl);
    free(inq);
}

static void collect_events(int f)
{
    int a = fn_entry[f], b = fn_end[f];
    g_report = 1;
    cur_fn = f;
    for (int k = a; k < b; k++) {
        if (I[k].kind != K_CODE || !IN[k].valid) continue;
        St out = IN[k];
        transfer(&out, k);
    }
    g_report = 0;
}

static int parse_lines(LineVec *lv)
{
    I = xcalloc((size_t)lv->n + 1, sizeof(Ins));
    nI = 0;
    int incode = 1;
    char parent[200] = "";
    for (int i = 0; i < lv->n; i++) {
        const char *t = skipw(lv->v[i].text);
        Ins *in = &I[nI];
        memset(in, 0, sizeof *in);
        in->line = i;
        in->kind = K_SKIP;
        in->fn = -1;
        if (!*t || *t == ';') { nI++; continue; }
        const char *p = t;
        char word[160];
        size_t n = 0;
        while ((ident_char((unsigned char)*p) || *p == '.' || *p == '$' || *p == '@' || *p == '?') && n + 1 < sizeof word) word[n++] = *p++;
        word[n] = 0;
        const char *q = skipw(p);
        if (n && *q == ':') {
            char full[200];
            if (word[0] == '.') snprintf(full, sizeof full, "%s%s", parent, word);
            else { snprintf(full, sizeof full, "%s", word); snprintf(parent, sizeof parent, "%s", word); }
            in->lab = xstrdup(full);
            q = skipw(q + 1);
            if (*q == ':') q = skipw(q + 1);
            p = q;
            n = 0;
            while ((ident_char((unsigned char)*p) || *p == '.' || *p == '$') && n + 1 < sizeof word) word[n++] = *p++;
            word[n] = 0;
            q = skipw(p);
            if (!n || *q == ';' || !*q) { if (!n || !strcmp(word, "")) { in->kind = K_LABEL; nI++; continue; } }
        }
        if (!n) { nI++; continue; }
        char mn[24];
        size_t ml = n < sizeof mn - 1 ? n : sizeof mn - 1;
        for (size_t z = 0; z < ml; z++) mn[z] = (char)tolower((unsigned char)word[z]);
        mn[ml] = 0;
        if (!strcmp(mn, "rep") || !strcmp(mn, "repe") || !strcmp(mn, "repz") || !strcmp(mn, "repne") || !strcmp(mn, "repnz") || !strcmp(mn, "lock")) {
            p = skipw(p);
            n = 0;
            while (ident_char((unsigned char)*p) && n + 1 < sizeof word) word[n++] = *p++;
            word[n] = 0;
            ml = n < sizeof mn - 1 ? n : sizeof mn - 1;
            for (size_t z = 0; z < ml; z++) mn[z] = (char)tolower((unsigned char)word[z]);
            mn[ml] = 0;
            q = skipw(p);
        }
        static const char *const dirs[] = { "section", "segment", "global", "extern", "extrn", "bits", "default", "align", "alignb", "org", "cpu", "db", "dw", "dd", "dq", "dt", "do", "dy", "dz", "resb", "resw", "resd", "resq", "rest", "reso", "resy", "resz", "equ", "times", "incbin", "struc", "endstruc", "istruc", "iend", "at", "use16", "use32", "use64", "absolute", "common", "static", "weak", NULL };
        int isdir = 0;
        for (int d = 0; dirs[d]; d++) if (!strcmp(mn, dirs[d])) isdir = 1;
        if (!isdir && *q) {
            const char *e2 = q;
            while (ident_char((unsigned char)*e2)) e2++;
            size_t l2 = (size_t)(e2 - q);
            for (int d = 0; dirs[d] && !isdir; d++)
                if (strlen(dirs[d]) == l2 && !strncasecmp(dirs[d], q, l2) && strcmp(dirs[d], "at") && strcmp(dirs[d], "section")) {
                    isdir = 1;
                    if (!in->lab && mn[0]) {
                        char full[200];
                        if (word[0] == '.') snprintf(full, sizeof full, "%s%s", parent, word);
                        else { snprintf(full, sizeof full, "%s", word); snprintf(parent, sizeof parent, "%s", word); }
                        in->lab = xstrdup(full);
                    }
                }
        }
        if (!strcmp(mn, "section") || !strcmp(mn, "segment")) {
            const char *a = skipw(q);
            incode = !strncasecmp(a, ".text", 5) || !strncasecmp(a, "text", 4) || !strncasecmp(a, ".code", 5);
            nI++;
            continue;
        }
        if (isdir || !incode) {
            if (in->lab) in->kind = K_LABEL;
            nI++;
            continue;
        }
        snprintf(in->mn, sizeof in->mn, "%s", mn);
        in->kind = K_CODE;
        const char *a = skipw(q);
        int depth = 0;
        const char *st = a;
        for (const char *z = a;; z++) {
            if (*z == '[' || *z == '(') depth++;
            else if (*z == ']' || *z == ')') depth--;
            else if (*z == '"' || *z == '\'') { char qc = *z++; while (*z && *z != qc) z++; if (!*z) break; continue; }
            if ((*z == ',' && depth == 0) || !*z || *z == ';') {
                size_t l = (size_t)(z - st);
                while (l && (st[l - 1] == ' ' || st[l - 1] == '\t')) l--;
                const char *s2 = skipw(st);
                if (z > s2 || l) {
                    if (in->nop < MAXOPS && (size_t)(s2 - st) <= l && l > (size_t)(s2 - st)) {
                        char *o = xstrndup(s2, l - (size_t)(s2 - st));
                        in->op[in->nop++] = o;
                    }
                }
                if (!*z || *z == ';') break;
                st = z + 1;
            }
        }
        nI++;
    }
    return nI;
}

static void find_functions(void)
{
    nfn = 0;
    capfn = 16;
    fn_entry = xmalloc(sizeof(int) * (size_t)capfn);
    fn_name = xmalloc(sizeof(char *) * (size_t)capfn);
    for (int k = 0; k < nI; k++) {
        if (!I[k].lab || I[k].lab[0] == '.' || strchr(I[k].lab, '.')) continue;
        if (I[k].kind == K_SKIP) continue;
        int is_fn = !strcmp(I[k].lab, "main") || !strcmp(I[k].lab, "_start") || !strcmp(I[k].lab, "start") || !strcmp(I[k].lab, "_main");
        if (!is_fn) {
            for (int j = 0; j < nI && !is_fn; j++) {
                if (I[j].kind != K_CODE || strcmp(I[j].mn, "call") || !I[j].nop) continue;
                char nm[64];
                call_name(I[j].op[0], nm, sizeof nm);
                if (!strcmp(nm, I[k].lab)) is_fn = 1;
            }
        }
        if (!is_fn) continue;
        if (nfn == capfn) { capfn *= 2; fn_entry = xrealloc(fn_entry, sizeof(int) * (size_t)capfn); fn_name = xrealloc(fn_name, sizeof(char *) * (size_t)capfn); }
        fn_entry[nfn] = k;
        fn_name[nfn] = I[k].lab;
        nfn++;
    }
    fn_end = xmalloc(sizeof(int) * (size_t)(nfn + 1));
    for (int f = 0; f < nfn; f++) {
        int e = f + 1 < nfn ? fn_entry[f + 1] : nI;
        int a = fn_entry[f];
        for (int k = a; k < e; k++) {
            int incode_end = 0;
            (void)incode_end;
            I[k].fn = f;
        }
        fn_end[f] = e;
    }
}

static const char *inv_jcc(const char *m)
{
    static const char *const p[][2] = { { "jz", "jnz" }, { "je", "jne" }, { "jg", "jle" }, { "jge", "jl" }, { "ja", "jbe" }, { "jae", "jb" }, { "js", "jns" }, { "jo", "jno" }, { "jp", "jnp" }, { "jc", "jnc" }, { "jnae", "jae" }, { "jnb", "jb" }, { "jnbe", "ja" }, { "jna", "ja" }, { "jnc", "jc" }, { "jng", "jg" }, { "jnge", "jge" }, { "jnl", "jl" }, { "jnle", "jle" }, { "jpe", "jpo" }, { "jpo", "jpe" }, { "jecxz", NULL }, { "jrcxz", NULL }, { NULL, NULL } };
    for (int i = 0; p[i][0]; i++) {
        if (!strcmp(m, p[i][0])) return p[i][1];
        if (p[i][1] && !strcmp(m, p[i][1])) return p[i][0];
    }
    return NULL;
}

typedef struct {
    Fix *v;
    int n, cap;
    int labn;
} FixList;

static void fix_add(FixList *F, int line, int where, const char *text, int repl)
{
    for (int i = 0; i < F->n; i++)
        if (F->v[i].line == line && F->v[i].where == where && !strcmp(F->v[i].text, text)) return;
    if (F->n == F->cap) { F->cap = F->cap ? F->cap * 2 : 32; F->v = xrealloc(F->v, sizeof(Fix) * (size_t)F->cap); }
    F->v[F->n].line = line;
    F->v[F->n].where = where;
    F->v[F->n].text = xstrdup(text);
    F->v[F->n].repl = repl;
    F->n++;
}

static int holder_operand(char *buf, size_t sz, int h, const St *st, int pushed)
{
    const char *nm = loc_names[h];
    if (nm[0] == 'R') { snprintf(buf, sz, "%s", rname(atoi(nm + 1))); return 1; }
    if (nm[0] == 'S') {
        int off = atoi(nm + 1);
        if (st->rbpd != UNK) { snprintf(buf, sz, "[%s%+d]", rname(5), off - st->rbpd); return 1; }
        if (st->delta != UNK) { snprintf(buf, sz, "[%s%+d]", rname(4), off - st->delta + pushed * W); return 1; }
        return 0;
    }
    if (nm[0] == 'G') {
        char sym[64];
        snprintf(sym, sizeof sym, "%s", nm + 1);
        char *pl = strpbrk(sym, "+-");
        long disp = 0;
        if (pl) { disp = strtol(pl, NULL, 10); *pl = 0; }
        if (disp) snprintf(buf, sz, "[%s%+ld]", sym, disp);
        else snprintf(buf, sz, "[%s]", sym);
        return 1;
    }
    return 0;
}

static int pick_holder(const Al *a)
{
    int best = -1, score = -1;
    for (int i = 0; i < a->nh; i++) {
        int h = a->h[i];
        int sc = 0;
        if (loc_is_reg(h)) sc = is_callee_saved(loc_reg(h)) ? 5 : 2;
        else if (loc_is_slot(h)) sc = 4;
        else sc = 3;
        if (sc > score) { score = sc; best = h; }
    }
    return best;
}

static int gen_free(Str *out, const Site *site, int h, const St *st)
{
    char hb[96];
    if (site->kind == SK_MMAP) {
        if (site->len < 0) return 0;
        if (g_bits == 64) {
            if (!holder_operand(hb, sizeof hb, h, st, 5)) return 0;
            sfmt(out, "    push rax\n    push rdi\n    push rsi\n    push rcx\n    push r11\n    mov rdi, %s\n    mov esi, %ld\n    mov eax, 11\n    syscall\n    pop r11\n    pop rcx\n    pop rsi\n    pop rdi\n    pop rax\n", hb, site->len);
        } else {
            if (!holder_operand(hb, sizeof hb, h, st, 3)) return 0;
            sfmt(out, "    push eax\n    push ebx\n    push ecx\n    mov ebx, %s\n    mov ecx, %ld\n    mov eax, 91\n    int 0x80\n    pop ecx\n    pop ebx\n    pop eax\n", hb, site->len);
        }
        return 1;
    }
    if (g_bits == 64) {
        if (!holder_operand(hb, sizeof hb, h, st, 2)) return 0;
        sfmt(out, "    push rax\n    push rdi\n    mov rdi, %s\n    call free\n    pop rdi\n    pop rax\n", hb);
    } else {
        if (!holder_operand(hb, sizeof hb, h, st, 1)) return 0;
        sfmt(out, "    push eax\n    push %s%s\n    call free\n    add esp, 4\n    pop eax\n", hb[0] == '[' ? "dword " : "", hb);
    }
    return 1;
}

static void sanitize_push(Str *s)
{
    (void)s;
}

typedef struct { int *site; int n; } SiteSet;

static int leak_event_edges(FixList *F, const Ev *ev, int *ok)
{
    int k = ev->k;
    int f = I[k].fn;
    int a = fn_entry[f], b = fn_end[f];
    int made = 0;
    *ok = 1;
    for (int p = a; p < b; p++) {
        if (I[p].kind != K_CODE || !IN[p].valid) continue;
        St pre = IN[p];
        St out = pre;
        g_report = 0;
        transfer(&out, p);
        Edge e[2];
        int ne = succ_edges(p, &out, &pre, e);
        for (int i = 0; i < ne; i++) {
            int to = e[i].to;
            int reaches = to == k;
            if (!reaches) {
                int q = to;
                while (q < k && q < b && I[q].kind != K_CODE) q++;
                if (q == k) reaches = 1;
            }
            if (ev->kind == EV_PREV) reaches = p >= S[ev->site].ins && to <= S[ev->site].ins;
            if (!reaches) continue;
            Al *al = find_al(&e[i].st, ev->site);
            if (!al) continue;
            int h = pick_holder(al);
            if (h < 0) { *ok = 0; continue; }
            Str code = {0};
            if (!gen_free(&code, &S[ev->site], h, &e[i].st)) { *ok = 0; free(code.s); continue; }
            const char *m = I[p].mn;
            if (e[i].taken && is_jcc(m) && strcmp(m, "jmp")) {
                const char *inv = inv_jcc(m);
                if (!inv) { *ok = 0; free(code.s); continue; }
                char lbl[64];
                snprintf(lbl, sizeof lbl, "__cly_fix_%d", F->labn++);
                Str r = {0};
                sfmt(&r, "    %s %s\n%s    jmp %s\n%s:\n", inv, lbl, code.s ? code.s : "", I[p].op[0], lbl);
                fix_add(F, I[p].line, 0, r.s, 1);
                free(r.s);
            } else if (!strcmp(m, "jmp")) {
                fix_add(F, I[p].line, 0, code.s, 0);
            } else {
                fix_add(F, I[p].line, 1, code.s, 0);
            }
            free(code.s);
            made++;
        }
    }
    return made;
}

static void ins_text(Str *out, const char *s) { sadd(out, s); }

static int list_lines(const Ev *evs, int n, int site, int kind, char *buf, size_t sz)
{
    int cnt = 0;
    buf[0] = 0;
    size_t used = 0;
    for (int i = 0; i < n; i++) {
        if (evs[i].site != site || evs[i].kind != kind) continue;
        int ln = g_lv->v[I[evs[i].k].line].line;
        cnt++;
        if (cnt <= 4) {
            int w = snprintf(buf + used, sz - used, "%s%d", cnt > 1 ? ", " : "", ln);
            if (w > 0) used += (size_t)w;
        }
    }
    if (cnt > 4 && used + 8 < sz) snprintf(buf + used, sz - used, ", ...");
    return cnt;
}

static const char *site_desc(const Site *s, char *b, size_t sz)
{
    if (s->kind == SK_MMAP) snprintf(b, sz, "the mmap");
    else if (s->kind == SK_UFN) snprintf(b, sz, "the block returned by `%s'", s->fn);
    else snprintf(b, sz, "the block from `%s'", s->fn);
    return b;
}

int leak_check(LineVec *lv, int bits, LineVec *fixed, int *nfixed)
{
    g_lv = lv;
    g_bits = bits;
    W = bits == 64 ? 8 : 4;
    reg_mode(64);
    nS = nloc = nEV = 0;
    parse_lines(lv);
    if (!nI) return 0;
    find_functions();
    if (!nfn) return 0;
    fn_ret_alloc = xcalloc((size_t)nfn, sizeof(int));
    fn_frees_arg = xcalloc((size_t)nfn, sizeof(int));
    IN = xcalloc((size_t)nI, sizeof(St));
    for (int round = 0; round < 4; round++) {
        int before_r[64], before_f[64];
        for (int f = 0; f < nfn && f < 64; f++) { before_r[f] = fn_ret_alloc[f]; before_f[f] = fn_frees_arg[f]; }
        for (int f = 0; f < nfn; f++) {
            g_report = 0;
            analyze_fn(f);
            int a = fn_entry[f], b = fn_end[f];
            for (int k = a; k < b; k++) {
                if (I[k].kind != K_CODE || !IN[k].valid) continue;
                if (strcmp(I[k].mn, "ret") && strcmp(I[k].mn, "retn")) continue;
                int rl = reg_loc(0);
                for (int i = 0; i < IN[k].nal; i++)
                    if (al_has(&IN[k].al[i], rl) && S[IN[k].al[i].site].kind != SK_PARAM) fn_ret_alloc[f] = 1;
            }
        }
        int same = 1;
        for (int f = 0; f < nfn && f < 64; f++) if (before_r[f] != fn_ret_alloc[f] || before_f[f] != fn_frees_arg[f]) same = 0;
        if (same) break;
    }
    nEV = 0;
    for (int f = 0; f < nfn; f++) {
        analyze_fn(f);
        collect_events(f);
    }
    int nleaks = 0;
    int sites_done[256], nsd = 0;
    FixList F = {0};
    int fixable_all = 1, any_fix = 0;
    for (int i = 0; i < nEV; i++) {
        int site = EV[i].site;
        int seen = 0;
        for (int j = 0; j < nsd; j++) if (sites_done[j] == site) seen = 1;
        if (seen) continue;
        if (nsd < 256) sites_done[nsd++] = site;
        char lines[128], lines2[128], lines3[128], d[96];
        int ne = list_lines(EV, nEV, site, EV_EXIT, lines, sizeof lines);
        int nl = list_lines(EV, nEV, site, EV_LOST, lines2, sizeof lines2);
        int np = list_lines(EV, nEV, site, EV_PREV, lines3, sizeof lines3);
        const Site *sp = &S[site];
        g_pos.file = lv->v[I[sp->ins].line].file;
        g_pos.line = lv->v[I[sp->ins].line].line;
        site_desc(sp, d, sizeof d);
        if (np && !ne && !nl) warn("potential memory leak: %s is allocated again in a loop without freeing the previous block (line%s %s)", d, np > 1 ? "s" : "", lines3);
        else if (np) warn("potential memory leak: %s is not freed on every path (it is also allocated again in a loop at line%s %s)", d, np > 1 ? "s" : "", lines3);
        else if (ne && nl) warn("potential memory leak: %s is not freed on every path (leaves at line%s %s, lost at line%s %s)", d, ne > 1 ? "s" : "", lines, nl > 1 ? "s" : "", lines2);
        else if (ne) warn("potential memory leak: %s is never freed before the program leaves (line%s %s)", d, ne > 1 ? "s" : "", lines);
        else warn("potential memory leak: %s is lost when its pointer is overwritten (line%s %s)", d, nl > 1 ? "s" : "", lines2);
        nleaks++;
        int ok = 1, made = 0;
        for (int e = 0; e < nEV; e++) {
            if (EV[e].site != site) continue;
            int ok1;
            made += leak_event_edges(&F, &EV[e], &ok1);
            if (!ok1) ok = 0;
        }
        if (!made || !ok) fixable_all = 0;
        if (made) any_fix = 1;
    }
    g_pos.file = NULL;
    *nfixed = 0;
    if (fixed && any_fix) {
        memset(fixed, 0, sizeof *fixed);
        int *nb = xcalloc((size_t)lv->n + 1, sizeof(int));
        (void)nb;
        for (int i = 0; i < lv->n; i++) {
            int repl = 0;
            for (int j = 0; j < F.n; j++) {
                if (F.v[j].line != i || F.v[j].where != 0) continue;
                Str t = {0};
                sadd(&t, F.v[j].text);
                char *s = sget(&t);
                for (char *p = s; *p;) {
                    char *nl = strchr(p, '\n');
                    if (nl) *nl = 0;
                    if (*p) lv_add(fixed, p, lv->v[i].file, lv->v[i].line);
                    if (!nl) break;
                    p = nl + 1;
                }
                free(s);
                if (F.v[j].repl) repl = 1;
            }
            if (!repl) lv_add(fixed, lv->v[i].text, lv->v[i].file, lv->v[i].line);
            for (int j = 0; j < F.n; j++) {
                if (F.v[j].line != i || F.v[j].where != 1) continue;
                Str t = {0};
                sadd(&t, F.v[j].text);
                char *s = sget(&t);
                for (char *p = s; *p;) {
                    char *nl = strchr(p, '\n');
                    if (nl) *nl = 0;
                    if (*p) lv_add(fixed, p, lv->v[i].file, lv->v[i].line);
                    if (!nl) break;
                    p = nl + 1;
                }
                free(s);
            }
        }
        *nfixed = fixable_all ? 2 : 1;
    }
    (void)ins_text;
    (void)sanitize_push;
    (void)fn_of_ins;
    return nleaks;
}
