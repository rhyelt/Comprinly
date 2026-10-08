#include "cly.h"

enum { LK_BLANK, LK_LABEL, LK_INSN, LK_OTHER };
enum { F_CF = 1, F_PF = 2, F_AF = 4, F_ZF = 8, F_SF = 16, F_OF = 32, F_ALL = 63 };

typedef struct {
    int kind;
    int local;
    char *name;
    char mn[24];
    char *ops[4];
    int nops;
    char w1[24], w2[24];
    char *plain;
} Ln;

static int OB;
static int OK_KERNEL;

static int idc(int c) { return isalnum(c) || c == '_' || c == '.' || c == '$' || c == '#' || c == '@' || c == '~' || c == '?'; }

static char *strip_comment(const char *t)
{
    char *s = xstrdup(t);
    char q = 0;
    for (char *p = s; *p; p++) {
        if (q) {
            if (*p == '\\' && q == '`' && p[1]) p++;
            else if (*p == q) q = 0;
        } else if (*p == '"' || *p == '\'' || *p == '`') q = *p;
        else if (*p == ';') { *p = 0; break; }
    }
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    char *b = s;
    while (*b && isspace((unsigned char)*b)) b++;
    if (b != s) memmove(s, b, strlen(b) + 1);
    return s;
}

static void lowcpy(char *d, size_t n, const char *s)
{
    size_t i = 0;
    for (; s[i] && i + 1 < n; i++) d[i] = (char)tolower((unsigned char)s[i]);
    d[i] = 0;
}

static int in_list(const char *w, const char *const *l)
{
    for (int i = 0; l[i]; i++) if (!strcmp(w, l[i])) return 1;
    return 0;
}

static const char *const prefixes[] = { "lock", "rep", "repe", "repz", "repne", "repnz", "bnd", "a16", "a32", "o16", "o32", "xacquire", "xrelease", "nobnd", "times", NULL };
static const char *const datad[] = { "db", "dw", "dd", "dq", "dt", "do", "dy", "dz", "resb", "resw", "resd", "resq", "rest", "reso", "resy", "resz", "incbin", "equ", "section", "segment", "global", "extern", "extrn", "default", "align", "alignb", "cpu", "bits", "public", NULL };

static void split_ops(char *s, Ln *L)
{
    int depth = 0;
    char q = 0;
    char *start = s;
    for (char *p = s;; p++) {
        if (q) {
            if (*p == q) q = 0;
            if (!*p) break;
            continue;
        }
        if (*p == '"' || *p == '\'' || *p == '`') q = *p;
        else if (*p == '[' || *p == '(') depth++;
        else if (*p == ']' || *p == ')') depth--;
        if ((*p == ',' && depth == 0) || !*p) {
            char save = *p;
            *p = 0;
            char *a = start;
            while (*a && isspace((unsigned char)*a)) a++;
            char *e = a + strlen(a);
            while (e > a && isspace((unsigned char)e[-1])) *--e = 0;
            if (L->nops < 4) L->ops[L->nops] = xstrdup(a);
            L->nops++;
            start = p + 1;
            if (!save) break;
        }
    }
}

