#include "a64_simd.h"

static int lg(int es) { return es == 8 ? 0 : es == 16 ? 1 : es == 32 ? 2 : 3; }

static int elem_imm5(Ctx *c, const VReg *v, int *out)
{
    if (v->es > 64) { err("%s: bad element size", c->mn); return 0; }
    int sz = lg(v->es);
    int max = 128 / v->es;
    if (v->lane < 0 || v->lane >= max) { err("%s: the element index must be 0 to %d for .%c elements", c->mn, max - 1, v->es == 8 ? 'b' : v->es == 16 ? 'h' : v->es == 32 ? 's' : 'd'); return 0; }
    *out = (v->lane << (sz + 1)) | (1 << sz);
    return 1;
}

static int is_elem(Ctx *c, int i, VReg *v)
{
    return i < c->n && a64_p_vec(c->tok[i], v) && !v->scalar && v->lane >= 0;
}

static int is_arr(Ctx *c, int i, VReg *v)
{
    return i < c->n && a64_p_vec(c->tok[i], v) && !v->scalar && v->has_arr && v->lane < 0;
}

int h_dup(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (c->n != 2) { sv_count(c, 2, 2); return 0; }
    VReg d, s;
    GReg g;
    if (a64_p_vec(c->tok[0], &d) && d.scalar) {
        if (!is_elem(c, 1, &s)) { err("dup: expected a vector element like v1.d[1]"); return 0; }
        if (s.es != d.es) { err("dup: the scalar size and the element size must match"); return 0; }
        int imm5;
        if (!elem_imm5(c, &s, &imm5)) return 0;
        a64_W(c, 0x5e000400u | ((u32)imm5 << 16) | ((u32)s.n << 5) | (u32)d.n);
        return 1;
    }
    int q;
    if (!sv_vreg(c, 0, &d)) return 0;
    if (!sv_arr(c, &d, 0, &q)) return 0;
    if (d.es > 64 || (d.es == 64 && !q)) { err("dup: .1d is not valid, use .2d"); return 0; }
    if (a64_p_gpr(c->tok[1], &g)) {
        if ((d.es == 64) != g.w64) { err("dup: use %s for .%s elements", d.es == 64 ? "an x register" : "a w register", sv_arrname(d.es, d.lanes)); return 0; }
        a64_W(c, 0x0e000c00u | ((u32)q << 30) | ((u32)(1 << lg(d.es)) << 16) | ((u32)g.n << 5) | (u32)d.n);
        return 1;
    }
    if (!is_elem(c, 1, &s)) { err("dup: the source must be a general register or a vector element like v1.s[2]"); return 0; }
    if (s.es != d.es) { err("dup: the element size must match the destination (.%s)", sv_arrname(d.es, d.lanes)); return 0; }
    int imm5;
    if (!elem_imm5(c, &s, &imm5)) return 0;
    a64_W(c, 0x0e000400u | ((u32)q << 30) | ((u32)imm5 << 16) | ((u32)s.n << 5) | (u32)d.n);
    return 1;
}

int h_ins(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (c->n != 2) { sv_count(c, 2, 2); return 0; }
    VReg d, s;
    GReg g;
    if (!is_elem(c, 0, &d)) { if (!sv_vreg(c, 0, &d)) return 0; err("%s: the destination must be a vector element like v0.s[1]", c->mn); return 0; }
    int imm5;
    if (!elem_imm5(c, &d, &imm5)) return 0;
    if (a64_p_gpr(c->tok[1], &g)) {
        if ((d.es == 64) != g.w64) { err("%s: use %s for .%c elements", c->mn, d.es == 64 ? "an x register" : "a w register", d.es == 8 ? 'b' : d.es == 16 ? 'h' : d.es == 32 ? 's' : 'd'); return 0; }
        a64_W(c, 0x4e001c00u | ((u32)imm5 << 16) | ((u32)g.n << 5) | (u32)d.n);
        return 1;
    }
    if (!is_elem(c, 1, &s)) { err("%s: the source must be a general register or a vector element", c->mn); return 0; }
    if (s.es != d.es) { err("%s: both elements must have the same size", c->mn); return 0; }
    int i2;
    if (!elem_imm5(c, &s, &i2)) return 0;
    int imm4 = s.lane << lg(s.es);
    a64_W(c, 0x6e000400u | ((u32)imm5 << 16) | ((u32)imm4 << 11) | ((u32)s.n << 5) | (u32)d.n);
    return 1;
}

