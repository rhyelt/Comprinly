/* ARM64 scalar floating point: fmov, arithmetic, conversions, compares and selects. Vector forms are passed on to the SIMD code. */
#include "a64.h"
#include <math.h>

/* Checks the operand count and reports a count error if it is outside lo..hi. */
static int usage(Ctx *c, int lo, int hi)
{
    if (lo == hi) err("%s: expected %d operand%s, found %d", c->mn, lo, lo == 1 ? "" : "s", c->n);
    else err("%s: expected %d to %d operands, found %d", c->mn, lo, hi, c->n);
    return 0;
}

#define NEED(lo, hi) do { if (c->n < (lo) || c->n > (hi)) return usage(c, (lo), (hi)); } while (0)

int a64_simd_dispatch(Ctx *c);

/* Parses a scalar FP register (h, s or d). */
static int fpreg(Ctx *c, int i, VReg *v, const char *what)
{
    if (!a64_p_vec(c->tok[i], v) || !v->scalar || v->sk == 'b' || v->sk == 'q') {
        err("%s: operand %d: %s register (h, s or d) expected, found `%s'", c->mn, i + 1, what, c->tok[i]);
        return 0;
    }
    return 1;
}

/* FP type field value for a register size. */
static int ftype(const VReg *v)
{
    return v->sk == 's' ? 0 : v->sk == 'd' ? 1 : 3;
}

/* True if any operand is a vector register, so the SIMD handler must take over. */
static int is_vecop(Ctx *c)
{
    for (int i = 0; i < c->n; i++) {
        const char *s = c->tok[i];
        while (*s == ' ' || *s == '{') s++;
        if ((s[0] == 'v' || s[0] == 'V') && isdigit((unsigned char)s[1])) return 1;
    }
    return 0;
}

/* True if an operand is a SIMD scalar register (b, h, s, d, q). */
static int any_scalar_simd(Ctx *c)
{
    VReg v;
    for (int i = 0; i < c->n; i++) if (a64_p_vec(c->tok[i], &v) && v.scalar && (v.sk == 'b' || v.sk == 'q')) return 1;
    return 0;
}

/* Encodes a floating point constant as the 8-bit fmov immediate. Returns 0 if not representable. */
int fp_imm8(const char *s, int *out)
{
    s = a64_strip_hash(s);
    if (!*s) return 0;
    char *end;
    double d;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        long v = strtol(s, &end, 16);
        if (*end || v < 0 || v > 255) return 0;
        *out = (int)v;
        return 1;
    }
    d = strtod(s, &end);
    if (*end || end == s) return 0;
    for (int i = 0; i < 256; i++) {
        int sign = (i >> 7) & 1;
        int b = (i >> 6) & 1;
        int cd = (i >> 4) & 3;
        int frac = i & 15;
        int n = ((b ^ 1) << 2) | cd;
        int ex = n - 3;
        double v = (1.0 + frac / 16.0) * ldexp(1.0, ex);
        if (sign) v = -v;
        if (v == d && !(d == 0 && signbit(d))) { *out = i; return 1; }
    }
    return 0;
}

/* True if text looks like a number with a fraction or exponent. */
static int looks_float(const char *s)
{
    s = a64_strip_hash(s);
    if (*s == '-' || *s == '+') s++;
    return isdigit((unsigned char)*s) || *s == '.';
}