static void parse_line(const char *text, Ln *L)
{
    memset(L, 0, sizeof *L);
    char *s = strip_comment(text);
    if (!*s) { L->kind = LK_BLANK; free(s); return; }
    L->plain = xstrdup(s);
    if (s[0] == '%' || s[0] == '[' || !idc((unsigned char)s[0])) { L->kind = LK_OTHER; free(s); return; }
    char *p = s;
    while (idc((unsigned char)*p)) p++;
    char tok[128];
    size_t tn = (size_t)(p - s);
    if (tn >= sizeof tok) { L->kind = LK_OTHER; free(s); return; }
    memcpy(tok, s, tn);
    tok[tn] = 0;
    lowcpy(L->w1, sizeof L->w1, tok);
    if (*p == ':') {
        char *r = p + 1;
        while (isspace((unsigned char)*r)) r++;
        if (*r) { L->kind = LK_OTHER; free(s); return; }
        L->kind = LK_LABEL;
        L->name = xstrdup(tok);
        L->local = tok[0] == '.' && tok[1] != '.';
        free(s);
        return;
    }
    char *r = p;
    while (isspace((unsigned char)*r)) r++;
    if (!*r) {
        if (insn_known(tok) && !in_list(L->w1, prefixes) && !in_list(L->w1, datad)) {
            L->kind = LK_INSN;
            strcpy(L->mn, L->w1);
        } else {
            L->kind = LK_LABEL;
            L->name = xstrdup(tok);
            L->local = tok[0] == '.' && tok[1] != '.';
        }
        free(s);
        return;
    }
    char *q = r;
    while (idc((unsigned char)*q)) q++;
    if (q > r && (size_t)(q - r) < sizeof L->w2) {
        char t2[32];
        memcpy(t2, r, (size_t)(q - r));
        t2[q - r] = 0;
        lowcpy(L->w2, sizeof L->w2, t2);
    }
    if (insn_known(tok) && !in_list(L->w1, prefixes) && !in_list(L->w1, datad)) {
        L->kind = LK_INSN;
        strcpy(L->mn, L->w1);
        char *ops = xstrdup(r);
        split_ops(ops, L);
        free(ops);
        if (L->nops > 4) {
            L->kind = LK_OTHER;
            for (int i = 0; i < 4; i++) { free(L->ops[i]); L->ops[i] = NULL; }
            L->nops = 0;
        }
        free(s);
        return;
    }
    L->kind = LK_OTHER;
    if (in_list(L->w2, datad) && !in_list(L->w1, datad) && !in_list(L->w1, prefixes)) {
        L->name = xstrdup(tok);
        L->local = tok[0] == '.' && tok[1] != '.';
    }
    free(s);
}

static void free_ln(Ln *L)
{
    free(L->name);
    free(L->plain);
    for (int i = 0; i < 4; i++) free(L->ops[i]);
}

static const char *const r64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", NULL };
static const char *const r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", NULL };
static const char *const r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", NULL };
static const char *const r8[] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh", "spl", "bpl", "sil", "dil", NULL };

static int regw(const char *s, char *r32name)
{
    char l[16];
    lowcpy(l, sizeof l, s);
    if (r32name) r32name[0] = 0;
    for (int i = 0; r64[i]; i++) if (!strcmp(l, r64[i])) { if (r32name) strcpy(r32name, r32[i]); return 64; }
    for (int i = 0; r32[i]; i++) if (!strcmp(l, r32[i])) { if (r32name) strcpy(r32name, r32[i]); return 32; }
    for (int i = 0; r16[i]; i++) if (!strcmp(l, r16[i])) return 16;
    for (int i = 0; r8[i]; i++) if (!strcmp(l, r8[i])) return 8;
    if (l[0] == 'r' && isdigit((unsigned char)l[1])) {
        char *e;
        long n = strtol(l + 1, &e, 10);
        if (n >= 8 && n <= 15) {
            if (!*e) { if (r32name) snprintf(r32name, 8, "r%ldd", n); return 64; }
            if (!strcmp(e, "d")) { if (r32name) snprintf(r32name, 8, "r%ldd", n); return 32; }
            if (!strcmp(e, "w")) return 16;
            if (!strcmp(e, "b")) return 8;
        }
    }
    return 0;
}

static int is_num(const char *s, long *v)
{
    char *e;
    if (!*s) return 0;
    errno = 0;
    long x = strtol(s, &e, 0);
    if (*e || errno) return 0;
    *v = x;
    return 1;
}

