#include "a64.h"

static HT g_tab;
static int g_tab_init;

static void tab_init(void)
{
    if (g_tab_init) return;
    g_tab_init = 1;
    const MnEnt *tabs[] = { a64_tab_int, a64_tab_mem, a64_tab_fp, a64_tab_ext, NULL };
    for (int t = 0; tabs[t]; t++)
        for (const MnEnt *e = tabs[t]; e->name; e++) ht_put(&g_tab, e->name, (void *)e);
}

static const char *const cond_names[] = { "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al", "nv", NULL };

int a64_p_cond(const char *s, int *cc)
{
    char b[4];
    size_t n = strlen(s);
    if (n != 2) return 0;
    b[0] = (char)tolower((unsigned char)s[0]);
    b[1] = (char)tolower((unsigned char)s[1]);
    b[2] = 0;
    if (!strcmp(b, "hs")) { *cc = 2; return 1; }
    if (!strcmp(b, "lo")) { *cc = 3; return 1; }
    for (int i = 0; cond_names[i]; i++) if (!strcmp(cond_names[i], b)) { *cc = i; return 1; }
    return 0;
}

static int cond_branch_name(const char *mn, int *cc)
{
    if (mn[0] != 'b') return 0;
    const char *q = mn + 1;
    if (*q == '.') q++;
    return a64_p_cond(q, cc);
}

static int h_bcond(Ctx *c, const MnEnt *e);

static const MnEnt bcond_ent = { "b.cond", h_bcond, 0, 0, 0 };

const MnEnt *a64_atomic_lookup(const char *mn);

const MnEnt *a64_lookup(const char *mn)
{
    tab_init();
    const MnEnt *e = ht_get(&g_tab, mn);
    if (e) return e;
    int cc;
    if (cond_branch_name(mn, &cc)) return &bcond_ent;
    return a64_atomic_lookup(mn);
}

static HT g_vtab;
static int g_vtab_init;

static const MnEnt *vlookup(const char *mn)
{
    if (!g_vtab_init) {
        g_vtab_init = 1;
        for (const MnEnt *e = a64_tab_simd; e->name; e++) ht_put(&g_vtab, e->name, (void *)e);
    }
    return ht_get(&g_vtab, mn);
}

const MnEnt *a64_vlookup(const char *mn)
{
    return vlookup(mn);
}

static int sc_first(const char *s)
{
    size_t n = strlen(s);
    if (n < 2 || n > 3) return 0;
    char ch = (char)tolower((unsigned char)s[0]);
    if (ch != 'b' && ch != 'h' && ch != 's' && ch != 'd' && ch != 'q') return 0;
    for (size_t i = 1; i < n; i++) if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int has_vname(const char *s)
{
    while (*s == ' ' || *s == '{') s++;
    if ((s[0] == 'v' || s[0] == 'V') && isdigit((unsigned char)s[1])) return 1;
    return 0;
}

int a64_known(const char *mn)
{
    char b[40];
    size_t i = 0;
    for (; mn[i] && i < sizeof b - 1; i++) b[i] = (char)tolower((unsigned char)mn[i]);
    b[i] = 0;
    if (mn[i]) return 0;
    return a64_lookup(b) != NULL || vlookup(b) != NULL;
}

void a64_trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    size_t k = 0;
    while (s[k] == ' ' || s[k] == '\t') k++;
    if (k) memmove(s, s + k, strlen(s + k) + 1);
}