/* fmov: register, general register, immediate and vector element forms. */
static int h_fmov(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(2, 2);
    if (is_vecop(c)) {
        VReg a, b;
        GReg g;
        if (a64_p_vec(c->tok[0], &a) && a.lane == 1 && a.es == 64 && a64_p_gpr(c->tok[1], &g) && g.w64) {
            a64_W(c, 0x9eaf0000u | ((u32)g.n << 5) | (u32)a.n);
            return 1;
        }
        if (a64_p_gpr(c->tok[0], &g) && a64_p_vec(c->tok[1], &b) && b.lane == 1 && b.es == 64 && g.w64) {
            a64_W(c, 0x9eae0000u | ((u32)b.n << 5) | (u32)g.n);
            return 1;
        }
        return a64_simd_dispatch(c);
    }
    VReg d, s;
    GReg g;
    if (a64_p_vec(c->tok[0], &d) && d.scalar) {
        if (d.sk == 'b' || d.sk == 'q') { err("%s: operand 1: h, s or d register expected", c->mn); return 0; }
        int t = ftype(&d);
        if (a64_p_vec(c->tok[1], &s) && s.scalar) {
            if (s.sk != d.sk) { err("%s: register sizes do not match", c->mn); return 0; }
            a64_W(c, 0x1e204000u | ((u32)t << 22) | ((u32)s.n << 5) | (u32)d.n);
            return 1;
        }
        if (a64_p_gpr(c->tok[1], &g)) {
            if (g.sp) { err("%s: sp is not allowed here", c->mn); return 0; }
            if (d.sk == 's' && !g.w64) { a64_W(c, 0x1e270000u | ((u32)g.n << 5) | (u32)d.n); return 1; }
            if (d.sk == 'd' && g.w64) { a64_W(c, 0x9e670000u | ((u32)g.n << 5) | (u32)d.n); return 1; }
            if (d.sk == 'h') { a64_W(c, (g.w64 ? 0x9ee70000u : 0x1ee70000u) | ((u32)g.n << 5) | (u32)d.n); return 1; }
            err("%s: register sizes do not match (s pairs with w, d pairs with x)", c->mn);
            return 0;
        }
        int imm;
        if (looks_float(c->tok[1])) {
            const char *zt = a64_strip_hash(c->tok[1]);
            char *zend;
            double zv = strtod(zt, &zend);
            if (!*zend && zend != zt && zv == 0.0 && !signbit(zv)) {
                if (d.sk == 's') a64_W(c, 0x1e270000u | (31u << 5) | (u32)d.n);
                else if (d.sk == 'd') a64_W(c, 0x9e670000u | (31u << 5) | (u32)d.n);
                else a64_W(c, 0x1ee70000u | (31u << 5) | (u32)d.n);
                return 1;
            }
            if (!fp_imm8(c->tok[1], &imm)) { err("%s: `%s' cannot be encoded as a floating-point immediate (use values like 1.0, 0.5, -2.5, 31.0)", c->mn, c->tok[1]); return 0; }
            a64_W(c, 0x1e201000u | ((u32)t << 22) | ((u32)imm << 13) | (u32)d.n);
            return 1;
        }
        err("%s: bad source operand `%s'", c->mn, c->tok[1]);
        return 0;
    }
    if (a64_p_gpr(c->tok[0], &g) && a64_p_vec(c->tok[1], &s) && s.scalar) {
        if (g.sp) { err("%s: sp is not allowed here", c->mn); return 0; }
        if (s.sk == 's' && !g.w64) { a64_W(c, 0x1e260000u | ((u32)s.n << 5) | (u32)g.n); return 1; }
        if (s.sk == 'd' && g.w64) { a64_W(c, 0x9e660000u | ((u32)s.n << 5) | (u32)g.n); return 1; }
        if (s.sk == 'h') { a64_W(c, (g.w64 ? 0x9ee60000u : 0x1ee60000u) | ((u32)s.n << 5) | (u32)g.n); return 1; }
        err("%s: register sizes do not match (s pairs with w, d pairs with x)", c->mn);
        return 0;
    }
    err("%s: unsupported operands", c->mn);
    return 0;
}