int h_umov(Ctx *c, const MnEnt *e)
{
    int sg = e->a;
    if (c->n != 2) { sv_count(c, 2, 2); return 0; }
    GReg g;
    VReg s;
    if (!sv_gpr(c, 0, &g)) return 0;
    if (!is_elem(c, 1, &s)) { err("%s: the source must be a vector element like v1.s[2]", c->mn); return 0; }
    int imm5;
    if (!elem_imm5(c, &s, &imm5)) return 0;
    if (sg) {
        if (s.es > 32) { err("smov: use .b, .h or .s elements (for .d use umov or mov)"); return 0; }
        if (!g.w64 && s.es > 16) { err("smov: a w register takes .b or .h elements"); return 0; }
        a64_W(c, 0x0e002c00u | ((u32)g.w64 << 30) | ((u32)imm5 << 16) | ((u32)s.n << 5) | (u32)g.n);
        return 1;
    }
    if (s.es == 64) {
        if (!g.w64) { err("%s: .d elements go to an x register", c->mn); return 0; }
        a64_W(c, 0x4e003c00u | ((u32)imm5 << 16) | ((u32)s.n << 5) | (u32)g.n);
        return 1;
    }
    if (g.w64) { err("%s: .%c elements go to a w register", c->mn, s.es == 8 ? 'b' : s.es == 16 ? 'h' : 's'); return 0; }
    a64_W(c, 0x0e003c00u | ((u32)imm5 << 16) | ((u32)s.n << 5) | (u32)g.n);
    return 1;
}

int h_vmov(Ctx *c, const MnEnt *e)
{
    VReg a, b;
    GReg g;
    if (c->n != 2) return PASS;
    int a_el = is_elem(c, 0, &a);
    int b_el = is_elem(c, 1, &b);
    if (a_el) return h_ins(c, e);
    if (b_el && a64_p_gpr(c->tok[0], &g)) {
        MnEnt u = { "umov", h_umov, 0, 0, 0 };
        return h_umov(c, &u);
    }
    if (b_el && a64_p_vec(c->tok[0], &a) && a.scalar) {
        MnEnt u = { "dup", h_dup, 0, 0, 0 };
        return h_dup(c, &u);
    }
    if (is_arr(c, 0, &a) && is_arr(c, 1, &b)) {
        int qa, qb;
        if (!sv_arr(c, &a, 0, &qa) || !sv_arr(c, &b, 1, &qb)) return 0;
        if (qa != qb || a.es != b.es) { err("mov: both registers need the same arrangement"); return 0; }
        a64_W(c, 0x0ea01c00u | ((u32)qa << 30) | ((u32)b.n << 16) | ((u32)b.n << 5) | (u32)a.n);
        return 1;
    }
    return PASS;
}

static int imm_shift(Ctx *c, int i, int *kind, i64 *sh)
{
    *kind = 0;
    *sh = 0;
    if (i >= c->n) return 1;
    char b[40];
    snprintf(b, sizeof b, "%s", c->tok[i]);
    for (char *p = b; *p; p++) *p = (char)tolower((unsigned char)*p);
    char *q = b;
    if (!strncmp(q, "lsl", 3)) *kind = 0;
    else if (!strncmp(q, "msl", 3)) *kind = 1;
    else { err("%s: expected `lsl #n' or `msl #n', found `%s'", c->mn, c->tok[i]); return 0; }
    q += 3;
    while (*q == ' ') q++;
    if (*q == '#') q++;
    char *end;
    long v = strtol(q, &end, 0);
    if (end == q || *end) { err("%s: bad shift amount in `%s'", c->mn, c->tok[i]); return 0; }
    *sh = v;
    *kind += 2;
    return 1;
}

