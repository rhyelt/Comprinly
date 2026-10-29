/* ARM64 loads and stores: addressing modes, pairs, exclusive and acquire/release forms, atomics and prefetch. */
#include "a64.h"

/* Checks the operand count and reports a count error if it is outside lo..hi. */
static int usage(Ctx *c, int lo, int hi)
{
    if (lo == hi) err("%s: expected %d operand%s, found %d", c->mn, lo, lo == 1 ? "" : "s", c->n);
    else err("%s: expected %d to %d operands, found %d", c->mn, lo, hi, c->n);
    return 0;
}

#define NEED(lo, hi) do { if (c->n < (lo) || c->n > (hi)) return usage(c, (lo), (hi)); } while (0)

/* Skips blanks. */
static const char *skip(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Splits at top-level commas. */
static int split_top(char *s, char **parts, int max)
{
    int n = 0, depth = 0;
    char *start = s;
    for (char *p = s;; p++) {
        if (*p == '(' || *p == '[') depth++;
        else if (*p == ')' || *p == ']') depth--;
        if (*p == 0 || (*p == ',' && depth == 0)) {
            if (n >= max) return -1;
            char save = *p;
            *p = 0;
            parts[n++] = start;
            start = p + 1;
            if (!save) break;
        }
    }
    for (int i = 0; i < n; i++) a64_trim(parts[i]);
    return n;
}

/* Parses an extend or shift name with amount inside a memory operand. */
static int ext_name(const char *s, int *kind, int *code, i64 *amt, int *has, Ctx *c)
{
    static const struct { const char *n; int kind; int code; } t[] = {
        { "lsl", 1, 3 }, { "uxtw", 2, 2 }, { "sxtw", 2, 6 }, { "sxtx", 2, 7 }, { "uxtx", 2, 3 }, { "uxtb", 2, 0 }, { "uxth", 2, 1 }, { "sxtb", 2, 4 }, { "sxth", 2, 5 }, { NULL, 0, 0 }
    };
    char w[8];
    size_t n = 0;
    while (isalpha((unsigned char)s[n]) && n < sizeof w - 1) { w[n] = (char)tolower((unsigned char)s[n]); n++; }
    w[n] = 0;
    if (isalpha((unsigned char)s[n])) return 0;
    for (int i = 0; t[i].n; i++) {
        if (strcmp(t[i].n, w)) continue;
        *kind = t[i].kind;
        *code = t[i].code;
        const char *r = skip(s + n);
        *has = 0;
        *amt = 0;
        if (*r) {
            int unk;
            if (!a64_const(c, r, amt, &unk)) return -1;
            *has = 1;
        }
        return 1;
    }
    return 0;
}


/* Parses [base], [base, #imm], [base, Xm, ext], pre-index and post-index forms, and :lo12: symbol offsets. */
int a64_parse_mem(Ctx *c, const char *s, AMem *m)
{
    memset(m, 0, sizeof *m);
    m->ext = -1;
    s = skip(s);
    if (*s != '[') { err("%s: memory operand expected, found `%s'", c->mn, s); return 0; }
    const char *close = NULL;
    int depth = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '[') depth++;
        else if (*p == ']') { depth--; if (!depth) { close = p; break; } }
    }
    if (!close) { err("%s: missing `]' in `%s'", c->mn, s); return 0; }
    const char *after = skip(close + 1);
    if (*after == '!') { m->mode = 1; after = skip(after + 1); }
    if (*after) { err("%s: unexpected text after `]': `%s'", c->mn, after); return 0; }
    char inner[200];
    size_t n = (size_t)(close - s - 1);
    if (n >= sizeof inner) { err("%s: memory operand too long", c->mn); return 0; }
    memcpy(inner, s + 1, n);
    inner[n] = 0;
    a64_trim(inner);
    if (!inner[0]) { err("%s: empty memory operand", c->mn); return 0; }
    char first[40];
    size_t k = 0;
    const char *p = inner;
    while (*p && *p != ',' && *p != '+' && *p != '-' && *p != ' ' && *p != '\t' && k < sizeof first - 1) first[k++] = *p++;
    first[k] = 0;
    GReg b;
    if (!a64_p_gpr(first, &b)) {
        Val v;
        if (!expr_eval_str(inner, &v, 0)) return 0;
        m->symbolic = 1;
        m->kind = 3;
        m->imm = v;
        return 1;
    }
    if (!b.w64 || b.zr) { err("%s: the base register must be an x register or sp, found `%s'", c->mn, first); return 0; }
    m->base = b.n;
    m->base_sp = b.sp;
    p = skip(p);
    if (!*p) { m->kind = 0; return 1; }
    if (*p == ',') {
        char rest[160];
        snprintf(rest, sizeof rest, "%s", p + 1);
        char *parts[4];
        int np = split_top(rest, parts, 4);
        if (np < 1) { err("%s: bad memory operand `%s'", c->mn, s); return 0; }
        if (np == 1) {
            char *tl = a64_split_shift(parts[0]);
            if (tl) { parts[1] = tl; np = 2; }
        }
        GReg ix;
        if (a64_p_gpr(parts[0], &ix)) {
            if (ix.sp) { err("%s: sp cannot be an index register", c->mn); return 0; }
            m->kind = 2;
            m->idx = ix.n;
            m->idxw64 = ix.w64;
            m->idxzr = ix.zr;
            m->ext = ix.w64 ? 3 : 2;
            if (np == 2) {
                int kind, code, has;
                i64 amt;
                int r = ext_name(parts[1], &kind, &code, &amt, &has, c);
                if (r < 0) return 0;
                if (r == 0) { err("%s: extend or shift expected, found `%s'", c->mn, parts[1]); return 0; }
                m->ext = code;
                m->hassh = has;
                m->shamt = (int)amt;
                if (kind == 1 && !has) { err("%s: lsl needs a shift amount here", c->mn); return 0; }
                if (has && (amt < 0 || amt > 4)) { err("%s: shift amount out of range", c->mn); return 0; }
            } else if (np > 2) { err("%s: too many fields in memory operand", c->mn); return 0; }
            return 1;
        }
        if (np != 1) { err("%s: too many fields in memory operand", c->mn); return 0; }
        const char *t = a64_strip_hash(parts[0]);
        m->kind = 1;
        if (t[0] == ':') {
            const char *e = strchr(t + 1, ':');
            if (!e) { err("%s: bad relocation modifier", c->mn); return 0; }
            char mod[16];
            size_t ml = (size_t)(e - t - 1);
            if (ml >= sizeof mod) { err("%s: unknown relocation modifier", c->mn); return 0; }
            memcpy(mod, t + 1, ml);
            mod[ml] = 0;
            int gotm = !strcasecmp(mod, "got_lo12");
            if (!gotm && strcasecmp(mod, "lo12") && strcasecmp(mod, "lo12_nc")) { err("%s: modifier `:%s:' is not supported here (use :lo12: or :got_lo12:)", c->mn, mod); return 0; }
            m->hasmod = 1;
            if (!expr_eval_str(e + 1, &m->imm, 0)) return 0;
            if (gotm) {
                if (!m->imm.ext && m->imm.pos < 0) { err("%s: :got_lo12: needs a symbol", c->mn); return 0; }
                m->imm.wk = 7;
            }
            return 1;
        }
        if (!expr_eval_str(t, &m->imm, 0)) return 0;
        return 1;
    }
    if (*p == '+' || *p == '-') {
        int neg = *p == '-';
        p = skip(p + 1);
        char w[40];
        size_t wl = 0;
        const char *q = p;
        while ((isalnum((unsigned char)*q) || *q == '_') && wl < sizeof w - 1) w[wl++] = *q++;
        w[wl] = 0;
        GReg ix;
        if (wl && !neg && a64_p_gpr(w, &ix)) {
            m->kind = 2;
            m->idx = ix.n;
            m->idxw64 = ix.w64;
            m->idxzr = ix.zr;
            m->ext = ix.w64 ? 3 : 2;
            q = skip(q);
            if (*q == '*' || (q[0] == '<' && q[1] == '<')) {
                int sh = 0;
                if (*q == '*') {
                    q = skip(q + 1);
                    i64 sc = strtoll(q, NULL, 0);
                    if (sc == 1) sh = 0;
                    else if (sc == 2) sh = 1;
                    else if (sc == 4) sh = 2;
                    else if (sc == 8) sh = 3;
                    else if (sc == 16) sh = 4;
                    else { err("%s: scale must be 1, 2, 4, 8 or 16", c->mn); return 0; }
                } else {
                    q = skip(q + 2);
                    sh = (int)strtoll(q, NULL, 0);
                }
                m->hassh = 1;
                m->shamt = sh;
            }
            return 1;
        }
        char *txt = xstrdup(p);
        Str full = {0};
        sadd(&full, neg ? "-(" : "(");
        sadd(&full, txt);
        sadd(&full, ")");
        free(txt);
        m->kind = 1;
        int ok = expr_eval_str(sget(&full), &m->imm, 0);
        free(full.s);
        return ok;
    }
    err("%s: bad memory operand `%s'", c->mn, s);
    return 0;
}