/* Two operand FP arithmetic (fadd, fsub, fmul, fdiv, fmax ...). */
static int h_fp2(Ctx *c, const MnEnt *e)
{
    if (is_vecop(c)) return a64_simd_dispatch(c);
    NEED(3, 3);
    if (any_scalar_simd(c)) return a64_simd_dispatch(c);
    VReg d, n, m;
    if (!fpreg(c, 0, &d, "floating-point") || !fpreg(c, 1, &n, "floating-point") || !fpreg(c, 2, &m, "floating-point")) return 0;
    if (d.sk != n.sk || d.sk != m.sk) { err("%s: register sizes do not match", c->mn); return 0; }
    a64_W(c, 0x1e200800u | ((u32)ftype(&d) << 22) | ((u32)m.n << 16) | ((u32)e->a << 12) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* One operand FP instructions (fabs, fneg, fsqrt, frint ...). */
static int h_fp1(Ctx *c, const MnEnt *e)
{
    if (is_vecop(c)) return a64_simd_dispatch(c);
    NEED(2, 2);
    if (any_scalar_simd(c)) return a64_simd_dispatch(c);
    VReg d, n;
    if (!fpreg(c, 0, &d, "floating-point") || !fpreg(c, 1, &n, "floating-point")) return 0;
    if (d.sk != n.sk) { err("%s: register sizes do not match", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)ftype(&d) << 22) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* fcvt between precisions. */
static int h_fcvt(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(2, 2);
    VReg d, s;
    if (!fpreg(c, 0, &d, "floating-point") || !fpreg(c, 1, &s, "floating-point")) return 0;
    if (d.sk == s.sk) { err("%s: source and destination must differ in size", c->mn); return 0; }
    int opc = ftype(&d);
    a64_W(c, 0x1e224000u | ((u32)ftype(&s) << 22) | ((u32)opc << 15) | ((u32)s.n << 5) | (u32)d.n);
    return 1;
}

/* Fused multiply-add family (fmadd, fmsub ...). */
static int h_fp3(Ctx *c, const MnEnt *e)
{
    NEED(4, 4);
    VReg d, n, m, a;
    if (!fpreg(c, 0, &d, "floating-point") || !fpreg(c, 1, &n, "floating-point") || !fpreg(c, 2, &m, "floating-point") || !fpreg(c, 3, &a, "floating-point")) return 0;
    if (d.sk != n.sk || d.sk != m.sk || d.sk != a.sk) { err("%s: register sizes do not match", c->mn); return 0; }
    a64_W(c, 0x1f000000u | ((u32)ftype(&d) << 22) | ((u32)e->a << 21) | ((u32)m.n << 16) | ((u32)e->b << 15) | ((u32)a.n << 10) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* fcmp and fcmpe. */
static int h_fcmp(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    VReg n, m;
    if (!fpreg(c, 0, &n, "floating-point")) return 0;
    u32 w = 0x1e202000u | ((u32)ftype(&n) << 22) | ((u32)n.n << 5) | ((u32)e->a << 4);
    if (a64_p_vec(c->tok[1], &m) && m.scalar) {
        if (m.sk != n.sk) { err("%s: register sizes do not match", c->mn); return 0; }
        a64_W(c, w | ((u32)m.n << 16));
        return 1;
    }
    const char *t = a64_strip_hash(c->tok[1]);
    char *end;
    double d = strtod(t, &end);
    if (*end || end == t || d != 0.0) { err("%s: the second operand must be a register or #0.0", c->mn); return 0; }
    a64_W(c, w | 8u);
    return 1;
}

/* fccmp and fccmpe. */
static int h_fccmp(Ctx *c, const MnEnt *e)
{
    NEED(4, 4);
    VReg n, m;
    int cc, unk;
    i64 nzcv;
    if (!fpreg(c, 0, &n, "floating-point") || !fpreg(c, 1, &m, "floating-point")) return 0;
    if (n.sk != m.sk) { err("%s: register sizes do not match", c->mn); return 0; }
    if (!a64_const(c, c->tok[2], &nzcv, &unk)) return 0;
    if (unk) nzcv = 0;
    if (nzcv < 0 || nzcv > 15) { err("%s: nzcv must be 0 to 15", c->mn); return 0; }
    if (!a64_p_cond(c->tok[3], &cc)) { err("%s: condition code expected, found `%s'", c->mn, c->tok[3]); return 0; }
    a64_W(c, 0x1e200400u | ((u32)ftype(&n) << 22) | ((u32)m.n << 16) | ((u32)cc << 12) | ((u32)n.n << 5) | ((u32)e->a << 4) | (u32)nzcv);
    return 1;
}

/* fcsel. */
static int h_fcsel(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(4, 4);
    VReg d, n, m;
    int cc;
    if (!fpreg(c, 0, &d, "floating-point") || !fpreg(c, 1, &n, "floating-point") || !fpreg(c, 2, &m, "floating-point")) return 0;
    if (d.sk != n.sk || d.sk != m.sk) { err("%s: register sizes do not match", c->mn); return 0; }
    if (!a64_p_cond(c->tok[3], &cc)) { err("%s: condition code expected, found `%s'", c->mn, c->tok[3]); return 0; }
    a64_W(c, 0x1e200c00u | ((u32)ftype(&d) << 22) | ((u32)m.n << 16) | ((u32)cc << 12) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* Conversions between FP and integer (scvtf, ucvtf, fcvtzs, fcvtzu ...). */
static int h_cvt_int(Ctx *c, const MnEnt *e)
{
    if (is_vecop(c)) return a64_simd_dispatch(c);
    NEED(2, 3);
    GReg g;
    VReg f;
    int to_int = a64_p_gpr(c->tok[0], &g);
    int from_int = 0;
    if (!to_int) {
        if (c->n >= 2 && a64_p_gpr(c->tok[1], &g)) from_int = 1;
        else return a64_simd_dispatch(c);
    }
    if (to_int && e->c == 1) { err("%s: the destination must be a floating-point register", c->mn); return 0; }
    if (from_int && e->c == 0) { err("%s: the destination must be a general register", c->mn); return 0; }
    if (g.sp) { err("%s: sp is not allowed here", c->mn); return 0; }
    if (!fpreg(c, to_int ? 1 : 0, &f, "floating-point")) return 0;
    int sf = g.w64;
    int t = ftype(&f);
    if (c->n == 3) {
        i64 fb;
        int unk;
        if (!(e->b & 0x200)) { err("%s: has no fixed-point form", c->mn); return 0; }
        if (!a64_const(c, c->tok[2], &fb, &unk)) return 0;
        if (unk) fb = 1;
        if (fb < 1 || fb > (sf ? 64 : 32)) { err("%s: the number of fractional bits must be 1 to %d", c->mn, sf ? 64 : 32); return 0; }
        u32 fx = e->c == 1 ? (0x1e020000u | ((u32)(e->b & 1) << 16)) : (0x1e180000u | ((u32)(e->b & 1) << 16));
        a64_W(c, ((u32)sf << 31) | fx | ((u32)t << 22) | ((u32)(64 - fb) << 10) | ((u32)(to_int ? f.n : g.n) << 5) | (u32)(to_int ? g.n : f.n));
        return 1;
    }
    a64_W(c, ((u32)sf << 31) | 0x1e200000u | ((u32)(e->b & 7) << 16) | ((u32)(e->a) << 19) | ((u32)t << 22) | ((u32)(to_int ? f.n : g.n) << 5) | (u32)(to_int ? g.n : f.n));
    return 1;
}

/* fjcvtzs. */
static int h_fjcvtzs(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(2, 2);
    GReg g;
    VReg f;
    if (!a64_p_gpr(c->tok[0], &g) || g.w64) { err("fjcvtzs: the destination must be a w register"); return 0; }
    if (!fpreg(c, 1, &f, "floating-point") || f.sk != 'd') { err("fjcvtzs: the source must be a d register"); return 0; }
    a64_W(c, 0x1e7e0000u | ((u32)f.n << 5) | (u32)g.n);
    return 1;
}

#define E(name, fn, a, b, c) { name, fn, a, b, c }

const MnEnt a64_tab_fp[] = {
    E("fmov", h_fmov, 0, 0, 0),
    E("fmul", h_fp2, 0, 0, 0), E("fdiv", h_fp2, 1, 0, 0), E("fadd", h_fp2, 2, 0, 0), E("fsub", h_fp2, 3, 0, 0),
    E("fmax", h_fp2, 4, 0, 0), E("fmin", h_fp2, 5, 0, 0), E("fmaxnm", h_fp2, 6, 0, 0), E("fminnm", h_fp2, 7, 0, 0), E("fnmul", h_fp2, 8, 0, 0),
    E("fabs", h_fp1, 0x1e20c000, 0, 0), E("fneg", h_fp1, 0x1e214000, 0, 0), E("fsqrt", h_fp1, 0x1e21c000, 0, 0),
    E("frintn", h_fp1, 0x1e244000, 0, 0), E("frintp", h_fp1, 0x1e24c000, 0, 0), E("frintm", h_fp1, 0x1e254000, 0, 0),
    E("frintz", h_fp1, 0x1e25c000, 0, 0), E("frinta", h_fp1, 0x1e264000, 0, 0), E("frintx", h_fp1, 0x1e274000, 0, 0), E("frinti", h_fp1, 0x1e27c000, 0, 0),
    E("fcvt", h_fcvt, 0, 0, 0),
    E("fmadd", h_fp3, 0, 0, 0), E("fmsub", h_fp3, 0, 1, 0), E("fnmadd", h_fp3, 1, 0, 0), E("fnmsub", h_fp3, 1, 1, 0),
    E("fcmp", h_fcmp, 0, 0, 0), E("fcmpe", h_fcmp, 1, 0, 0),
    E("fccmp", h_fccmp, 0, 0, 0), E("fccmpe", h_fccmp, 1, 0, 0), E("fcsel", h_fcsel, 0, 0, 0),
    E("scvtf", h_cvt_int, 0, 2 | 0x200, 1), E("ucvtf", h_cvt_int, 0, 3 | 0x200, 1),
    E("fcvtns", h_cvt_int, 0, 0, 0), E("fcvtnu", h_cvt_int, 0, 1, 0), E("fcvtas", h_cvt_int, 0, 4, 0), E("fcvtau", h_cvt_int, 0, 5, 0),
    E("fcvtps", h_cvt_int, 1, 0, 0), E("fcvtpu", h_cvt_int, 1, 1, 0), E("fcvtms", h_cvt_int, 2, 0, 0), E("fcvtmu", h_cvt_int, 2, 1, 0),
    E("fcvtzs", h_cvt_int, 3, 0x200 | 0, 0), E("fcvtzu", h_cvt_int, 3, 0x200 | 1, 0),
    E("fjcvtzs", h_fjcvtzs, 0, 0, 0),
    { NULL, NULL, 0, 0, 0 }
};