int a64_p_gpr(const char *s, GReg *r)
{
    char b[8];
    size_t n = strlen(s);
    if (n < 2 || n > 4) return 0;
    for (size_t i = 0; i <= n; i++) b[i] = (char)tolower((unsigned char)s[i]);
    memset(r, 0, sizeof *r);
    if (!strcmp(b, "sp")) { r->n = 31; r->w64 = 1; r->sp = 1; return 1; }
    if (!strcmp(b, "wsp")) { r->n = 31; r->sp = 1; return 1; }
    if (!strcmp(b, "xzr")) { r->n = 31; r->w64 = 1; r->zr = 1; return 1; }
    if (!strcmp(b, "wzr")) { r->n = 31; r->zr = 1; return 1; }
    if (!strcmp(b, "lr")) { r->n = 30; r->w64 = 1; return 1; }
    if (!strcmp(b, "fp")) { r->n = 29; r->w64 = 1; return 1; }
    if (!strcmp(b, "ip0")) { r->n = 16; r->w64 = 1; return 1; }
    if (!strcmp(b, "ip1")) { r->n = 17; r->w64 = 1; return 1; }
    if (b[0] != 'x' && b[0] != 'w') return 0;
    if (!isdigit((unsigned char)b[1])) return 0;
    int v = b[1] - '0';
    if (n >= 3) {
        if (!isdigit((unsigned char)b[2])) return 0;
        if (b[1] == '0') return 0;
        v = v * 10 + b[2] - '0';
        if (n == 4) return 0;
    }
    if (v > 30) return 0;
    r->n = v;
    r->w64 = b[0] == 'x';
    return 1;
}

int a64_p_vec(const char *s, VReg *v)
{
    memset(v, 0, sizeof *v);
    v->lane = -1;
    char b[40];
    size_t n = strlen(s);
    if (n < 2 || n >= sizeof b) return 0;
    for (size_t i = 0; i <= n; i++) b[i] = (char)tolower((unsigned char)s[i]);
    char k = b[0];
    if (k != 'v' && k != 'b' && k != 'h' && k != 's' && k != 'd' && k != 'q') return 0;
    const char *p = b + 1;
    if (!isdigit((unsigned char)*p)) return 0;
    int num = 0, nd = 0;
    while (isdigit((unsigned char)*p)) { num = num * 10 + (*p - '0'); p++; nd++; }
    if (nd > 2 || num > 31) return 0;
    if (nd == 2 && b[1] == '0') return 0;
    v->n = num;
    if (k != 'v') {
        v->scalar = 1;
        v->sk = k;
        v->es = k == 'b' ? 8 : k == 'h' ? 16 : k == 's' ? 32 : k == 'd' ? 64 : 128;
        if (*p == '[') {
            return 0;
        }
        return *p == 0;
    }
    v->sk = 'v';
    if (*p == '.') {
        p++;
        int cnt = 0, hascnt = 0;
        while (isdigit((unsigned char)*p)) { cnt = cnt * 10 + (*p - '0'); p++; hascnt = 1; }
        char e = *p;
        if (e != 'b' && e != 'h' && e != 's' && e != 'd' && e != 'q') return 0;
        p++;
        int es = e == 'b' ? 8 : e == 'h' ? 16 : e == 's' ? 32 : e == 'd' ? 64 : 128;
        v->es = es;
        if (hascnt) {
            int ok = 0;
            if (es == 8 && (cnt == 8 || cnt == 16 || cnt == 4)) ok = 1;
            if (es == 16 && (cnt == 4 || cnt == 8 || cnt == 2)) ok = 1;
            if (es == 32 && (cnt == 2 || cnt == 4 || cnt == 1)) ok = 1;
            if (es == 64 && (cnt == 1 || cnt == 2)) ok = 1;
            if (es == 128 && cnt == 1) ok = 1;
            if (!ok) return 0;
            v->lanes = cnt;
            v->has_arr = 1;
        }
        if (*p == '[') {
            p++;
            if (!isdigit((unsigned char)*p)) return 0;
            int ln = 0;
            while (isdigit((unsigned char)*p)) { ln = ln * 10 + (*p - '0'); p++; }
            if (*p != ']') return 0;
            p++;
            v->lane = ln;
        }
        return *p == 0;
    }
    if (*p == '[') {
        p++;
        return 0;
    }
    return *p == 0;
}