static int cond_mask(const char *c)
{
    static const struct { const char *n; int m; } t[] = {
        {"e", F_ZF}, {"z", F_ZF}, {"ne", F_ZF}, {"nz", F_ZF},
        {"b", F_CF}, {"c", F_CF}, {"nae", F_CF}, {"ae", F_CF}, {"nb", F_CF}, {"nc", F_CF},
        {"be", F_CF | F_ZF}, {"na", F_CF | F_ZF}, {"a", F_CF | F_ZF}, {"nbe", F_CF | F_ZF},
        {"s", F_SF}, {"ns", F_SF}, {"o", F_OF}, {"no", F_OF},
        {"p", F_PF}, {"pe", F_PF}, {"np", F_PF}, {"po", F_PF},
        {"l", F_SF | F_OF}, {"nge", F_SF | F_OF}, {"ge", F_SF | F_OF}, {"nl", F_SF | F_OF},
        {"le", F_ZF | F_SF | F_OF}, {"ng", F_ZF | F_SF | F_OF}, {"g", F_ZF | F_SF | F_OF}, {"nle", F_ZF | F_SF | F_OF},
        {NULL, 0}
    };
    for (int i = 0; t[i].n; i++) if (!strcmp(c, t[i].n)) return t[i].m;
    return 0;
}

static const char *inv_cc(const char *c)
{
    static const char *const p[][2] = {
        {"e", "ne"}, {"z", "nz"}, {"b", "ae"}, {"c", "nc"}, {"be", "a"}, {"na", "a"}, {"nae", "ae"}, {"nb", "b"}, {"nbe", "be"},
        {"s", "ns"}, {"o", "no"}, {"p", "np"}, {"pe", "po"}, {"l", "ge"}, {"nge", "ge"}, {"le", "g"}, {"ng", "g"}, {"nl", "l"}, {"nle", "le"},
        {"ne", "e"}, {"nz", "z"}, {"ae", "b"}, {"nc", "c"}, {"a", "be"}, {"ns", "s"}, {"no", "o"}, {"np", "p"}, {"po", "pe"}, {"ge", "l"}, {"g", "le"},
        {NULL, NULL}
    };
    for (int i = 0; p[i][0]; i++) if (!strcmp(c, p[i][0])) return p[i][1];
    return NULL;
}

static int is_jcc(const char *mn, const char **cc)
{
    if (mn[0] != 'j' || !strcmp(mn, "jmp")) return 0;
    if (!cond_mask(mn + 1)) return 0;
    *cc = mn + 1;
    return 1;
}

static int flag_effects(const Ln *x, int *rd, int *wr)
{
    const char *m = x->mn;
    static const char *const pass[] = { "mov", "lea", "push", "pop", "movzx", "movsx", "movsxd", "xchg", "nop", "cdq", "cqo", "cwd", "cbw", "cwde", "cdqe", "not", "bswap", "leave", "pause", "cld", "std", "syscall", "movd", "movq", "movaps", "movups", "movdqa", "movdqu", "pxor", "xorps", "movss", NULL };
    static const char *const wall[] = { "add", "sub", "cmp", "test", "and", "or", "xor", "neg", "mul", "imul", "div", "idiv", NULL };
    *rd = 0;
    *wr = 0;
    if (in_list(m, pass)) return 1;
    if (in_list(m, wall)) { *wr = F_ALL; return 1; }
    if (!strcmp(m, "adc") || !strcmp(m, "sbb")) { *rd = F_CF; *wr = F_ALL; return 1; }
    if (!strcmp(m, "inc") || !strcmp(m, "dec")) { *wr = F_ALL & ~F_CF; return 1; }
    if (!strcmp(m, "clc") || !strcmp(m, "stc")) { *wr = F_CF; return 1; }
    if (!strcmp(m, "cmc")) { *rd = F_CF; *wr = F_CF; return 1; }
    if (!strcmp(m, "shl") || !strcmp(m, "sal") || !strcmp(m, "shr") || !strcmp(m, "sar")) {
        long v;
        if (x->nops == 2 && is_num(x->ops[1], &v) && v > 0 && v < 32) { *wr = F_ALL; return 1; }
        return 0;
    }
    if (!strncmp(m, "set", 3) && cond_mask(m + 3)) { *rd = cond_mask(m + 3); return 1; }
    if (!strncmp(m, "cmov", 4) && cond_mask(m + 4)) { *rd = cond_mask(m + 4); return 1; }
    return 0;
}