static u32 imm_enc(u32 q, u32 op, u32 cmode, u32 imm8, u32 rd)
{
    return 0x0f000400u | (q << 30) | (op << 29) | ((imm8 >> 5) << 16) | (cmode << 12) | ((imm8 & 31) << 5) | rd;
}

static int fit_shift(i64 *imm, int *sh, int step, int max, int *auto_ok)
{
    *auto_ok = 0;
    if (*imm >= 0 && *imm <= 255) return 1;
    for (int s = step; s <= max; s += step) {
        if (*imm > 0 && (*imm & (((i64)1 << s) - 1)) == 0 && (*imm >> s) <= 255) {
            *imm >>= s;
            *sh = s;
            *auto_ok = 1;
            return 1;
        }
    }
    return 0;
}

int h_movi(Ctx *c, const MnEnt *e)
{
    int inv = e->a;
    if (c->n < 2 || c->n > 3) { sv_count(c, 2, 3); return 0; }
    VReg d;
    if (!sv_vreg(c, 0, &d)) return 0;
    i64 imm;
    if (!sv_imm(c, 1, &imm)) return 0;
    int kind;
    i64 sh;
    if (!imm_shift(c, 2, &kind, &sh)) return 0;
    if (d.scalar) {
        if (d.sk != 'd' || inv) { err("%s: the scalar form is movi d0, #imm64", c->mn); return 0; }
        u32 m = 0;
        for (int i = 0; i < 8; i++) {
            u64 byte = ((u64)imm >> (i * 8)) & 0xff;
            if (byte == 0xff) m |= 1u << i;
            else if (byte) { err("movi: a 64-bit immediate needs every byte to be 0x00 or 0xff"); return 0; }
        }
        a64_W(c, 0x2f00e400u | ((m >> 5) << 16) | ((m & 31) << 5) | (u32)d.n);
        return 1;
    }
    int q;
    if (!sv_arr(c, &d, 0, &q)) return 0;
    if (d.es == 64) {
        if (inv) { err("mvni: not available for .2d"); return 0; }
        if (!q) { err("movi: .1d is not valid, use .2d or a d register"); return 0; }
        if (c->n == 3) { err("movi: .2d takes no shift"); return 0; }
        u32 m = 0;
        for (int i = 0; i < 8; i++) {
            u64 byte = ((u64)imm >> (i * 8)) & 0xff;
            if (byte == 0xff) m |= 1u << i;
            else if (byte) { err("movi: a 64-bit immediate needs every byte to be 0x00 or 0xff"); return 0; }
        }
        a64_W(c, imm_enc(1, 1, 0xe, m, (u32)d.n));
        return 1;
    }
    if (d.es == 8) {
        if (inv) { err("mvni: not available for .8b or .16b"); return 0; }
        if (c->n == 3) { err("movi: bytes take no shift"); return 0; }
        if (imm < -128 || imm > 255) { err("movi: the immediate must fit in 8 bits"); return 0; }
        a64_W(c, imm_enc((u32)q, 0, 0xe, (u32)imm & 255, (u32)d.n));
        return 1;
    }
    int au;
    if (d.es == 16) {
        if (kind >= 2 && (kind & 1)) { err("%s: msl is only for .2s and .4s", c->mn); return 0; }
        int s = (int)sh;
        if (c->n == 2) { if (!fit_shift(&imm, &s, 8, 8, &au)) { err("%s: the immediate must be an 8 bit value, optionally shifted left by 8", c->mn); return 0; } }
        if (s != 0 && s != 8) { err("%s: the shift must be 0 or 8 for .4h and .8h", c->mn); return 0; }
        if (imm < 0 || imm > 255) { err("%s: the immediate must fit in 8 bits", c->mn); return 0; }
        a64_W(c, imm_enc((u32)q, (u32)inv, 0x8u | ((u32)(s / 8) << 1), (u32)imm, (u32)d.n));
        return 1;
    }
    if (d.es == 32) {
        int s = (int)sh;
        if (kind == 3) {
            if (s != 8 && s != 16) { err("%s: msl takes 8 or 16", c->mn); return 0; }
            if (imm < 0 || imm > 255) { err("%s: the immediate must fit in 8 bits", c->mn); return 0; }
            a64_W(c, imm_enc((u32)q, (u32)inv, 0xc | (s == 16), (u32)imm, (u32)d.n));
            return 1;
        }
        if (c->n == 2) { if (!fit_shift(&imm, &s, 8, 24, &au)) { err("%s: the immediate must be an 8 bit value shifted left by 0, 8, 16 or 24", c->mn); return 0; } }
        if (s % 8 || s < 0 || s > 24) { err("%s: the shift must be 0, 8, 16 or 24", c->mn); return 0; }
        if (imm < 0 || imm > 255) { err("%s: the immediate must fit in 8 bits", c->mn); return 0; }
        a64_W(c, imm_enc((u32)q, (u32)inv, (u32)(s / 8) << 1, (u32)imm, (u32)d.n));
        return 1;
    }
    err("%s: bad arrangement", c->mn);
    return 0;
}