const char *a64_strip_hash(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') s++;
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int a64_eval(Ctx *c, const char *s, Val *v)
{
    (void)c;
    s = a64_strip_hash(s);
    if (!*s) { err("%s: missing operand", c->mn); return 0; }
    return expr_eval_str(s, v, 0);
}

int a64_const(Ctx *c, const char *s, i64 *out, int *unk)
{
    Val v;
    if (!a64_eval(c, s, &v)) return 0;
    if (unk) *unk = 0;
    if (v.unk) {
        if (unk) *unk = 1;
        *out = 0;
        return 1;
    }
    if (!val_isconst(&v)) {
        err("%s: constant expression expected here", c->mn);
        return 0;
    }
    *out = v.n;
    return 1;
}

void a64_W(Ctx *c, u32 w)
{
    badle(&c->out->code, w, 4);
}

i64 a64_cur_off(Ctx *c)
{
    return c->ic->start_off + (i64)c->out->code.n;
}

void a64_reloc(Ctx *c, int kind, int size, const Val *v, i64 off)
{
    InsnOut *o = c->out;
    if (o->nrel >= o->caprel) {
        o->caprel = o->caprel ? o->caprel * 2 : 8;
        o->relocs = xrealloc(o->relocs, sizeof(Reloc) * (size_t)o->caprel);
    }
    Reloc *r = &o->relocs[o->nrel++];
    memset(r, 0, sizeof *r);
    r->off = off;
    r->size = size;
    r->kind = kind;
    r->v = *v;
}

int a64_same_sec(Ctx *c, const Val *v)
{
    return !v->unk && v->pos == c->ic->cur_sec && v->neg < 0 && !v->ext && !v->hasreg;
}

void a64_pad(Ctx *c)
{
    while ((int)c->out->code.n < c->floor) a64_W(c, 0xd503201f);
    if ((int)c->out->code.n > 4 && (int)c->out->code.n > c->ic->wide) c->ic->need_wide = (int)c->out->code.n;
}

int a64_need_n(Ctx *c, int count)
{
    int bytes = count * 4;
    if (bytes < c->floor) bytes = c->floor;
    return bytes / 4;
}

int a64_logical_imm(u64 imm, int is64, u32 *out)
{
    if (!is64) {
        imm &= 0xffffffffULL;
        imm |= imm << 32;
    }
    if (imm == 0 || imm == ~0ULL) return 0;
    int size;
    for (size = 2; size <= 64; size <<= 1) {
        u64 mask = size == 64 ? ~0ULL : ((1ULL << size) - 1);
        u64 e = imm & mask;
        u64 t = 0;
        for (int i = 0; i < 64; i += size) t |= e << i;
        if (t == imm) break;
    }
    u64 mask = size == 64 ? ~0ULL : ((1ULL << size) - 1);
    u64 e = imm & mask;
    int ones = __builtin_popcountll(e);
    for (int r = 0; r < size; r++) {
        u64 rot = r == 0 ? e : (((e >> r) | (e << (size - r))) & mask);
        if (rot == ((ones == 64) ? ~0ULL : ((1ULL << ones) - 1))) {
            int immr = (size - r) % size;
            int imms = (ones - 1) | ((~(size * 2 - 1)) & 0x3f);
            int N = size == 64;
            *out = ((u32)N << 12) | ((u32)immr << 6) | (u32)(imms & 0x3f);
            return 1;
        }
    }
    return 0;
}

static int range_bits(i64 v, int bits, int sign)
{
    if (sign) return v >= -((i64)1 << (bits - 1)) && v < ((i64)1 << (bits - 1));
    return v >= 0 && v < ((i64)1 << bits);
}

int a64_enc_branch(Ctx *c, u32 base, const Val *v, int kind, int bits, int sub, int shift_field)
{
    i64 at = a64_cur_off(c);
    if (v->unk) {
        a64_W(c, base);
        return 1;
    }
    if (a64_same_sec(c, v)) {
        i64 d = v->n - at;
        if (d & 3) { err("%s: branch target is not 4-byte aligned", c->mn); return 0; }
        i64 w = d >> 2;
        if (!range_bits(w, bits, 1)) {
            err("%s: branch target out of range (limit is %s)", c->mn, bits == 26 ? "128 MB" : bits == 19 ? "1 MB" : "32 KB");
            return 0;
        }
        u32 field = (u32)w & (u32)((1u << bits) - 1);
        a64_W(c, base | (field << shift_field));
        return 1;
    }
    a64_reloc(c, kind, sub, v, (i64)c->out->code.n);
    a64_W(c, base);
    return 1;
}

static int branch_fits(Ctx *c, const Val *v, int bits)
{
    if (!a64_same_sec(c, v)) return 1;
    i64 d = v->n - a64_cur_off(c);
    if (d & 3) return 1;
    return range_bits(d >> 2, bits, 1);
}

static int movw(Ctx *c, int sf, int opc, int rd, u32 imm16, int hw)
{
    (void)c;
    a64_W(c, ((u32)sf << 31) | ((u32)opc << 29) | 0x12800000u | ((u32)hw << 21) | (imm16 << 5) | (u32)rd);
    return 1;
}

int a64_mov_imm(Ctx *c, GReg rd, i64 val, int unk)
{
    int sf = rd.w64;
    u64 v = (u64)val;
    if (unk) { v = 0; }
    if (!sf) {
        if (val < -2147483648LL || val > 4294967295LL) {
            err("%s: immediate %lld does not fit in a 32-bit register", c->mn, (long long)val);
            return 0;
        }
        v &= 0xffffffffULL;
    }
    int nch = sf ? 4 : 2;
    u64 full = sf ? ~0ULL : 0xffffffffULL;
    u64 inv = ~v & full;
    int nz = 0, nzi = 0, fz = -1, fzi = -1;
    for (int i = 0; i < nch; i++) {
        if ((v >> (16 * i)) & 0xffff) { nz++; if (fz < 0) fz = i; }
        if ((inv >> (16 * i)) & 0xffff) { nzi++; if (fzi < 0) fzi = i; }
    }
    if (nz <= 1) {
        int hw = fz < 0 ? 0 : fz;
        return movw(c, sf, 2, rd.n, (u32)((v >> (16 * hw)) & 0xffff), hw);
    }
    if (nzi <= 1) {
        int hw = fzi < 0 ? 0 : fzi;
        return movw(c, sf, 0, rd.n, (u32)((inv >> (16 * hw)) & 0xffff), hw);
    }
    u32 enc;
    if (a64_logical_imm(v, sf, &enc)) {
        a64_W(c, ((u32)sf << 31) | 0x320003e0u | (enc << 10) | (u32)rd.n);
        return 1;
    }
    if (nzi < nz) {
        int k = -1;
        for (int i = 0; i < nch; i++) if (((v >> (16 * i)) & 0xffff) != 0xffff) { k = i; break; }
        movw(c, sf, 0, rd.n, (u32)((inv >> (16 * k)) & 0xffff), k);
        for (int i = 0; i < nch; i++) {
            if (i == k) continue;
            u32 ch = (u32)((v >> (16 * i)) & 0xffff);
            if (ch != 0xffff) movw(c, sf, 3, rd.n, ch, i);
        }
        return 1;
    }
    movw(c, sf, 2, rd.n, (u32)((v >> (16 * fz)) & 0xffff), fz);
    for (int i = fz + 1; i < nch; i++) {
        u32 ch = (u32)((v >> (16 * i)) & 0xffff);
        if (ch) movw(c, sf, 3, rd.n, ch, i);
    }
    return 1;
}

int a64_load_addr(Ctx *c, int rd, const Val *v)
{
    if (c->optimize && c->floor < 8 && (v->unk || a64_same_sec(c, v))) {
        int near = 1;
        if (!v->unk) {
            i64 d = v->n - a64_cur_off(c);
            near = d >= -(1 << 20) && d < (1 << 20);
        }
        if (near) {
            if (v->unk) { a64_W(c, 0x10000000u | (u32)rd); return 1; }
            i64 d = v->n - a64_cur_off(c);
            u32 immlo = (u32)d & 3, immhi = ((u32)d >> 2) & 0x7ffff;
            a64_W(c, 0x10000000u | (immlo << 29) | (immhi << 5) | (u32)rd);
            return 1;
        }
        c->ic->need_wide = 8;
    }
    a64_reloc(c, RK_A64_ADRP, 0, v, (i64)c->out->code.n);
    a64_W(c, 0x90000000u | (u32)rd);
    a64_reloc(c, RK_A64_LO12, 0, v, (i64)c->out->code.n);
    a64_W(c, 0x91000000u | ((u32)rd << 5) | (u32)rd);
    return 1;
}

static int h_bcond(Ctx *c, const MnEnt *e)
{
    (void)e;
    int cc;
    cond_branch_name(c->mn, &cc);
    if (c->n != 1) { err("%s: expected a label", c->mn); return 0; }
    Val v;
    if (!a64_eval(c, c->tok[0], &v)) return 0;
    if (c->floor >= 8 || !branch_fits(c, &v, 19)) {
        if (c->floor < 8) c->ic->need_wide = 8;
        a64_W(c, 0x54000000u | (2u << 5) | (u32)(cc ^ 1));
        return a64_enc_branch(c, 0x14000000u, &v, RK_A64_B26, 26, 0, 0);
    }
    return a64_enc_branch(c, 0x54000000u | (u32)cc, &v, RK_A64_B19, 19, 0, 5);
}

static int is_shkw(const char *p, size_t n)
{
    static const char *const k[] = { "lsl", "lsr", "asr", "ror", "msl", "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx", NULL };
    for (int i = 0; k[i]; i++) if (strlen(k[i]) == n && !strncasecmp(k[i], p, n)) return 1;
    return 0;
}

char *a64_split_shift(char *buf)
{
    char *ws = buf;
    while (*ws && *ws != ' ' && *ws != '\t') ws++;
    if (!*ws || buf[0] == '[' || buf[0] == '{') return NULL;
    if (is_shkw(buf, (size_t)(ws - buf))) return NULL;
    char *p = ws;
    while (*p == ' ' || *p == '\t') p++;
    size_t n = 0;
    while (isalpha((unsigned char)p[n])) n++;
    if (!n || !is_shkw(p, n)) return NULL;
    *ws = 0;
    return p;
}

static int tokenize(const char *rest, Ctx *c)
{
    char buf[512];
    size_t n = 0;
    int depth = 0;
    char q = 0;
    c->n = 0;
    const char *p = rest;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == ';') return 1;
    for (;; p++) {
        char ch = *p;
        int end = !ch || (!q && ch == ';');
        if (!end && q) {
            if (ch == q) q = 0;
        } else if (!end && (ch == '\'' || ch == '"')) q = ch;
        else if (!end && (ch == '[' || ch == '{' || ch == '(')) depth++;
        else if (!end && (ch == ']' || ch == '}' || ch == ')')) depth--;
        if (end || (!q && ch == ',' && depth == 0)) {
            buf[n] = 0;
            a64_trim(buf);
            if (c->n >= 12) { err("too many operands"); return 0; }
            if (!buf[0]) { err("empty operand"); return 0; }
            char *tl = a64_split_shift(buf);
            c->tok[c->n++] = xstrdup(buf);
            if (tl) {
                if (c->n >= 12) { err("too many operands"); return 0; }
                c->tok[c->n++] = xstrdup(tl);
            }
            n = 0;
            if (end) break;
            continue;
        }
        if (n + 1 >= sizeof buf) { err("operand too long"); return 0; }
        buf[n++] = ch;
    }
    return 1;
}