static int other_passes(const Ln *x)
{
    if (in_list(x->w1, datad)) return strcmp(x->w1, "bits") != 0;
    if (x->w2[0] && in_list(x->w2, datad)) return 1;
    return 0;
}

static int flags_dead(Ln *L, int n, int at, int need)
{
    int steps = 0;
    for (int i = at + 1; i < n; i++) {
        Ln *x = &L[i];
        if (x->kind == LK_BLANK || x->kind == LK_LABEL) continue;
        if (x->kind == LK_OTHER) {
            if (other_passes(x)) continue;
            return 0;
        }
        int rd, wr;
        if (!flag_effects(x, &rd, &wr)) return 0;
        if (rd & need) return 0;
        need &= ~wr;
        if (!need) return 1;
        if (++steps > 40) return 0;
    }
    return 1;
}

static int is_term(const Ln *x)
{
    static const char *const t[] = { "jmp", "ret", "retn", "retf", "iret", "iretd", "iretq", "ud2", NULL };
    return x->kind == LK_INSN && in_list(x->mn, t);
}

static void set_text(LineVec *lv, int i, const char *t)
{
    free(lv->v[i].text);
    lv->v[i].text = xstrdup(t);
}

static int ident_only(const char *s)
{
    if (!*s || isdigit((unsigned char)*s)) return 0;
    for (const char *p = s; *p; p++) if (!idc((unsigned char)*p)) return 0;
    return !regw(s, NULL);
}

static int direct_target(const Ln *x, char *out, size_t n)
{
    if (x->kind != LK_INSN || x->nops != 1) return 0;
    const char *o = x->ops[0];
    if (!strncasecmp(o, "short ", 6)) o += 6;
    else if (!strncasecmp(o, "near ", 5)) o += 5;
    while (isspace((unsigned char)*o)) o++;
    if (!ident_only(o) || strlen(o) >= n) return 0;
    strcpy(out, o);
    return 1;
}

typedef struct {
    LineVec *lv;
    Ln *L;
    int n;
    char **scope;
} Ctx;

static void compute_scope(Ctx *c)
{
    char cur[128] = "";
    for (int i = 0; i < c->n; i++) {
        Ln *x = &c->L[i];
        if (x->name && !x->local) { strncpy(cur, x->name, sizeof cur - 1); cur[sizeof cur - 1] = 0; }
        free(c->scope[i]);
        c->scope[i] = xstrdup(cur);
    }
}

static char *scoped(Ctx *c, int line, const char *t)
{
    if (t[0] == '.' && t[1] != '.') {
        Str s = {0};
        sadd(&s, c->scope[line]);
        sadd(&s, t);
        return sget(&s);
    }
    return xstrdup(t);
}

static int find_label(Ctx *c, const char *sc_name)
{
    for (int i = 0; i < c->n; i++) {
        Ln *x = &c->L[i];
        if (!x->name || (x->kind != LK_LABEL)) continue;
        char *s = scoped(c, i, x->name);
        int eq = !strcmp(s, sc_name);
        free(s);
        if (eq) return i;
    }
    return -1;
}

static int next_insn(Ctx *c, int i, int skip_labels, int *crossed_label_to, const char *want)
{
    for (int k = i + 1; k < c->n; k++) {
        Ln *x = &c->L[k];
        if (x->kind == LK_BLANK) continue;
        if (x->kind == LK_LABEL) {
            if (!skip_labels) return -1;
            if (want && crossed_label_to) {
                char *s = scoped(c, k, x->name);
                if (!strcmp(s, want)) *crossed_label_to = 1;
                free(s);
            }
            continue;
        }
        return x->kind == LK_INSN ? k : -1;
    }
    return -1;
}

static void set_line(Ctx *c, int i, const char *t)
{
    set_text(c->lv, i, t);
    free_ln(&c->L[i]);
    parse_line(t, &c->L[i]);
}