typedef struct { int sz; int fp; int opc_ld, opc_st; int w64; int n; int zr; } RtInfo;

/* Parses the transfer register and its size. */
static int rt_parse(Ctx *c, int i, RtInfo *r, int allow_fp)
{
    GReg g;
    memset(r, 0, sizeof *r);
    if (a64_p_gpr(c->tok[i], &g)) {
        if (g.sp) { err("%s: sp cannot be used as the transfer register", c->mn); return 0; }
        r->n = g.n;
        r->w64 = g.w64;
        r->zr = g.zr;
        return 1;
    }
    VReg v;
    if (allow_fp && a64_p_vec(c->tok[i], &v) && v.scalar) {
        r->fp = 1;
        r->n = v.n;
        r->sz = v.sk == 'b' ? 0 : v.sk == 'h' ? 1 : v.sk == 's' ? 2 : v.sk == 'd' ? 3 : 4;
        return 1;
    }
    err("%s: operand %d: register expected, found `%s'", c->mn, i + 1, c->tok[i]);
    return 0;
}

/* Emits a load or store with a :lo12: relocation. */
static int emit_lo12(Ctx *c, u32 base, int shift, const Val *v)
{
    if (v->wk == 7 && shift != 3) { err("%s: :got_lo12: works only with a 64-bit ldr", c->mn); return 0; }
    a64_reloc(c, RK_A64_LO12, shift, v, (i64)c->out->code.n);
    a64_W(c, base);
    return 1;
}