int h_orrbic(Ctx *c, const MnEnt *e)
{
    (void)e;
    int inv = !strcmp(c->mn, "bic");
    if (c->n < 2 || c->n > 3) { sv_count(c, 2, 3); return 0; }
    VReg d;
    if (!sv_vreg(c, 0, &d)) return 0;
    int q;
    if (!sv_arr(c, &d, 0, &q)) return 0;
    if (d.es != 16 && d.es != 32) { err("%s: immediate forms need .4h, .8h, .2s or .4s", c->mn); return 0; }
    i64 imm;
    if (!sv_imm(c, 1, &imm)) return 0;
    int kind;
    i64 sh;
    if (!imm_shift(c, 2, &kind, &sh)) return 0;
    if (kind >= 2 && (kind & 1)) { err("%s: msl is not available here", c->mn); return 0; }
    int s = (int)sh, au;
    int mx = d.es == 16 ? 8 : 24;
    if (c->n == 2) { if (!fit_shift(&imm, &s, 8, mx, &au)) { err("%s: the immediate must be an 8 bit value, optionally shifted left", c->mn); return 0; } }
    if (s % 8 || s < 0 || s > mx) { err("%s: the shift must be %s", c->mn, d.es == 16 ? "0 or 8" : "0, 8, 16 or 24"); return 0; }
    if (imm < 0 || imm > 255) { err("%s: the immediate must fit in 8 bits", c->mn); return 0; }
    u32 cm = d.es == 16 ? 0x9u | ((u32)(s / 8) << 1) : 0x1u | ((u32)(s / 8) << 1);
    a64_W(c, imm_enc((u32)q, (u32)inv, cm, (u32)imm, (u32)d.n));
    return 1;
}