static int mem_same(const char *a, const char *b)
{
    const char *pa = strchr(a, '['), *pb = strchr(b, '[');
    if (!pa || !pb || !strchr(pa, ']')) return 0;
    for (const char *p = a; p < pa; p++) if (*p == ':') return 0;
    for (const char *p = b; p < pb; p++) if (*p == ':') return 0;
    return !strcasecmp(pa, pb);
}

static int peephole(Ctx *c)
{
    Ln *L = c->L;
    int n = c->n;
    int changed = 0;
    for (int i = 0; i < n; i++) {
        Ln *x = &L[i];
        if (x->kind != LK_INSN) continue;
        char buf[160], r32n[16], t[128], t2[128];
        long v;
        if (!strcmp(x->mn, "mov") && x->nops == 2) {
            int w = regw(x->ops[0], r32n);
            if ((w == 32 || w == 64) && is_num(x->ops[1], &v) && v == 0 && flags_dead(L, n, i, F_ALL)) {
                snprintf(buf, sizeof buf, "    xor %s, %s", r32n, r32n);
                set_line(c, i, buf);
                changed = 1;
                continue;
            }
            int w2 = regw(x->ops[1], NULL);
            int wide_ok = w && w == w2 && (w != 32 || OB == 32);
            if (wide_ok && !strcasecmp(x->ops[0], x->ops[1])) {
                set_line(c, i, "");
                changed = 1;
                continue;
            }
            int k = next_insn(c, i, 0, NULL, NULL);
            if (k > 0 && !strcmp(L[k].mn, "mov") && L[k].nops == 2) {
                if (wide_ok && !strcasecmp(L[k].ops[0], x->ops[1]) && !strcasecmp(L[k].ops[1], x->ops[0])) {
                    set_line(c, k, "");
                    changed = 1;
                    continue;
                }
                int w3 = regw(x->ops[1], NULL);
                if (OK_KERNEL != K_BARE && w == 0 && w3 && (w3 != 32 || OB == 32) && !strcasecmp(L[k].ops[0], x->ops[1]) && mem_same(x->ops[0], L[k].ops[1])) {
                    set_line(c, k, "");
                    changed = 1;
                    continue;
                }
            }
        } else if (!strcmp(x->mn, "cmp") && x->nops == 2) {
            if (regw(x->ops[0], NULL) && is_num(x->ops[1], &v) && v == 0) {
                snprintf(buf, sizeof buf, "    test %s, %s", x->ops[0], x->ops[0]);
                set_line(c, i, buf);
                changed = 1;
                continue;
            }
        } else if ((!strcmp(x->mn, "add") || !strcmp(x->mn, "sub")) && x->nops == 2 && is_num(x->ops[1], &v) && (v == 1 || v == -1)) {
            int w = regw(x->ops[0], NULL);
            const char *o = x->ops[0];
            int mem = strchr(o, '[') && (!strncasecmp(o, "byte", 4) || !strncasecmp(o, "word", 4) || !strncasecmp(o, "dword", 5) || !strncasecmp(o, "qword", 5));
            if ((w >= 16 || mem) && flags_dead(L, n, i, F_CF)) {
                int up = (!strcmp(x->mn, "add")) == (v == 1);
                snprintf(buf, sizeof buf, "    %s %s", up ? "inc" : "dec", o);
                set_line(c, i, buf);
                changed = 1;
                continue;
            }
        }
        if (!strcmp(x->mn, "jmp") && direct_target(x, t, sizeof t)) {
            char *want = scoped(c, i, t);
            int hit = 0;
            next_insn(c, i, 1, &hit, want);
            if (hit) {
                free(want);
                set_line(c, i, "");
                changed = 1;
                continue;
            }
            int tl = find_label(c, want);
            free(want);
            if (tl >= 0) {
                int j = next_insn(c, tl, 1, NULL, NULL);
                if (j >= 0 && j != i && !strcmp(L[j].mn, "jmp") && direct_target(&L[j], t2, sizeof t2)) {
                    char *w2 = scoped(c, j, t2);
                    char *w1 = scoped(c, i, t);
                    int diff = strcmp(w1, w2) != 0;
                    free(w1);
                    if (diff) {
                        snprintf(buf, sizeof buf, "    jmp %s", w2);
                        set_line(c, i, buf);
                        changed = 1;
                    }
                    free(w2);
                    if (diff) continue;
                }
            }
        }
        const char *cc;
        if (is_jcc(x->mn, &cc) && direct_target(x, t, sizeof t) && inv_cc(cc)) {
            int j = next_insn(c, i, 0, NULL, NULL);
            if (j > 0 && !strcmp(L[j].mn, "jmp") && direct_target(&L[j], t2, sizeof t2)) {
                char *want = scoped(c, i, t);
                int hit = 0;
                next_insn(c, j, 1, &hit, want);
                free(want);
                if (hit) {
                    snprintf(buf, sizeof buf, "    j%s %s", inv_cc(cc), t2);
                    set_line(c, i, buf);
                    set_line(c, j, "");
                    changed = 1;
                    continue;
                }
            }
        }
        if (!strcmp(x->mn, "call") && direct_target(x, t, sizeof t)) {
            int j = next_insn(c, i, 0, NULL, NULL);
            if (j > 0 && (!strcmp(L[j].mn, "ret") || !strcmp(L[j].mn, "retn")) && L[j].nops == 0) {
                snprintf(buf, sizeof buf, "    jmp %s", t);
                set_line(c, i, buf);
                set_line(c, j, "");
                changed = 1;
                continue;
            }
            char *want = scoped(c, i, t);
            int tl = find_label(c, want);
            free(want);
            if (tl >= 0) {
                int j2 = next_insn(c, tl, 1, NULL, NULL);
                if (j2 >= 0 && !strcmp(L[j2].mn, "jmp") && direct_target(&L[j2], t2, sizeof t2)) {
                    char *w2 = scoped(c, j2, t2);
                    char *w1 = scoped(c, i, t);
                    int diff = strcmp(w1, w2) != 0;
                    free(w1);
                    if (diff) {
                        snprintf(buf, sizeof buf, "    call %s", w2);
                        set_line(c, i, buf);
                        changed = 1;
                    }
                    free(w2);
                    if (diff) continue;
                }
            }
        }
        if (is_term(x)) {
            for (int k = i + 1; k < n; k++) {
                if (L[k].kind == LK_BLANK) continue;
                if (L[k].kind != LK_INSN) break;
                set_line(c, k, "");
                changed = 1;
            }
        }
    }
    return changed;
}