/* Reads the immediate of a post-index form. */
static int post_imm(Ctx *c, AMem *m, int post_idx, i64 *val)
{
    int unk;
    (void)m;
    return a64_const(c, c->tok[post_idx], val, &unk);
}

/* ldr, str, ldur, stur, byte, half and signed forms, FP and SIMD registers. */
static int h_ldst(Ctx *c, const MnEnt *e)
{
    NEED(2, 3);
    int load = e->a & 1;
    int unsc = (e->a >> 1) & 1;
    int fixed = e->b;
    int sgn = e->c;
    RtInfo rt;
    if (!rt_parse(c, 0, &rt, fixed < 0)) return 0;
    if (!load && sgn) { err("%s: signed stores do not exist", c->mn); return 0; }
    int size, opc, V = 0;
    if (rt.fp) {
        V = 1;
        size = rt.sz == 4 ? 0 : rt.sz;
        opc = rt.sz == 4 ? (load ? 3 : 2) : (load ? 1 : 0);
    } else if (fixed < 0) {
        size = rt.w64 ? 3 : 2;
        opc = load ? 1 : 0;
    } else {
        size = fixed;
        if (sgn) {
            if (fixed == 2) {
                if (!rt.w64) { err("%s: ldrsw needs an x register", c->mn); return 0; }
                opc = 2;
            } else opc = rt.w64 ? 2 : 3;
        } else {
            if (rt.w64) { err("%s: needs a w register", c->mn); return 0; }
            opc = load ? 1 : 0;
        }
    }
    const char *t1 = c->tok[1];
    if (t1[0] == '=') {
        if (!load || fixed >= 0 || rt.fp) { err("%s: `=' is only valid with ldr and a general register", c->mn); return 0; }
        GReg g = { rt.n, rt.w64, 0, rt.zr };
        extern int a64_ldr_eq(Ctx * c, GReg rd);
        return a64_ldr_eq(c, g);
    }
    if (t1[0] != '[') {
        if (!load || unsc || (fixed >= 0 && !(sgn && fixed == 2))) { err("%s: a memory operand in [brackets] is required", c->mn); return 0; }
        if (c->n != 2) return usage(c, 2, 2);
        Val v;
        if (!a64_eval(c, t1, &v)) return 0;
        u32 base;
        if (rt.fp) base = rt.sz == 2 ? 0x1c000000u : rt.sz == 3 ? 0x5c000000u : 0x9c000000u;
        else if (fixed == 2) base = 0x98000000u;
        else base = rt.w64 ? 0x58000000u : 0x18000000u;
        if (rt.fp && rt.sz < 2) { err("%s: literal loads need s, d or q registers", c->mn); return 0; }
        return a64_enc_branch(c, base | (u32)rt.n, &v, RK_A64_B19, 19, 1, 5);
    }
    AMem m;
    if (!a64_parse_mem(c, t1, &m)) return 0;
    if (c->n == 3) {
        if (m.mode) { err("%s: cannot combine pre-index and post-index", c->mn); return 0; }
        m.mode = 2;
    }
    u32 hd = ((u32)size << 30) | ((u32)V << 26) | ((u32)opc << 22);
    if (m.symbolic) {
        if (m.mode || unsc) { err("%s: a symbol cannot be used with writeback or ldur/stur", c->mn); return 0; }
        int scr = (load && !rt.fp && !rt.zr) ? rt.n : 16;
        int slg = (V && rt.sz == 4) ? 4 : size;
        if (c->floor < 8) c->ic->need_wide = 8;
        a64_reloc(c, RK_A64_ADRP, 0, &m.imm, (i64)c->out->code.n);
        a64_W(c, 0x90000000u | (u32)scr);
        return emit_lo12(c, 0x39000000u | hd | ((u32)scr << 5) | (u32)rt.n, slg, &m.imm);
    }
    if (m.kind == 2) {
        if (m.mode) { err("%s: a register offset cannot be combined with writeback", c->mn); return 0; }
        if (unsc) { err("%s: ldur/stur take an immediate offset only", c->mn); return 0; }
        int lg = (V && rt.sz == 4) ? 4 : size;
        int option = m.ext;
        if (!m.idxw64 && option != 2 && option != 6) { err("%s: a w index register needs uxtw or sxtw", c->mn); return 0; }
        if (m.idxw64 && option != 3 && option != 7) { err("%s: an x index register needs lsl or sxtx", c->mn); return 0; }
        if (m.hassh && m.shamt != 0 && m.shamt != lg) { err("%s: the shift amount must be 0 or %d for this access size", c->mn, lg); return 0; }
        int S = m.hassh && (m.shamt != 0 || lg == 0);
        if (m.hassh && m.shamt == 0 && lg == 0) S = 1;
        a64_W(c, 0x38200800u | hd | ((u32)m.idx << 16) | ((u32)option << 13) | ((u32)S << 12) | ((u32)m.base << 5) | (u32)rt.n);
        return 1;
    }
    int lg = (V && rt.sz == 4) ? 4 : size;
    if (m.mode) {
        i64 imm;
        if (m.mode == 2) { if (!post_imm(c, &m, 2, &imm)) return 0; }
        else {
            if (m.kind == 0) imm = 0;
            else { if (m.hasmod || !val_isconst(&m.imm)) { err("%s: the offset must be a constant here", c->mn); return 0; } imm = m.imm.n; }
        }
        if (m.kind == 0 && m.mode == 1) imm = 0;
        if (imm < -256 || imm > 255) { err("%s: writeback offset %lld is out of range (-256 to 255)", c->mn, (long long)imm); return 0; }
        a64_W(c, 0x38000000u | hd | ((u32)(imm & 0x1ff) << 12) | (m.mode == 1 ? 0xc00u : 0x400u) | ((u32)m.base << 5) | (u32)rt.n);
        return 1;
    }
    if (m.kind == 0) {
        a64_W(c, (unsc ? 0x38000000u : 0x39000000u) | hd | ((u32)m.base << 5) | (u32)rt.n);
        return 1;
    }
    if (m.hasmod) {
        if (unsc) { err("%s: :lo12: needs ldr or str", c->mn); return 0; }
        return emit_lo12(c, 0x39000000u | hd | ((u32)m.base << 5) | (u32)rt.n, lg, &m.imm);
    }
    if (!val_isconst(&m.imm) && !m.imm.unk) { err("%s: the offset must be a constant (use :lo12:symbol for an address part)", c->mn); return 0; }
    i64 imm = m.imm.unk ? 0 : m.imm.n;
    int scale = 1 << lg;
    if (!unsc && imm >= 0 && imm % scale == 0 && (imm / scale) < 4096) {
        a64_W(c, 0x39000000u | hd | ((u32)(imm / scale) << 10) | ((u32)m.base << 5) | (u32)rt.n);
        return 1;
    }
    if (imm >= -256 && imm <= 255) {
        a64_W(c, 0x38000000u | hd | ((u32)(imm & 0x1ff) << 12) | ((u32)m.base << 5) | (u32)rt.n);
        return 1;
    }
    if (unsc) err("%s: offset %lld is out of range (-256 to 255)", c->mn, (long long)imm);
    else if (imm >= 0 && imm % scale) err("%s: offset %lld must be a multiple of %d (or between -256 and 255)", c->mn, (long long)imm, scale);
    else err("%s: offset %lld is out of range (0 to %d, or -256 to 255)", c->mn, (long long)imm, 4095 * scale);
    return 0;
}