int h_vfmov(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (c->n != 2) return PASS;
    VReg a, b;
    GReg g;
    if (is_elem(c, 0, &a) && a64_p_gpr(c->tok[1], &g)) {
        if (a.es != 64 || a.lane != 1 || !g.w64) { err("fmov: the only element form is fmov v0.d[1], x1"); return 0; }
        a64_W(c, 0x9eaf0000u | ((u32)g.n << 5) | (u32)a.n);
        return 1;
    }
    if (is_elem(c, 1, &b) && a64_p_gpr(c->tok[0], &g)) {
        if (b.es != 64 || b.lane != 1 || !g.w64) { err("fmov: the only element form is fmov x0, v1.d[1]"); return 0; }
        a64_W(c, 0x9eae0000u | ((u32)b.n << 5) | (u32)g.n);
        return 1;
    }
    if (is_arr(c, 0, &a)) {
        int q, imm;
        if (!sv_arr(c, &a, 0, &q)) return 0;
        if (!fp_imm8(c->tok[1], &imm)) { err("fmov: `%s' cannot be encoded as a floating-point immediate (use values like 1.0, 0.5, -2.5, 31.0)", c->tok[1]); return 0; }
        if (a.es == 32) { a64_W(c, imm_enc((u32)q, 0, 0xf, (u32)imm, (u32)a.n)); return 1; }
        if (a.es == 64) {
            if (!q) { err("fmov: .1d is not valid, use .2d"); return 0; }
            a64_W(c, imm_enc(1, 1, 0xf, (u32)imm, (u32)a.n));
            return 1;
        }
        if (a.es == 16) { a64_W(c, 0x0f00fc00u | ((u32)q << 30) | (((u32)imm >> 5) << 16) | (((u32)imm & 31) << 5) | (u32)a.n); return 1; }
        err("fmov: use .2s, .4s, .2d, .4h or .8h");
        return 0;
    }
    return PASS;
}

int h_pairwise(Ctx *c, const MnEnt *e)
{
    (void)e;
    static const struct { const char *n; u32 base; } T[] = {
        { "addp", 0x5ef1b800u }, { "faddp", 0x7e30d800u }, { "fmaxnmp", 0x7e30c800u }, { "fmaxp", 0x7e30f800u }, { "fminnmp", 0x7eb0c800u }, { "fminp", 0x7eb0f800u },
    };
    if (c->n != 2) return PASS;
    int k = -1;
    for (int i = 0; i < 6; i++) if (!strcmp(T[i].n, c->mn)) k = i;
    if (k < 0) return PASS;
    VReg d, s;
    if (!sv_vreg(c, 0, &d)) return 0;
    if (!d.scalar) return PASS;
    if (!sv_vreg(c, 1, &s)) return 0;
    if (s.scalar || !s.has_arr) { err("%s: the source must be a vector like v1.2d", c->mn); return 0; }
    u32 w = T[k].base;
    if (k == 0) {
        if (d.sk != 'd' || s.es != 64 || s.lanes != 2) { err("addp: the scalar form is addp d0, v1.2d"); return 0; }
        a64_W(c, w | ((u32)s.n << 5) | (u32)d.n);
        return 1;
    }
    if (d.sk == 'h') {
        if (s.es != 16 || s.lanes != 2) { err("%s: h destinations take v1.2h", c->mn); return 0; }
        a64_W(c, (w & ~(1u << 29)) | ((u32)s.n << 5) | (u32)d.n);
        return 1;
    }
    if (d.sk == 's' && s.es == 32 && s.lanes == 2) { a64_W(c, w | ((u32)s.n << 5) | (u32)d.n); return 1; }
    if (d.sk == 'd' && s.es == 64 && s.lanes == 2) { a64_W(c, w | (1u << 22) | ((u32)s.n << 5) | (u32)d.n); return 1; }
    err("%s: the scalar form is s from .2s, or d from .2d", c->mn);
    return 0;
}

int h_xtl(Ctx *c, const MnEnt *e)
{
    int u = e->a;
    int p2 = c->mn[strlen(c->mn) - 1] == '2';
    if (c->n != 2) { sv_count(c, 2, 2); return 0; }
    VReg d, s;
    int qd, qs;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &s)) return 0;
    if (!sv_arr(c, &d, 0, &qd) || !sv_arr(c, &s, 1, &qs)) return 0;
    if (!qd || d.es != s.es * 2 || s.es > 32 || qs != p2 || d.lanes * (p2 ? 2 : 1) != s.lanes) {
        err("%s: bad arrangements (like %s v0.8h, v1.%s)", c->mn, c->mn, p2 ? "16b" : "8b");
        return 0;
    }
    a64_W(c, 0x0f00a400u | ((u32)p2 << 30) | ((u32)u << 29) | ((u32)(s.es) << 16) | ((u32)s.n << 5) | (u32)d.n);
    return 1;
}