static void reparse(Ctx *c)
{
    for (int i = 0; i < c->n; i++) {
        free_ln(&c->L[i]);
        parse_line(c->lv->v[i].text, &c->L[i]);
    }
    compute_scope(c);
}

static int unsafe_operand(const char *o)
{
    if (strchr(o, '$') || strchr(o, '.')) return 1;
    char l[160];
    lowcpy(l, sizeof l, o);
    static const char *const bad[] = { "rsp", "esp", "rbp", "ebp", "spl", "bpl", NULL };
    for (int i = 0; bad[i]; i++) if (strstr(l, bad[i])) return 1;
    for (const char *p = l; *p; p++) {
        if ((p[0] == 's' || p[0] == 'b') && p[1] == 'p' && !isalnum((unsigned char)p[2]) && (p == l || !isalnum((unsigned char)p[-1]))) return 1;
    }
    return 0;
}

static int inline_calls(Ctx *c, LineVec *out_lv)
{
    static const char *const safe[] = { "mov", "add", "sub", "inc", "dec", "xor", "and", "or", "not", "neg", "lea", "shl", "shr", "sar", "imul", "movzx", "movsx", "movsxd", "cmp", "test", "nop", "cdq", "cqo", "cdqe", NULL };
    int n = c->n;
    Ln *L = c->L;
    int *cand = xcalloc((size_t)n + 1, sizeof(int));
    int nc = 0;
    for (int a = 0; a < n; a++) {
        if (L[a].kind != LK_LABEL || L[a].local || !L[a].plain || !strchr(L[a].plain, ':')) continue;
        int cnt = 0, ok = 0;
        for (int k = a + 1; k < n; k++) {
            if (L[k].kind == LK_BLANK) continue;
            if (L[k].kind != LK_INSN) break;
            if (!strcmp(L[k].mn, "ret") && L[k].nops == 0) { ok = cnt > 0; break; }
            if (!in_list(L[k].mn, safe) || ++cnt > 3) break;
            int bad = 0;
            for (int o = 0; o < L[k].nops; o++) if (unsafe_operand(L[k].ops[o])) bad = 1;
            if (bad) break;
        }
        if (ok) cand[nc++] = a;
    }
    int did = 0;
    char **repl_text = xcalloc((size_t)n + 1, sizeof(char *));
    for (int i = 0; i < n; i++) {
        char t[128];
        if (!(L[i].kind == LK_INSN && !strcmp(L[i].mn, "call") && direct_target(&L[i], t, sizeof t))) continue;
        for (int ci = 0; ci < nc; ci++) {
            int a = cand[ci];
            if (strcmp(L[a].name, t) || i == a) continue;
            Str s = {0};
            int first = 1;
            for (int k = a + 1; k < n; k++) {
                if (L[k].kind == LK_BLANK) continue;
                if (!strcmp(L[k].mn, "ret")) break;
                if (!first) sadc(&s, '\n');
                first = 0;
                sadd(&s, "    ");
                sadd(&s, L[k].plain);
            }
            repl_text[i] = sget(&s);
            did = 1;
            break;
        }
    }
    free(cand);
    if (did) {
        for (int i = 0; i < n; i++) {
            if (!repl_text[i]) { lv_add(out_lv, c->lv->v[i].text, c->lv->v[i].file, c->lv->v[i].line); continue; }
            char *p = repl_text[i];
            for (;;) {
                char *nl = strchr(p, '\n');
                if (nl) *nl = 0;
                lv_add(out_lv, p, c->lv->v[i].file, c->lv->v[i].line);
                if (!nl) break;
                p = nl + 1;
            }
        }
    }
    for (int i = 0; i < n; i++) free(repl_text[i]);
    free(repl_text);
    return did;
}