/* Unprivileged loads and stores (ldtr, sttr). */
static int h_ldtr(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    int load = e->a & 1;
    int fixed = e->b;
    int sgn = e->c;
    RtInfo rt;
    if (!rt_parse(c, 0, &rt, 0)) return 0;
    int size, opc;
    if (fixed < 0) { size = rt.w64 ? 3 : 2; opc = load ? 1 : 0; }
    else {
        size = fixed;
        if (sgn) {
            if (fixed == 2) { if (!rt.w64) { err("%s: needs an x register", c->mn); return 0; } opc = 2; }
            else opc = rt.w64 ? 2 : 3;
        } else {
            if (rt.w64) { err("%s: needs a w register", c->mn); return 0; }
            opc = load ? 1 : 0;
        }
    }
    AMem m;
    if (!a64_parse_mem(c, c->tok[1], &m)) return 0;
    if (m.mode || m.kind > 1 || m.hasmod) { err("%s: only [base] or [base, #imm] is allowed", c->mn); return 0; }
    i64 imm = 0;
    if (m.kind == 1) { if (!val_isconst(&m.imm)) { err("%s: the offset must be a constant", c->mn); return 0; } imm = m.imm.n; }
    if (imm < -256 || imm > 255) { err("%s: offset out of range (-256 to 255)", c->mn); return 0; }
    a64_W(c, 0x38000800u | ((u32)size << 30) | ((u32)opc << 22) | ((u32)(imm & 0x1ff) << 12) | ((u32)m.base << 5) | (u32)rt.n);
    return 1;
}