int a64_insn(const char *mn, const char *rest, IC *ic, InsnOut *out)
{
    Ctx c;
    memset(&c, 0, sizeof c);
    size_t i = 0;
    for (; mn[i] && i < sizeof c.mn - 1; i++) c.mn[i] = (char)tolower((unsigned char)mn[i]);
    c.mn[i] = 0;
    c.ic = ic;
    c.out = out;
    c.floor = ic->wide;
    c.optimize = ic->optimize;
    int ok = 0;
    if (!tokenize(rest, &c)) goto done;
    const MnEnt *e = NULL;
    const MnEnt *ve = vlookup(c.mn);
    int anyv = 0;
    for (int k = 0; k < c.n; k++) if (has_vname(c.tok[k]) || (k == 0 && sc_first(c.tok[k]))) anyv = 1;
    int tried = 0;
    if (anyv && ve) {
        tried = 1;
        ok = ve->fn(&c, ve);
        if (ok >= 0) goto fin;
    }
    e = a64_lookup(c.mn);
    if (e) {
        ok = e->fn(&c, e);
        if (ok >= 0) goto fin;
    }
    if (ve && !tried) {
        ok = ve->fn(&c, ve);
        if (ok >= 0) goto fin;
    }
    if (!e && !ve) err("unknown ARM64 instruction `%s'", mn);
    else err("%s: unsupported operands", mn);
    ok = 0;
fin:
    if (ok) a64_pad(&c);
done:
    for (int k = 0; k < c.n; k++) free(c.tok[k]);
    return ok;
}