static int name_is_entry(const char *s)
{
    return !strcasecmp(s, "start") || !strcasecmp(s, "_start") || !strcasecmp(s, "..start") || !strcasecmp(s, "main");
}

static int mentions(Ctx *c, int from, int to, const char *name)
{
    size_t nl = strlen(name);
    for (int i = 0; i < c->n; i++) {
        if (i >= from && i < to) continue;
        const char *p = c->L[i].plain;
        if (!p) continue;
        while (*p) {
            if (!idc((unsigned char)*p)) { p++; continue; }
            const char *q = p;
            while (idc((unsigned char)*q)) q++;
            size_t len = (size_t)(q - p);
            if (len == nl && !strncasecmp(p, name, nl)) return 1;
            if (len > nl && p[nl] == '.' && !strncasecmp(p, name, nl)) return 1;
            p = q;
        }
    }
    return 0;
}

static int dead_functions(Ctx *c)
{
    int changed = 0;
    int n = c->n;
    Ln *L = c->L;
    for (int a = 0; a < n; a++) {
        if (L[a].kind != LK_LABEL || L[a].local || name_is_entry(L[a].name)) continue;
        if (!L[a].plain || !strchr(L[a].plain, ':')) continue;
        int e = a + 1, last_insn = -1, bad = 0;
        for (; e < n; e++) {
            Ln *x = &L[e];
            if (x->kind == LK_BLANK) continue;
            if (x->kind == LK_LABEL && x->local) continue;
            if (x->kind == LK_INSN) { last_insn = e; continue; }
            if (x->kind == LK_LABEL || x->kind == LK_OTHER) break;
        }
        if (last_insn < 0 || !is_term(&L[last_insn])) continue;
        for (int k = a + 1; k < e; k++) if (L[k].kind == LK_OTHER) bad = 1;
        if (bad) continue;
        int p = a - 1;
        while (p >= 0 && (L[p].kind == LK_BLANK || (L[p].kind == LK_OTHER && other_passes(&L[p])))) p--;
        if (p >= 0) {
            if (L[p].kind == LK_LABEL) continue;
            if (L[p].kind == LK_OTHER) continue;
            if (L[p].kind == LK_INSN && !is_term(&L[p])) continue;
        }
        if (mentions(c, a, e, L[a].name)) continue;
        for (int k = a; k < e; k++) {
            set_text(c->lv, k, "");
            free_ln(&L[k]);
            memset(&L[k], 0, sizeof L[k]);
            L[k].kind = LK_BLANK;
        }
        changed = 1;
    }
    return changed;
}