/* ldp, stp, ldnp, stnp, ldpsw. */
static int h_ldp(Ctx *c, const MnEnt *e)
{
    NEED(3, 4);
    int load = e->a & 1;
    int nt = (e->a >> 1) & 1;
    int sw = e->b;
    RtInfo r1, r2;
    if (!rt_parse(c, 0, &r1, 1) || !rt_parse(c, 1, &r2, 1)) return 0;
    if (r1.fp != r2.fp || (!r1.fp && r1.w64 != r2.w64) || (r1.fp && r1.sz != r2.sz)) { err("%s: the two registers must be the same size", c->mn); return 0; }
    int opc, V = r1.fp, lg;
    if (r1.fp) {
        if (r1.sz < 2) { err("%s: pairs need s, d or q registers", c->mn); return 0; }
        opc = r1.sz - 2;
        lg = r1.sz;
    } else if (sw) {
        if (!r1.w64) { err("%s: needs x registers", c->mn); return 0; }
        opc = 1;
        lg = 2;
    } else {
        opc = r1.w64 ? 2 : 0;
        lg = r1.w64 ? 3 : 2;
    }
    if (load && r1.n == r2.n && !r1.zr) { err("%s: the two destination registers must differ", c->mn); return 0; }
    AMem m;
    if (!a64_parse_mem(c, c->tok[2], &m)) return 0;
    if (c->n == 4) {
        if (m.mode) { err("%s: cannot combine pre-index and post-index", c->mn); return 0; }
        m.mode = 2;
    }
    if (m.kind == 2 || m.symbolic || m.hasmod) { err("%s: only [base{, #imm}] is allowed", c->mn); return 0; }
    if (nt && m.mode) { err("%s: no writeback form exists", c->mn); return 0; }
    i64 imm = 0;
    if (m.mode == 2) { int unk; if (!a64_const(c, c->tok[3], &imm, &unk)) return 0; }
    else if (m.kind == 1) { if (!val_isconst(&m.imm) && !m.imm.unk) { err("%s: the offset must be a constant", c->mn); return 0; } imm = m.imm.unk ? 0 : m.imm.n; }
    int scale = 1 << lg;
    if (imm % scale) { err("%s: offset %lld must be a multiple of %d", c->mn, (long long)imm, scale); return 0; }
    i64 q = imm / scale;
    if (q < -64 || q > 63) { err("%s: offset %lld is out of range (%d to %d)", c->mn, (long long)imm, -64 * scale, 63 * scale); return 0; }
    u32 base = nt ? 0x28000000u : (m.mode == 1 ? 0x29800000u : m.mode == 2 ? 0x28800000u : 0x29000000u);
    a64_W(c, base | ((u32)opc << 30) | ((u32)V << 26) | ((u32)load << 22) | ((u32)(q & 0x7f) << 15) | ((u32)r2.n << 10) | ((u32)m.base << 5) | (u32)r1.n);
    return 1;
}

/* Parses an operand that must be just [base]. */
static int base_only(Ctx *c, int idx, int *base)
{
    AMem m;
    if (!a64_parse_mem(c, c->tok[idx], &m)) return 0;
    if (m.mode || m.kind == 2 || m.symbolic || m.hasmod) { err("%s: only [base] is allowed", c->mn); return 0; }
    if (m.kind == 1) {
        if (!val_isconst(&m.imm) || m.imm.n != 0) { err("%s: only a zero offset is allowed", c->mn); return 0; }
    }
    *base = m.base;
    return 1;
}

/* Exclusive load and store (ldxr, stxr, ldaxr, stlxr and pair forms). */
static int h_excl(Ctx *c, const MnEnt *e)
{
    int kind = e->a;
    int pair = kind == 2 || kind == 3;
    int store = kind == 1 || kind == 3;
    int nreg = (store ? 1 : 0) + (pair ? 2 : 1);
    NEED(nreg + 1, nreg + 1);
    RtInfo rs, r1, r2;
    int ti = store ? 1 : 0;
    if (store && !rt_parse(c, 0, &rs, 0)) return 0;
    if (!rt_parse(c, ti, &r1, 0)) return 0;
    if (pair && !rt_parse(c, ti + 1, &r2, 0)) return 0;
    int size = e->c >= 0 ? e->c : (r1.w64 ? 3 : 2);
    if (e->c >= 0 && r1.w64) { err("%s: needs a w register", c->mn); return 0; }
    if (e->c < 0 && pair && r2.w64 != r1.w64) { err("%s: the two registers must be the same size", c->mn); return 0; }
    if (store && rs.w64) { err("%s: the status register must be a w register", c->mn); return 0; }
    int base;
    if (!base_only(c, nreg, &base)) return 0;
    int o0 = e->b & 1, o2 = (e->b >> 1) & 1, o1 = pair;
    u32 w = ((u32)size << 30) | 0x08000000u | ((u32)o2 << 23) | ((u32)(!store) << 22) | ((u32)o1 << 21) | ((u32)o0 << 15) | ((u32)base << 5) | (u32)r1.n;
    w |= (store ? (u32)rs.n : 31u) << 16;
    w |= (pair ? (u32)r2.n : 31u) << 10;
    a64_W(c, w);
    return 1;
}

/* ldar, stlr, ldapr and relatives. */
static int h_ldar(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    RtInfo rt;
    if (!rt_parse(c, 0, &rt, 0)) return 0;
    int size = e->c >= 0 ? e->c : (rt.w64 ? 3 : 2);
    if (e->c >= 0 && rt.w64) { err("%s: needs a w register", c->mn); return 0; }
    int base;
    if (!base_only(c, 1, &base)) return 0;
    a64_W(c, ((u32)size << 30) | (u32)e->a | ((u32)base << 5) | (u32)rt.n);
    return 1;
}

static int mk_atomic(const char *mn, MnEnt *out);

static int h_atomic(Ctx *c, const MnEnt *e);

/* Finds an atomic instruction (ldadd, swp, cas ... with size and ordering suffixes). */
const MnEnt *a64_atomic_lookup(const char *mn)
{
    static MnEnt dyn;
    if (!mk_atomic(mn, &dyn)) return NULL;
    return &dyn;
}

typedef struct { const char *n; int kind; int op; } AtBase;

static const AtBase atbases[] = {
    { "casp", 1, 0 }, { "cas", 0, 0 }, { "swp", 2, 0 },
    { "ldadd", 3, 0 }, { "ldclr", 3, 1 }, { "ldeor", 3, 2 }, { "ldset", 3, 3 }, { "ldsmax", 3, 4 }, { "ldsmin", 3, 5 }, { "ldumax", 3, 6 }, { "ldumin", 3, 7 },
    { "stadd", 4, 0 }, { "stclr", 4, 1 }, { "steor", 4, 2 }, { "stset", 4, 3 }, { "stsmax", 4, 4 }, { "stsmin", 4, 5 }, { "stumax", 4, 6 }, { "stumin", 4, 7 },
    { NULL, 0, 0 }
};

/* Builds a table entry for an atomic mnemonic from its base, size and ordering suffix. */
static int mk_atomic(const char *mn, MnEnt *out)
{
    for (int i = 0; atbases[i].n; i++) {
        size_t bl = strlen(atbases[i].n);
        if (strncmp(mn, atbases[i].n, bl)) continue;
        const char *s = mn + bl;
        int A = 0, L = 0, sz = -1;
        if (atbases[i].kind != 4 && *s == 'a') { A = 1; s++; }
        if (*s == 'l') { L = 1; s++; }
        if (atbases[i].kind != 1 && (*s == 'b' || *s == 'h')) { sz = *s == 'b' ? 0 : 1; s++; }
        if (*s) continue;
        out->name = mn;
        out->fn = h_atomic;
        out->a = atbases[i].kind | (atbases[i].op << 4);
        out->b = A | (L << 1);
        out->c = sz;
        return 1;
    }
    return 0;
}