static int should_skip(LineVec *lv, int *bits_out)
{
    int seen_bits = 0;
    for (int i = 0; i < lv->n; i++) {
        char *s = strip_comment(lv->v[i].text);
        char *p = s;
        if (*p == '[') p++;
        while (isspace((unsigned char)*p)) p++;
        char w[24];
        int k = 0;
        while (idc((unsigned char)*p) && k < 23) w[k++] = (char)tolower((unsigned char)*p++);
        w[k] = 0;
        if (!strcmp(w, "org") || !strcmp(w, "use16")) { free(s); return 1; }
        if (!strcmp(w, "bits") || !strcmp(w, "use32") || !strcmp(w, "use64")) {
            int b = !strcmp(w, "use32") ? 32 : !strcmp(w, "use64") ? 64 : atoi(p);
            if (b == 16 || (seen_bits && seen_bits != b)) { free(s); return 1; }
            seen_bits = b;
        }
        if (strstr(s, "$") && strstr(s, "$") != strstr(s, "$$")) {
            Ln t;
            parse_line(lv->v[i].text, &t);
            int bad = 0;
            if (t.kind == LK_INSN) for (int o = 0; o < t.nops; o++) if (strchr(t.ops[o], '$') && strcmp(t.ops[o], "$")) bad = 1;
            free_ln(&t);
            if (bad) { free(s); return 1; }
        }
        free(s);
    }
    (void)bits_out;
    return 0;
}

void opt_run(LineVec *lv, int bits, int kernel, int type)
{
    if (bits == 16) return;
    if (type == T_FLAT || type == T_COM || type == T_HEX || type == T_SREC || type == T_IMG) return;
    if (should_skip(lv, NULL)) return;
    insn_init();
    OB = bits;
    OK_KERNEL = kernel;
    int allow_dce = type != T_OBJ && type != T_COFF;
    for (int round = 0; round < 2; round++) {
        Ctx c;
        c.lv = lv;
        c.n = lv->n;
        c.L = xcalloc((size_t)c.n + 1, sizeof(Ln));
        c.scope = xcalloc((size_t)c.n + 1, sizeof(char *));
        reparse(&c);
        if (round == 0 && allow_dce) {
            LineVec nl = {0};
            if (inline_calls(&c, &nl)) {
                for (int i = 0; i < c.n; i++) { free_ln(&c.L[i]); free(c.scope[i]); }
                free(c.L);
                free(c.scope);
                for (int i = 0; i < lv->n; i++) free(lv->v[i].text);
                free(lv->v);
                *lv = nl;
                c.lv = lv;
                c.n = lv->n;
                c.L = xcalloc((size_t)c.n + 1, sizeof(Ln));
                c.scope = xcalloc((size_t)c.n + 1, sizeof(char *));
                reparse(&c);
            } else {
                for (int i = 0; i < nl.n; i++) free(nl.v[i].text);
                free(nl.v);
            }
        }
        for (int it = 0; it < 12; it++) {
            int ch = peephole(&c);
            if (round == 1 && allow_dce) ch |= dead_functions(&c);
            if (!ch) break;
            reparse(&c);
        }
        if (round == 0 && allow_dce) {
            if (dead_functions(&c)) reparse(&c);
        }
        for (int i = 0; i < c.n; i++) { free_ln(&c.L[i]); free(c.scope[i]); }
        free(c.L);
        free(c.scope);
    }
}