/* Atomic memory operations. */
static int h_atomic(Ctx *c, const MnEnt *e)
{
    int kind = e->a & 15, op = e->a >> 4;
    int A = e->b & 1, L = (e->b >> 1) & 1;
    int fsz = e->c;
    RtInfo rs, rt, rs2, rt2;
    int base;
    if (kind == 1) {
        NEED(5, 5);
        if (!rt_parse(c, 0, &rs, 0) || !rt_parse(c, 1, &rs2, 0) || !rt_parse(c, 2, &rt, 0) || !rt_parse(c, 3, &rt2, 0)) return 0;
        if (rs.w64 != rt.w64 || rs.w64 != rs2.w64 || rs.w64 != rt2.w64) { err("%s: all registers must be the same size", c->mn); return 0; }
        if ((rs.n & 1) || (rt.n & 1) || rs2.n != rs.n + 1 || rt2.n != rt.n + 1) { err("%s: registers must be consecutive pairs starting at an even register", c->mn); return 0; }
        if (!base_only(c, 4, &base)) return 0;
        a64_W(c, 0x08207c00u | ((u32)rs.w64 << 30) | ((u32)A << 22) | ((u32)L << 15) | ((u32)rs.n << 16) | ((u32)base << 5) | (u32)rt.n);
        return 1;
    }
    if (kind == 4) {
        NEED(2, 2);
        if (!rt_parse(c, 0, &rs, 0)) return 0;
        rt.n = 31;
        rt.w64 = rs.w64;
        if (!base_only(c, 1, &base)) return 0;
    } else {
        NEED(3, 3);
        if (!rt_parse(c, 0, &rs, 0) || !rt_parse(c, 1, &rt, 0)) return 0;
        if (rs.w64 != rt.w64) { err("%s: the two registers must be the same size", c->mn); return 0; }
        if (!base_only(c, 2, &base)) return 0;
    }
    if (fsz >= 0 && rs.w64) { err("%s: needs w registers", c->mn); return 0; }
    int size = fsz >= 0 ? fsz : (rs.w64 ? 3 : 2);
    if (kind == 0) {
        a64_W(c, 0x08a07c00u | ((u32)size << 30) | ((u32)A << 22) | ((u32)L << 15) | ((u32)rs.n << 16) | ((u32)base << 5) | (u32)rt.n);
        return 1;
    }
    u32 opc = kind == 2 ? 0x8000u : ((u32)op << 12);
    a64_W(c, 0x38200000u | ((u32)size << 30) | ((u32)A << 23) | ((u32)L << 22) | ((u32)rs.n << 16) | opc | ((u32)base << 5) | (u32)rt.n);
    return 1;
}

static const struct { const char *n; int v; } prfops[] = {
    { "pldl1keep", 0 }, { "pldl1strm", 1 }, { "pldl2keep", 2 }, { "pldl2strm", 3 }, { "pldl3keep", 4 }, { "pldl3strm", 5 },
    { "plil1keep", 8 }, { "plil1strm", 9 }, { "plil2keep", 10 }, { "plil2strm", 11 }, { "plil3keep", 12 }, { "plil3strm", 13 },
    { "pstl1keep", 16 }, { "pstl1strm", 17 }, { "pstl2keep", 18 }, { "pstl2strm", 19 }, { "pstl3keep", 20 }, { "pstl3strm", 21 }, { NULL, 0 }
};

/* prfm and prfum. */
static int h_prfm(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    int op = -1;
    for (int i = 0; prfops[i].n; i++) if (!strcasecmp(c->tok[0], prfops[i].n)) op = prfops[i].v;
    if (op < 0) {
        i64 v;
        int unk;
        if (!a64_const(c, c->tok[0], &v, &unk)) return 0;
        if (v < 0 || v > 31) { err("%s: prefetch operation out of range (0 to 31)", c->mn); return 0; }
        op = (int)v;
    }
    if (c->tok[1][0] != '[') {
        Val v;
        if (!a64_eval(c, c->tok[1], &v)) return 0;
        return a64_enc_branch(c, 0xd8000000u | (u32)op, &v, RK_A64_B19, 19, 1, 5);
    }
    AMem m;
    if (!a64_parse_mem(c, c->tok[1], &m)) return 0;
    if (m.mode || m.symbolic) { err("%s: no writeback or symbolic form exists", c->mn); return 0; }
    if (m.kind == 2) {
        int option = m.ext;
        if (!m.idxw64 && option != 2 && option != 6) { err("%s: a w index register needs uxtw or sxtw", c->mn); return 0; }
        if (m.idxw64 && option != 3 && option != 7) { err("%s: an x index register needs lsl or sxtx", c->mn); return 0; }
        if (m.hassh && m.shamt != 0 && m.shamt != 3) { err("%s: the shift amount must be 0 or 3", c->mn); return 0; }
        int S = m.hassh && m.shamt == 3;
        a64_W(c, 0xf8a00800u | ((u32)m.idx << 16) | ((u32)option << 13) | ((u32)S << 12) | ((u32)m.base << 5) | (u32)op);
        return 1;
    }
    if (m.hasmod) return emit_lo12(c, 0xf9800000u | ((u32)m.base << 5) | (u32)op, 3, &m.imm);
    i64 imm = 0;
    if (m.kind == 1) { if (!val_isconst(&m.imm)) { err("%s: the offset must be a constant", c->mn); return 0; } imm = m.imm.n; }
    if (!e->a && imm >= 0 && imm % 8 == 0 && imm / 8 < 4096) {
        a64_W(c, 0xf9800000u | ((u32)(imm / 8) << 10) | ((u32)m.base << 5) | (u32)op);
        return 1;
    }
    if (imm >= -256 && imm <= 255) {
        a64_W(c, 0xf8800000u | ((u32)(imm & 0x1ff) << 12) | ((u32)m.base << 5) | (u32)op);
        return 1;
    }
    err("%s: offset %lld is out of range", c->mn, (long long)imm);
    return 0;
}

#define E(name, fn, a, b, c) { name, fn, a, b, c }

const MnEnt a64_tab_mem[] = {
    E("ldr", h_ldst, 1, -1, 0), E("str", h_ldst, 0, -1, 0),
    E("ldrb", h_ldst, 1, 0, 0), E("strb", h_ldst, 0, 0, 0), E("ldrh", h_ldst, 1, 1, 0), E("strh", h_ldst, 0, 1, 0),
    E("ldrsb", h_ldst, 1, 0, 1), E("ldrsh", h_ldst, 1, 1, 1), E("ldrsw", h_ldst, 1, 2, 1),
    E("ldur", h_ldst, 3, -1, 0), E("stur", h_ldst, 2, -1, 0),
    E("ldurb", h_ldst, 3, 0, 0), E("sturb", h_ldst, 2, 0, 0), E("ldurh", h_ldst, 3, 1, 0), E("sturh", h_ldst, 2, 1, 0),
    E("ldursb", h_ldst, 3, 0, 1), E("ldursh", h_ldst, 3, 1, 1), E("ldursw", h_ldst, 3, 2, 1),
    E("ldtr", h_ldtr, 1, -1, 0), E("sttr", h_ldtr, 0, -1, 0), E("ldtrb", h_ldtr, 1, 0, 0), E("sttrb", h_ldtr, 0, 0, 0),
    E("ldtrh", h_ldtr, 1, 1, 0), E("sttrh", h_ldtr, 0, 1, 0), E("ldtrsb", h_ldtr, 1, 0, 1), E("ldtrsh", h_ldtr, 1, 1, 1), E("ldtrsw", h_ldtr, 1, 2, 1),
    E("ldp", h_ldp, 1, 0, 0), E("stp", h_ldp, 0, 0, 0), E("ldpsw", h_ldp, 1, 1, 0), E("ldnp", h_ldp, 3, 0, 0), E("stnp", h_ldp, 2, 0, 0),
    E("ldxr", h_excl, 0, 0, -1), E("ldxrb", h_excl, 0, 0, 0), E("ldxrh", h_excl, 0, 0, 1),
    E("ldaxr", h_excl, 0, 1, -1), E("ldaxrb", h_excl, 0, 1, 0), E("ldaxrh", h_excl, 0, 1, 1),
    E("stxr", h_excl, 1, 0, -1), E("stxrb", h_excl, 1, 0, 0), E("stxrh", h_excl, 1, 0, 1),
    E("stlxr", h_excl, 1, 1, -1), E("stlxrb", h_excl, 1, 1, 0), E("stlxrh", h_excl, 1, 1, 1),
    E("ldxp", h_excl, 2, 0, -1), E("ldaxp", h_excl, 2, 1, -1), E("stxp", h_excl, 3, 0, -1), E("stlxp", h_excl, 3, 1, -1),
    E("ldar", h_ldar, 0x08dffc00, 0, -1), E("ldarb", h_ldar, 0x08dffc00, 0, 0), E("ldarh", h_ldar, 0x08dffc00, 0, 1),
    E("ldlar", h_ldar, 0x08df7c00, 0, -1), E("ldlarb", h_ldar, 0x08df7c00, 0, 0), E("ldlarh", h_ldar, 0x08df7c00, 0, 1),
    E("stlr", h_ldar, 0x089ffc00, 0, -1), E("stlrb", h_ldar, 0x089ffc00, 0, 0), E("stlrh", h_ldar, 0x089ffc00, 0, 1),
    E("stllr", h_ldar, 0x089f7c00, 0, -1), E("stllrb", h_ldar, 0x089f7c00, 0, 0), E("stllrh", h_ldar, 0x089f7c00, 0, 1),
    E("ldapr", h_ldar, 0x38bfc000, 0, -1), E("ldaprb", h_ldar, 0x38bfc000, 0, 0), E("ldaprh", h_ldar, 0x38bfc000, 0, 1),
    E("prfm", h_prfm, 0, 0, 0), E("prfum", h_prfm, 1, 0, 0),
    { NULL, NULL, 0, 0, 0 }
};
