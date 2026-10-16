/* ARM64 SIMD (NEON): vector helpers, three-same, two-register, across-lane, permute and table instructions, and the dispatcher that routes vector forms. */
#include "a64_simd.h"

/* Operand i as a vector register. */
int sv_vreg(Ctx *c, int i, VReg *v)
{
    if (!a64_p_vec(c->tok[i], v)) {
        err("%s: operand %d: vector register expected, found `%s'", c->mn, i + 1, c->tok[i]);
        return 0;
    }
    return 1;
}

/* Operand i as a general register. */
int sv_gpr(Ctx *c, int i, GReg *g)
{
    if (!a64_p_gpr(c->tok[i], g)) {
        err("%s: operand %d: general register expected, found `%s'", c->mn, i + 1, c->tok[i]);
        return 0;
    }
    return 1;
}

/* Element size code (0 to 3) for 8, 16, 32 or 64 bits. */
int sv_esz(int es)
{
    return es == 8 ? 0 : es == 16 ? 1 : es == 32 ? 2 : es == 64 ? 3 : 4;
}

/* Arrangement text such as 4s or 16b for messages. */
const char *sv_arrname(int es, int lanes)
{
    static char b[8][12];
    static int k;
    char *p = b[k++ & 7];
    snprintf(p, 12, "%d%c", lanes, es == 8 ? 'b' : es == 16 ? 'h' : es == 32 ? 's' : es == 64 ? 'd' : 'q');
    return p;
}

/* Checks that a register has a full arrangement and returns the Q bit. */
int sv_arr(Ctx *c, const VReg *v, int i, int *q)
{
    if (v->scalar || !v->has_arr || v->lane >= 0) {
        err("%s: operand %d: expected a vector with an arrangement (like v0.4s), found `%s'", c->mn, i + 1, c->tok[i]);
        return 0;
    }
    int bits = v->es * v->lanes;
    if (bits == 128) *q = 1;
    else if (bits == 64) *q = 0;
    else {
        err("%s: operand %d: arrangement .%s is not valid here", c->mn, i + 1, sv_arrname(v->es, v->lanes));
        return 0;
    }
    return 1;
}

/* Checks that two registers have the same arrangement. */
int sv_same(Ctx *c, const VReg *a, const VReg *b, int i, int j)
{
    if (a->es != b->es || a->lanes != b->lanes) {
        err("%s: operands %d and %d must have the same arrangement", c->mn, i + 1, j + 1);
        return 0;
    }
    return 1;
}

/* Checks the operand count. */
int sv_count(Ctx *c, int lo, int hi)
{
    if (c->n < lo || c->n > hi) {
        if (lo == hi) err("%s: expected %d operand%s, found %d", c->mn, lo, lo == 1 ? "" : "s", c->n);
        else err("%s: expected %d to %d operands, found %d", c->mn, lo, hi, c->n);
        return 0;
    }
    return 1;
}

/* True if any operand is a vector register. */
int sv_has_vec(Ctx *c)
{
    VReg v;
    for (int i = 0; i < c->n; i++) if (a64_p_vec(c->tok[i], &v) && !v.scalar) return 1;
    return 0;
}

/* True if all operands are scalar SIMD registers. */
int sv_all_scalar(Ctx *c)
{
    VReg v;
    if (!c->n) return 0;
    for (int i = 0; i < c->n; i++) if (!a64_p_vec(c->tok[i], &v) || !v.scalar) return 0;
    return 1;
}

/* Operand i as a constant. */
int sv_imm(Ctx *c, int i, i64 *v)
{
    int unk;
    if (!a64_const(c, c->tok[i], v, &unk)) return 0;
    if (unk) *v = 0;
    return 1;
}

#define SZ_B 1
#define SZ_H 2
#define SZ_S 4
#define SZ_D 8
#define SZ_BHS 7
#define SZ_ALL 15
#define SZ_HS 6

/* True if an element size is in the allowed mask. */
static int sz_ok(int mask, int es)
{
    int s = sv_esz(es);
    return s <= 3 && (mask & (1 << s));
}

typedef struct { const char *n; int u; int opc; int sizes; int fp; int a; } T3;

static const T3 t3[] = {
    { "shadd", 0, 0x00, SZ_BHS, 0, 0 }, { "uhadd", 1, 0x00, SZ_BHS, 0, 0 },
    { "sqadd", 0, 0x01, SZ_ALL, 0, 0 }, { "uqadd", 1, 0x01, SZ_ALL, 0, 0 },
    { "srhadd", 0, 0x02, SZ_BHS, 0, 0 }, { "urhadd", 1, 0x02, SZ_BHS, 0, 0 },
    { "shsub", 0, 0x04, SZ_BHS, 0, 0 }, { "uhsub", 1, 0x04, SZ_BHS, 0, 0 },
    { "sqsub", 0, 0x05, SZ_ALL, 0, 0 }, { "uqsub", 1, 0x05, SZ_ALL, 0, 0 },
    { "cmgt", 0, 0x06, SZ_ALL, 0, 0 }, { "cmhi", 1, 0x06, SZ_ALL, 0, 0 },
    { "cmge", 0, 0x07, SZ_ALL, 0, 0 }, { "cmhs", 1, 0x07, SZ_ALL, 0, 0 },
    { "sshl", 0, 0x08, SZ_ALL, 0, 0 }, { "ushl", 1, 0x08, SZ_ALL, 0, 0 },
    { "sqshl", 0, 0x09, SZ_ALL, 0, 0 }, { "uqshl", 1, 0x09, SZ_ALL, 0, 0 },
    { "srshl", 0, 0x0a, SZ_ALL, 0, 0 }, { "urshl", 1, 0x0a, SZ_ALL, 0, 0 },
    { "sqrshl", 0, 0x0b, SZ_ALL, 0, 0 }, { "uqrshl", 1, 0x0b, SZ_ALL, 0, 0 },
    { "smax", 0, 0x0c, SZ_BHS, 0, 0 }, { "umax", 1, 0x0c, SZ_BHS, 0, 0 },
    { "smin", 0, 0x0d, SZ_BHS, 0, 0 }, { "umin", 1, 0x0d, SZ_BHS, 0, 0 },
    { "sabd", 0, 0x0e, SZ_BHS, 0, 0 }, { "uabd", 1, 0x0e, SZ_BHS, 0, 0 },
    { "saba", 0, 0x0f, SZ_BHS, 0, 0 }, { "uaba", 1, 0x0f, SZ_BHS, 0, 0 },
    { "add", 0, 0x10, SZ_ALL, 0, 0 }, { "sub", 1, 0x10, SZ_ALL, 0, 0 },
    { "cmtst", 0, 0x11, SZ_ALL, 0, 0 }, { "cmeq", 1, 0x11, SZ_ALL, 0, 0 },
    { "mla", 0, 0x12, SZ_BHS, 0, 0 }, { "mls", 1, 0x12, SZ_BHS, 0, 0 },
    { "mul", 0, 0x13, SZ_BHS, 0, 0 }, { "pmul", 1, 0x13, SZ_B, 0, 0 },
    { "smaxp", 0, 0x14, SZ_BHS, 0, 0 }, { "umaxp", 1, 0x14, SZ_BHS, 0, 0 },
    { "sminp", 0, 0x15, SZ_BHS, 0, 0 }, { "uminp", 1, 0x15, SZ_BHS, 0, 0 },
    { "sqdmulh", 0, 0x16, SZ_HS, 0, 0 }, { "sqrdmulh", 1, 0x16, SZ_HS, 0, 0 },
    { "addp", 0, 0x17, SZ_ALL, 0, 0 },
    { "fmaxnm", 0, 0x18, 0, 1, 0 }, { "fminnm", 0, 0x18, 0, 1, 1 },
    { "fmla", 0, 0x19, 0, 1, 0 }, { "fmls", 0, 0x19, 0, 1, 1 },
    { "fadd", 0, 0x1a, 0, 1, 0 }, { "fsub", 0, 0x1a, 0, 1, 1 },
    { "fmulx", 0, 0x1b, 0, 1, 0 },
    { "fcmeq", 0, 0x1c, 0, 1, 0 },
    { "fmax", 0, 0x1e, 0, 1, 0 }, { "fmin", 0, 0x1e, 0, 1, 1 },
    { "frecps", 0, 0x1f, 0, 1, 0 }, { "frsqrts", 0, 0x1f, 0, 1, 1 },
    { "fmaxnmp", 1, 0x18, 0, 1, 0 }, { "fminnmp", 1, 0x18, 0, 1, 1 },
    { "faddp", 1, 0x1a, 0, 1, 0 }, { "fabd", 1, 0x1a, 0, 1, 1 },
    { "fmul", 1, 0x1b, 0, 1, 0 },
    { "fcmge", 1, 0x1c, 0, 1, 0 }, { "fcmgt", 1, 0x1c, 0, 1, 1 },
    { "facge", 1, 0x1d, 0, 1, 0 }, { "facgt", 1, 0x1d, 0, 1, 1 },
    { "fmaxp", 1, 0x1e, 0, 1, 0 }, { "fminp", 1, 0x1e, 0, 1, 1 },
    { "fdiv", 1, 0x1f, 0, 1, 0 },
    { NULL, 0, 0, 0, 0, 0 }
};

/* Finds a three-same table entry. */
static const T3 *t3_find(const char *mn)
{
    for (const T3 *t = t3; t->n; t++) if (!strcmp(t->n, mn)) return t;
    return NULL;
}

/* True if the three-same instruction has a scalar form. */
static int scalar3_ok(const T3 *t)
{
    static const char *const ok[] = { "sqadd", "uqadd", "sqsub", "uqsub", "cmgt", "cmhi", "cmge", "cmhs", "sshl", "ushl", "sqshl", "uqshl", "srshl", "urshl", "sqrshl", "uqrshl",
        "add", "sub", "cmtst", "cmeq", "sqdmulh", "sqrdmulh", "fmulx", "fcmeq", "fcmge", "fcmgt", "facge", "facgt", "frecps", "frsqrts", "fabd", NULL };
    for (int i = 0; ok[i]; i++) if (!strcmp(ok[i], t->n)) return 1;
    return 0;
}

/* True if the scalar form exists only for 64-bit elements. */
static int scalar3_d_only(const T3 *t)
{
    static const char *const d[] = { "cmgt", "cmhi", "cmge", "cmhs", "sshl", "ushl", "srshl", "urshl", "add", "sub", "cmtst", "cmeq", NULL };
    for (int i = 0; d[i]; i++) if (!strcmp(d[i], t->n)) return 1;
    return 0;
}

/* Three register same-arrangement instructions (add, sub, mul, cmeq, ...). */
int h_three(Ctx *c, const MnEnt *e)
{
    (void)e;
    const T3 *t = t3_find(c->mn);
    if (!t) return PASS;
    if (c->n == 2) return h_pairwise(c, e);
    if (c->n == 3 && elem_operand(c, 2)) return h_byelem(c, e);
    if (c->n == 3 && shift_known(c->mn) && (c->tok[2][0] == '#' || isdigit((unsigned char)c->tok[2][0]))) return h_shift(c, e);
    if (c->n == 3 && (c->tok[2][0] == '#' || isdigit((unsigned char)c->tok[2][0]) || c->tok[2][0] == '-')) return h_two(c, e);
    if (!sv_has_vec(c)) {
        if (!sv_all_scalar(c) || c->n != 3) return PASS;
        if (!scalar3_ok(t)) return PASS;
        VReg d, n, m;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &m)) return 0;
        if (d.sk != n.sk || d.sk != m.sk) { err("%s: operand sizes do not match", c->mn); return 0; }
        u32 w;
        if (t->fp) {
            if (d.sk == 'h') w = 0x5e400400u | ((u32)t->u << 29) | ((u32)t->a << 23) | ((u32)m.n << 16) | ((u32)(t->opc & 7) << 11) | ((u32)n.n << 5) | (u32)d.n;
            else if (d.sk == 's' || d.sk == 'd') w = 0x5e200400u | ((u32)t->u << 29) | ((u32)t->a << 23) | ((u32)(d.sk == 'd') << 22) | ((u32)m.n << 16) | ((u32)t->opc << 11) | ((u32)n.n << 5) | (u32)d.n;
            else { err("%s: the scalar form needs h, s or d registers", c->mn); return 0; }
        } else {
            int s = sv_esz(d.es);
            if (d.sk == 'q' || !sz_ok(t->sizes, d.es)) { err("%s: not available for %c registers", c->mn, d.sk); return 0; }
            if (scalar3_d_only(t) && s != 3) { err("%s: the scalar form only takes d registers", c->mn); return 0; }
            w = 0x5e200400u | ((u32)t->u << 29) | ((u32)s << 22) | ((u32)m.n << 16) | ((u32)t->opc << 11) | ((u32)n.n << 5) | (u32)d.n;
        }
        a64_W(c, w);
        return 1;
    }
    if (c->n != 3) {
        if (c->n == 2 && (!strcmp(c->mn, "add") || !strcmp(c->mn, "sub"))) return PASS;
        sv_count(c, 3, 3);
        return 0;
    }
    VReg d, n, m;
    int q;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &m)) return 0;
    if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1) || !sv_same(c, &d, &m, 0, 2)) return 0;
    int s = sv_esz(d.es);
    u32 w = 0x0e200400u | ((u32)q << 30) | ((u32)t->u << 29) | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n;
    if (t->fp) {
        if (d.es == 16) {
            w = 0x0e400400u | ((u32)q << 30) | ((u32)t->u << 29) | ((u32)t->a << 23) | ((u32)m.n << 16) | ((u32)(t->opc & 7) << 11) | ((u32)n.n << 5) | (u32)d.n;
            a64_W(c, w);
            return 1;
        }
        if (d.es != 32 && d.es != 64) { err("%s: floating-point vectors use .2s, .4s, .2d (or .4h, .8h)", c->mn); return 0; }
        if (d.es == 64 && !q) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
        w |= ((u32)t->a << 23) | ((u32)(d.es == 64) << 22) | ((u32)t->opc << 11);
        a64_W(c, w);
        return 1;
    }
    if (s > 3 || !sz_ok(t->sizes, d.es)) {
        err("%s: the .%s arrangement is not available for this instruction", c->mn, sv_arrname(d.es, d.lanes));
        return 0;
    }
    if (s == 3 && !q) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
    w |= ((u32)s << 22) | ((u32)t->opc << 11);
    a64_W(c, w);
    return 1;
}

/* Bitwise vector instructions (and, orr, eor, bic, bsl ...). */
int h_logic3(Ctx *c, const MnEnt *e)
{
    int size = e->a, u = e->b;
    if (!sv_has_vec(c)) return PASS;
    if ((!strcmp(c->mn, "orr") || !strcmp(c->mn, "bic")) && c->n >= 2) {
        VReg t1;
        if (!a64_p_vec(c->tok[1], &t1)) return h_orrbic(c, e);
    }
    if (c->n != 3) {
        if (c->n >= 2 && (!strcmp(c->mn, "orr") || !strcmp(c->mn, "bic"))) return PASS;
        sv_count(c, 3, 3);
        return 0;
    }
    VReg d, n, m;
    int q;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
    if (!a64_p_vec(c->tok[2], &m)) return PASS;
    if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1) || !sv_same(c, &d, &m, 0, 2)) return 0;
    if (d.es != 8) { err("%s: use the .8b or .16b arrangement", c->mn); return 0; }
    a64_W(c, 0x0e201c00u | ((u32)q << 30) | ((u32)u << 29) | ((u32)size << 22) | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

typedef struct { const char *n; int u; int opc; int kind; int a; } T2;

enum { K_SAME, K_PAIRL, K_NARROW, K_FP, K_FPSZ, K_BYTE, K_CVTN, K_CVTL, K_SHLL };

static const T2 t2[] = {
    { "rev64", 0, 0x00, K_SAME, 0 }, { "rev32", 1, 0x00, K_SAME, 0 }, { "rev16", 0, 0x01, K_SAME, 0 },
    { "saddlp", 0, 0x02, K_PAIRL, 0 }, { "uaddlp", 1, 0x02, K_PAIRL, 0 },
    { "suqadd", 0, 0x03, K_SAME, 0 }, { "usqadd", 1, 0x03, K_SAME, 0 },
    { "cls", 0, 0x04, K_SAME, 0 }, { "clz", 1, 0x04, K_SAME, 0 },
    { "cnt", 0, 0x05, K_BYTE, 0 }, { "not", 1, 0x05, K_BYTE, 0 }, { "mvn", 1, 0x05, K_BYTE, 0 }, { "rbit", 1, 0x05, K_BYTE, 1 },
    { "sadalp", 0, 0x06, K_PAIRL, 0 }, { "uadalp", 1, 0x06, K_PAIRL, 0 },
    { "sqabs", 0, 0x07, K_SAME, 0 }, { "sqneg", 1, 0x07, K_SAME, 0 },
    { "abs", 0, 0x0b, K_SAME, 0 }, { "neg", 1, 0x0b, K_SAME, 0 },
    { "xtn", 0, 0x12, K_NARROW, 0 }, { "xtn2", 0, 0x12, K_NARROW, 1 },
    { "sqxtn", 0, 0x14, K_NARROW, 0 }, { "sqxtn2", 0, 0x14, K_NARROW, 1 },
    { "uqxtn", 1, 0x14, K_NARROW, 0 }, { "uqxtn2", 1, 0x14, K_NARROW, 1 },
    { "sqxtun", 1, 0x12, K_NARROW, 0 }, { "sqxtun2", 1, 0x12, K_NARROW, 1 },
    { "shll", 1, 0x13, K_SHLL, 0 }, { "shll2", 1, 0x13, K_SHLL, 1 },
    { "fcvtn", 0, 0x16, K_CVTN, 0 }, { "fcvtn2", 0, 0x16, K_CVTN, 1 }, { "fcvtl", 0, 0x17, K_CVTL, 0 }, { "fcvtl2", 0, 0x17, K_CVTL, 1 },
    { "fcvtxn", 1, 0x16, K_CVTN, 0 }, { "fcvtxn2", 1, 0x16, K_CVTN, 1 },
    { "frintn", 0, 0x18, K_FP, 0 }, { "frintm", 0, 0x19, K_FP, 0 }, { "fcvtns", 0, 0x1a, K_FP, 0 }, { "fcvtms", 0, 0x1b, K_FP, 0 }, { "fcvtas", 0, 0x1c, K_FP, 0 }, { "scvtf", 0, 0x1d, K_FP, 0 },
    { "frintp", 0, 0x18, K_FP, 1 }, { "frintz", 0, 0x19, K_FP, 1 }, { "fcvtps", 0, 0x1a, K_FP, 1 }, { "fcvtzs", 0, 0x1b, K_FP, 1 }, { "urecpe", 0, 0x1c, K_FPSZ, 1 }, { "frecpe", 0, 0x1d, K_FP, 1 },
    { "frinta", 1, 0x18, K_FP, 0 }, { "frintx", 1, 0x19, K_FP, 0 }, { "fcvtnu", 1, 0x1a, K_FP, 0 }, { "fcvtmu", 1, 0x1b, K_FP, 0 }, { "fcvtau", 1, 0x1c, K_FP, 0 }, { "ucvtf", 1, 0x1d, K_FP, 0 },
    { "frinti", 1, 0x19, K_FP, 1 }, { "fcvtpu", 1, 0x1a, K_FP, 1 }, { "fcvtzu", 1, 0x1b, K_FP, 1 }, { "ursqrte", 1, 0x1c, K_FPSZ, 1 }, { "frsqrte", 1, 0x1d, K_FP, 1 },
    { "fabs", 0, 0x0f, K_FP, 1 }, { "fneg", 1, 0x0f, K_FP, 1 }, { "fsqrt", 1, 0x1f, K_FP, 1 },
    { "frint32z", 0, 0x1e, K_FP, 0 }, { "frint32x", 1, 0x1e, K_FP, 0 }, { "frint64z", 0, 0x1f, K_FP, 0 }, { "frint64x", 1, 0x1f, K_FP, 0 },
    { NULL, 0, 0, 0, 0 }
};

/* Finds a two-register table entry. */
static const T2 *t2_find(const char *mn)
{
    for (const T2 *t = t2; t->n; t++) if (!strcmp(t->n, mn)) return t;
    return NULL;
}

typedef struct { const char *n; int u; int opc; int fp; int a; } TZ;

static const TZ tz[] = {
    { "cmgt", 0, 0x08, 0, 0 }, { "cmeq", 0, 0x09, 0, 0 }, { "cmlt", 0, 0x0a, 0, 0 }, { "cmge", 1, 0x08, 0, 0 }, { "cmle", 1, 0x09, 0, 0 },
    { "fcmgt", 0, 0x0c, 1, 1 }, { "fcmeq", 0, 0x0d, 1, 1 }, { "fcmlt", 0, 0x0e, 1, 1 }, { "fcmge", 1, 0x0c, 1, 1 }, { "fcmle", 1, 0x0d, 1, 1 },
    { NULL, 0, 0, 0, 0 }
};

/* True if text is the constant #0. */
static int zero_ok(const char *s)
{
    s = a64_strip_hash(s);
    char *end;
    if (!*s) return 0;
    double d = strtod(s, &end);
    return !*end && end != s && d == 0;
}

/* The compare-with-zero forms (cmeq v, v, #0 ...). */
static int zero_form(Ctx *c, const TZ *z)
{
    VReg d, n;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
    if (!zero_ok(c->tok[2])) { err("%s: the last operand must be #0%s", c->mn, z->fp ? ".0" : ""); return 0; }
    if (d.scalar) {
        if (!n.scalar || n.sk != d.sk) { err("%s: operand sizes do not match", c->mn); return 0; }
        if (z->fp) {
            if (d.sk == 'h') { a64_W(c, 0x5ef8c800u | ((u32)z->u << 29) | ((u32)(z->opc - 0x0c) << 12) | ((u32)n.n << 5) | (u32)d.n); return 1; }
            if (d.sk != 's' && d.sk != 'd') { err("%s: needs h, s or d registers", c->mn); return 0; }
            a64_W(c, 0x5ea0c800u | ((u32)z->u << 29) | ((u32)(d.sk == 'd') << 22) | ((u32)(z->opc - 0x0c) << 12) | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (d.sk != 'd') { err("%s: the scalar form only takes d registers", c->mn); return 0; }
        a64_W(c, 0x5ee00800u | ((u32)z->u << 29) | ((u32)z->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    int q;
    if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1)) return 0;
    if (z->fp) {
        if (d.es == 16) { a64_W(c, 0x0ef8c800u | ((u32)q << 30) | ((u32)z->u << 29) | ((u32)(z->opc - 0x0c) << 12) | ((u32)n.n << 5) | (u32)d.n); return 1; }
        if (d.es != 32 && d.es != 64) { err("%s: floating-point vectors use .2s, .4s, .2d (or .4h, .8h)", c->mn); return 0; }
        if (d.es == 64 && !q) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
        a64_W(c, 0x0ea0c800u | ((u32)q << 30) | ((u32)z->u << 29) | ((u32)(d.es == 64) << 22) | ((u32)(z->opc - 0x0c) << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    int s = sv_esz(d.es);
    if (s > 3) { err("%s: bad element size", c->mn); return 0; }
    if (s == 3 && !q) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
    a64_W(c, 0x0e200800u | ((u32)q << 30) | ((u32)z->u << 29) | ((u32)s << 22) | ((u32)z->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

static const struct { const char *n; int u; int opc; int kind; int a; } sc2[] = {
    { "suqadd", 0, 0x03, 0, 0 }, { "usqadd", 1, 0x03, 0, 0 }, { "sqabs", 0, 0x07, 0, 0 }, { "sqneg", 1, 0x07, 0, 0 }, { "abs", 0, 0x0b, 0, 0 }, { "neg", 1, 0x0b, 0, 0 },
    { "sqxtn", 0, 0x14, 2, 0 }, { "uqxtn", 1, 0x14, 2, 0 }, { "sqxtun", 1, 0x12, 2, 0 },
    { "fcvtns", 0, 0x1a, 1, 0 }, { "fcvtms", 0, 0x1b, 1, 0 }, { "fcvtas", 0, 0x1c, 1, 0 }, { "scvtf", 0, 0x1d, 1, 0 },
    { "fcvtps", 0, 0x1a, 1, 1 }, { "fcvtzs", 0, 0x1b, 1, 1 }, { "frecpe", 0, 0x1d, 1, 1 }, { "frecpx", 0, 0x1f, 1, 1 },
    { "fcvtnu", 1, 0x1a, 1, 0 }, { "fcvtmu", 1, 0x1b, 1, 0 }, { "fcvtau", 1, 0x1c, 1, 0 }, { "ucvtf", 1, 0x1d, 1, 0 },
    { "fcvtpu", 1, 0x1a, 1, 1 }, { "fcvtzu", 1, 0x1b, 1, 1 }, { "frsqrte", 1, 0x1d, 1, 1 },
    { NULL, 0, 0, 0, 0 }
};

/* Two register instructions (abs, neg, not, cnt, rev, cmeq zero ...). */
int h_two(Ctx *c, const MnEnt *e)
{
    (void)e;
    const T2 *t = t2_find(c->mn);
    if (c->n == 3 && shift_known(c->mn)) return h_shift(c, e);
    const TZ *z = NULL;
    for (const TZ *k = tz; k->n; k++) if (!strcmp(k->n, c->mn)) z = k;
    int scalar = !sv_has_vec(c);
    if (z && c->n == 3) {
        return zero_form(c, z);
    }
    if (!t && !sc2[0].n) return PASS;
    int isscalar_only = 0;
    for (int i = 0; sc2[i].n; i++) if (!strcmp(sc2[i].n, c->mn)) isscalar_only = 1;
    if (!t && !isscalar_only) return PASS;
    if (scalar) {
        if (!sv_all_scalar(c) || c->n != 2) return PASS;
        VReg d, n;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
        for (int i = 0; sc2[i].n; i++) {
            if (strcmp(sc2[i].n, c->mn)) continue;
            if (sc2[i].kind == 0) {
                if (d.sk != 'd' || n.sk != 'd') { err("%s: the scalar form only takes d registers", c->mn); return 0; }
                a64_W(c, 0x5ee00800u | ((u32)sc2[i].u << 29) | ((u32)sc2[i].opc << 12) | ((u32)n.n << 5) | (u32)d.n);
                return 1;
            }
            if (sc2[i].kind == 2) {
                int sd = sv_esz(d.es), sn = sv_esz(n.es);
                if (sn != sd + 1 || sd > 2) { err("%s: the source must be one size wider than the destination", c->mn); return 0; }
                a64_W(c, 0x5e200800u | ((u32)sc2[i].u << 29) | ((u32)sd << 22) | ((u32)sc2[i].opc << 12) | ((u32)n.n << 5) | (u32)d.n);
                return 1;
            }
            if (d.sk != n.sk) { err("%s: operand sizes do not match", c->mn); return 0; }
            if (d.sk == 'h') {
                a64_W(c, 0x5e780800u | ((u32)sc2[i].u << 29) | ((u32)sc2[i].a << 23) | ((u32)sc2[i].opc << 12) | ((u32)n.n << 5) | (u32)d.n);
                return 1;
            }
            if (d.sk != 's' && d.sk != 'd') { err("%s: needs h, s or d registers", c->mn); return 0; }
            a64_W(c, 0x5e200800u | ((u32)sc2[i].u << 29) | ((u32)sc2[i].a << 23) | ((u32)(d.sk == 'd') << 22) | ((u32)sc2[i].opc << 12) | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (!strcmp(c->mn, "fcvtxn")) {
            if (d.sk != 's' || n.sk != 'd') { err("fcvtxn: the scalar form is s from d"); return 0; }
            a64_W(c, 0x7e616800u | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        return PASS;
    }
    if (!t) return PASS;
    if (c->n != 2 && !(c->n == 3 && (t->kind == K_SHLL))) {
        if (t->kind == K_FP && c->n == 3) return PASS;
        sv_count(c, 2, 2);
        return 0;
    }
    VReg d, n;
    int q;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
    u32 reg = ((u32)n.n << 5) | (u32)d.n;
    switch (t->kind) {
    case K_SAME:
    case K_BYTE: {
        if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1)) return 0;
        int s = sv_esz(d.es);
        int mask = SZ_ALL;
        if (!strcmp(t->n, "rev64") || !strcmp(t->n, "cls") || !strcmp(t->n, "clz")) mask = SZ_BHS;
        if (!strcmp(t->n, "rev32")) mask = SZ_B | SZ_H;
        if (!strcmp(t->n, "rev16")) mask = SZ_B;
        if (t->kind == K_BYTE) mask = SZ_B;
        if (s > 3 || !sz_ok(mask, d.es)) { err("%s: the .%s arrangement is not available here", c->mn, sv_arrname(d.es, d.lanes)); return 0; }
        if (s == 3 && !q) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
        if (t->kind == K_BYTE) s = t->a ? 1 : 0;
        a64_W(c, 0x0e200800u | ((u32)q << 30) | ((u32)t->u << 29) | ((u32)s << 22) | ((u32)t->opc << 12) | reg);
        return 1;
    }
    case K_PAIRL: {
        int qs, qd;
        if (!sv_arr(c, &n, 1, &qs) || !sv_arr(c, &d, 0, &qd)) return 0;
        if (qs != qd || d.es != n.es * 2 || d.lanes * 2 != n.lanes) { err("%s: the destination elements must be twice the size of the source (e.g. v0.4h, v1.8b)", c->mn); return 0; }
        int s = sv_esz(n.es);
        if (s > 2) { err("%s: bad source arrangement", c->mn); return 0; }
        a64_W(c, 0x0e200800u | ((u32)qs << 30) | ((u32)t->u << 29) | ((u32)s << 22) | ((u32)t->opc << 12) | reg);
        return 1;
    }
    case K_NARROW: {
        int qd, qs;
        if (!sv_arr(c, &n, 1, &qs) || !sv_arr(c, &d, 0, &qd)) return 0;
        if (!qs || d.es * 2 != n.es || d.lanes != n.lanes * (t->a ? 2 : 1)) { err("%s: the source must be a 128-bit vector with elements twice the destination size (e.g. v0.8b, v1.8h)", c->mn); return 0; }
        int part2 = t->a;
        if (part2 != qd) {
            err("%s: the destination must be a %s arrangement for this form", c->mn, part2 ? "128-bit" : "64-bit");
            return 0;
        }
        int s = sv_esz(d.es);
        if (s > 2) { err("%s: bad destination arrangement", c->mn); return 0; }
        a64_W(c, 0x0e200800u | ((u32)qd << 30) | ((u32)t->u << 29) | ((u32)s << 22) | ((u32)t->opc << 12) | reg);
        return 1;
    }
    case K_SHLL: {
        int qs, qd;
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        if (!sv_arr(c, &n, 1, &qs) || !sv_arr(c, &d, 0, &qd)) return 0;
        if (d.es != n.es * 2 || n.lanes != d.lanes * (t->a ? 2 : 1) || qs != t->a || qd != 1) { err("%s: bad arrangements (like %s v0.8h, v1.%s, #8)", c->mn, c->mn, t->a ? "16b" : "8b"); return 0; }
        i64 sh;
        if (!sv_imm(c, 2, &sh)) return 0;
        if (sh != n.es) { err("%s: the shift must equal the element size (%d)", c->mn, n.es); return 0; }
        int s = sv_esz(n.es);
        if (s > 2) { err("%s: bad source arrangement", c->mn); return 0; }
        a64_W(c, 0x0e200800u | ((u32)qs << 30) | ((u32)t->u << 29) | ((u32)s << 22) | ((u32)t->opc << 12) | reg);
        return 1;
    }
    case K_CVTN:
    case K_CVTL: {
        int qs, qd;
        if (!sv_arr(c, &d, 0, &qd) || !sv_arr(c, &n, 1, &qs)) return 0;
        int part2 = t->a;
        int ok = 0, sz = 0;
        if (t->kind == K_CVTN) {
            if (d.es == 16 && n.es == 32 && d.lanes == n.lanes * (part2 ? 2 : 1)) { ok = 1; sz = 0; }
            if (d.es == 32 && n.es == 64 && d.lanes == n.lanes * (part2 ? 2 : 1)) { ok = 1; sz = 1; }
            if (t->u && !(d.es == 32 && n.es == 64)) ok = 0;
            if (part2 != qd) ok = 0;
            if (!ok) { err("%s: bad arrangements (like %s v0.%s, v1.%s)", c->mn, c->mn, part2 ? "4s" : "2s", t->u ? "2d" : "2d"); return 0; }
        } else {
            if (n.es == 16 && d.es == 32 && n.lanes == d.lanes * (part2 ? 2 : 1)) { ok = 1; sz = 0; }
            if (n.es == 32 && d.es == 64 && n.lanes == d.lanes * (part2 ? 2 : 1)) { ok = 1; sz = 1; }
            if (part2 != qs || !qd) ok = 0;
            if (!ok) { err("%s: bad arrangements (like %s v0.4s, v1.%s)", c->mn, c->mn, part2 ? "8h" : "4h"); return 0; }
        }
        a64_W(c, 0x0e200800u | ((u32)(t->kind == K_CVTN ? qd : qs) << 30) | ((u32)t->u << 29) | ((u32)sz << 22) | ((u32)t->opc << 12) | reg);
        return 1;
    }
    case K_FP:
    case K_FPSZ: {
        if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1)) return 0;
        if (t->kind == K_FPSZ) {
            if (d.es != 32) { err("%s: only .2s and .4s are available", c->mn); return 0; }
            a64_W(c, 0x0ea00800u | ((u32)q << 30) | ((u32)t->u << 29) | ((u32)t->opc << 12) | reg);
            return 1;
        }
        if (d.es == 16) {
            if (t->opc >= 0x1e && !t->a) { err("%s: not available with half-precision elements", c->mn); return 0; }
            a64_W(c, 0x0e780800u | ((u32)q << 30) | ((u32)t->u << 29) | ((u32)t->a << 23) | ((u32)t->opc << 12) | reg);
            return 1;
        }
        if (d.es != 32 && d.es != 64) { err("%s: floating-point vectors use .2s, .4s, .2d (or .4h, .8h)", c->mn); return 0; }
        if (d.es == 64 && !q) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
        if (t->opc >= 0x1e && !t->a && d.es == 64) { err("%s: only .2s and .4s are available", c->mn); return 0; }
        a64_W(c, 0x0e200800u | ((u32)q << 30) | ((u32)t->u << 29) | ((u32)t->a << 23) | ((u32)(d.es == 64) << 22) | ((u32)t->opc << 12) | reg);
        return 1;
    }
    }
    return PASS;
}

typedef struct { const char *n; int u; int opc; int kind; } TA;

static const TA ta[] = {
    { "addv", 0, 0x1b, 0 }, { "saddlv", 0, 0x03, 1 }, { "uaddlv", 1, 0x03, 1 },
    { "smaxv", 0, 0x0a, 0 }, { "umaxv", 1, 0x0a, 0 }, { "sminv", 0, 0x1a, 0 }, { "uminv", 1, 0x1a, 0 },
    { "fmaxnmv", 1, 0x0c, 2 }, { "fminnmv", 1, 0x0c, 3 }, { "fmaxv", 1, 0x0f, 2 }, { "fminv", 1, 0x0f, 3 },
    { NULL, 0, 0, 0 }
};

/* Across lane reductions (addv, smaxv, uminv ...). */
int h_across(Ctx *c, const MnEnt *e)
{
    (void)e;
    const TA *t = NULL;
    for (const TA *k = ta; k->n; k++) if (!strcmp(k->n, c->mn)) t = k;
    if (!t) return PASS;
    if (c->n != 2) { sv_count(c, 2, 2); return 0; }
    VReg d, n;
    int q;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
    if (!d.scalar) { err("%s: the destination must be a scalar register (b, h, s or d)", c->mn); return 0; }
    if (!sv_arr(c, &n, 1, &q)) return 0;
    if (t->kind >= 2) {
        int a = t->kind == 3;
        if (n.es == 16) {
            if (d.sk != 'h') { err("%s: the destination must be an h register", c->mn); return 0; }
            a64_W(c, 0x0e300800u | ((u32)q << 30) | ((u32)a << 23) | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (n.es != 32 || !q || d.sk != 's') { err("%s: use v.4s with an s register (or v.4h/v.8h with an h register)", c->mn); return 0; }
        a64_W(c, 0x2e300800u | (1u << 30) | ((u32)a << 23) | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    int s = sv_esz(n.es);
    if (s > 2 || (s == 2 && !q)) { err("%s: the .%s arrangement is not available here", c->mn, sv_arrname(n.es, n.lanes)); return 0; }
    int want = t->kind == 1 ? s + 1 : s;
    if (sv_esz(d.es) != want) { err("%s: the destination must be a %c register for this source", c->mn, want == 0 ? 'b' : want == 1 ? 'h' : want == 2 ? 's' : 'd'); return 0; }
    a64_W(c, 0x0e300800u | ((u32)q << 30) | ((u32)t->u << 29) | ((u32)s << 22) | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* uzp, zip, trn. */
int h_permute(Ctx *c, const MnEnt *e)
{
    int opc = e->a;
    if (c->n != 3) { sv_count(c, 3, 3); return 0; }
    VReg d, n, m;
    int q;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &m)) return 0;
    if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1) || !sv_same(c, &d, &m, 0, 2)) return 0;
    int s = sv_esz(d.es);
    if (s > 3 || (s == 3 && !q)) { err("%s: the .%s arrangement is not available here", c->mn, sv_arrname(d.es, d.lanes)); return 0; }
    a64_W(c, 0x0e000800u | ((u32)q << 30) | ((u32)s << 22) | ((u32)m.n << 16) | ((u32)opc << 12) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* Vector ext. */
int h_ext(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (c->n != 4) { sv_count(c, 4, 4); return 0; }
    VReg d, n, m;
    int q;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &m)) return 0;
    if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1) || !sv_same(c, &d, &m, 0, 2)) return 0;
    if (d.es != 8) { err("ext: use the .8b or .16b arrangement"); return 0; }
    i64 imm;
    if (!sv_imm(c, 3, &imm)) return 0;
    if (imm < 0 || imm >= (q ? 16 : 8)) { err("ext: the index must be 0 to %d", q ? 15 : 7); return 0; }
    a64_W(c, 0x2e000000u | ((u32)q << 30) | ((u32)m.n << 16) | ((u32)imm << 11) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}


/* Parses a register list {v0.16b, v1.16b} with optional lane. */
static int parse_list(Ctx *c, int idx, VList *L, int allow_elem)
{
    char buf[200];
    const char *s = c->tok[idx];
    size_t len = strlen(s);
    if (s[0] != '{') { err("%s: operand %d: expected a register list like {v0.16b, v1.16b}, found `%s'", c->mn, idx + 1, s); return 0; }
    const char *close = strchr(s, '}');
    if (!close) { err("%s: operand %d: missing `}'", c->mn, idx + 1); return 0; }
    size_t inner = (size_t)(close - s - 1);
    if (inner >= sizeof buf) { err("%s: register list is too long", c->mn); return 0; }
    memcpy(buf, s + 1, inner);
    buf[inner] = 0;
    L->hasl = 0;
    L->lane = -1;
    const char *tail = close + 1;
    while (*tail == ' ') tail++;
    if (*tail == '[') {
        const char *e2 = strchr(tail, ']');
        if (!e2 || e2[1]) { err("%s: bad lane index after the register list", c->mn); return 0; }
        char nb[16];
        size_t nl = (size_t)(e2 - tail - 1);
        if (nl >= sizeof nb) { err("%s: bad lane index", c->mn); return 0; }
        memcpy(nb, tail + 1, nl);
        nb[nl] = 0;
        L->lane = atoi(nb);
        L->hasl = 1;
    } else if (*tail) { err("%s: junk after the register list", c->mn); return 0; }
    (void)len;
    L->n = 0;
    char *dash = strchr(buf, '-');
    if (dash && !strchr(buf, ',')) {
        *dash = 0;
        char *a = buf, *b = dash + 1;
        a64_trim(a);
        a64_trim(b);
        VReg x, y;
        if (!a64_p_vec(a, &x) || !a64_p_vec(b, &y)) { err("%s: bad register range", c->mn); return 0; }
        int cnt = ((y.n - x.n) & 31) + 1;
        if (cnt > 4) { err("%s: a register list holds at most 4 registers", c->mn); return 0; }
        for (int i = 0; i < cnt; i++) { L->v[i] = x; L->v[i].n = (x.n + i) & 31; }
        L->n = cnt;
    } else {
        char *p = buf;
        for (;;) {
            char *comma = strchr(p, ',');
            if (comma) *comma = 0;
            a64_trim(p);
            if (!*p) { err("%s: empty entry in the register list", c->mn); return 0; }
            if (L->n >= 4) { err("%s: a register list holds at most 4 registers", c->mn); return 0; }
            if (!a64_p_vec(p, &L->v[L->n]) || L->v[L->n].scalar) { err("%s: bad register `%s' in the list", c->mn, p); return 0; }
            L->n++;
            if (!comma) break;
            p = comma + 1;
        }
    }
    for (int i = 1; i < L->n; i++) {
        if (L->v[i].n != ((L->v[0].n + i) & 31)) { err("%s: the registers in the list must be consecutive", c->mn); return 0; }
        if (L->v[i].es != L->v[0].es || L->v[i].lanes != L->v[0].lanes) { err("%s: all registers in the list need the same arrangement", c->mn); return 0; }
    }
    if (!allow_elem && !L->v[0].has_arr) { err("%s: the registers need an arrangement like .16b", c->mn); return 0; }
    return 1;
}

/* Public wrapper for parse_list. */
int sv_parse_list(Ctx *c, int idx, void *L, int allow_elem)
{
    return parse_list(c, idx, (VList *)L, allow_elem);
}

/* tbl and tbx. */
int h_tbl(Ctx *c, const MnEnt *e)
{
    int op = e->a;
    if (c->n != 3) { sv_count(c, 3, 3); return 0; }
    VReg d, m;
    VList L;
    int q;
    if (!sv_vreg(c, 0, &d) || !parse_list(c, 1, &L, 0) || !sv_vreg(c, 2, &m)) return 0;
    if (!sv_arr(c, &d, 0, &q) || d.es != 8) { err("%s: the destination must be .8b or .16b", c->mn); return 0; }
    if (L.v[0].es != 8 || L.v[0].lanes != 16) { err("%s: the table registers must use .16b", c->mn); return 0; }
    if (m.es != 8 || m.lanes != d.lanes) { err("%s: the index register must match the destination arrangement", c->mn); return 0; }
    a64_W(c, 0x0e000000u | ((u32)q << 30) | ((u32)m.n << 16) | ((u32)(L.n - 1) << 13) | ((u32)op << 12) | ((u32)L.v[0].n << 5) | (u32)d.n);
    return 1;
}

typedef struct { const char *n; int u; int opc; int kind; } TD;

static const TD td[] = {
    { "saddl", 0, 0x0, 0 }, { "uaddl", 1, 0x0, 0 }, { "saddw", 0, 0x1, 1 }, { "uaddw", 1, 0x1, 1 },
    { "ssubl", 0, 0x2, 0 }, { "usubl", 1, 0x2, 0 }, { "ssubw", 0, 0x3, 1 }, { "usubw", 1, 0x3, 1 },
    { "addhn", 0, 0x4, 2 }, { "raddhn", 1, 0x4, 2 }, { "sabal", 0, 0x5, 0 }, { "uabal", 1, 0x5, 0 },
    { "subhn", 0, 0x6, 2 }, { "rsubhn", 1, 0x6, 2 }, { "sabdl", 0, 0x7, 0 }, { "uabdl", 1, 0x7, 0 },
    { "smlal", 0, 0x8, 0 }, { "umlal", 1, 0x8, 0 }, { "sqdmlal", 0, 0x9, 0 }, { "smlsl", 0, 0xa, 0 }, { "umlsl", 1, 0xa, 0 },
    { "sqdmlsl", 0, 0xb, 0 }, { "smull", 0, 0xc, 0 }, { "umull", 1, 0xc, 0 }, { "sqdmull", 0, 0xd, 0 }, { "pmull", 0, 0xe, 3 },
    { NULL, 0, 0, 0 }
};

/* Long, wide and narrow arithmetic (saddl, uabal, addhn ...). */
int h_diff(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (c->n == 3 && elem_operand(c, 2)) return h_byelem(c, e);
    char base[48];
    snprintf(base, sizeof base, "%s", c->mn);
    size_t bl = strlen(base);
    int part2 = 0;
    const TD *t = NULL;
    for (const TD *k = td; k->n; k++) if (!strcmp(k->n, base)) t = k;
    if (!t && bl > 1 && base[bl - 1] == '2') {
        base[bl - 1] = 0;
        for (const TD *k = td; k->n; k++) if (!strcmp(k->n, base)) t = k;
        part2 = 1;
    }
    if (!t) return PASS;
    if (!sv_has_vec(c)) {
        if (!sv_all_scalar(c) || c->n != 3) return PASS;
        if (t->opc != 0x9 && t->opc != 0xb && t->opc != 0xd) return PASS;
        VReg d, n, m;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &m)) return 0;
        int sn = sv_esz(n.es);
        if (n.sk != m.sk || sv_esz(d.es) != sn + 1 || (sn != 1 && sn != 2)) { err("%s: the scalar form is s from h, or d from s", c->mn); return 0; }
        a64_W(c, 0x5e200000u | ((u32)sn << 22) | ((u32)m.n << 16) | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (c->n != 3) { sv_count(c, 3, 3); return 0; }
    VReg d, n, m;
    int qd, qn, qm;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &m)) return 0;
    u32 reg = ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n;
    u32 w = 0x0e200000u | ((u32)t->u << 29) | ((u32)t->opc << 12) | reg;
    if (t->kind == 3) {
        if (d.has_arr && d.es == 128 && n.es == 64 && m.es == 64) {
            if (n.lanes != (part2 ? 2 : 1) || m.lanes != n.lanes || d.lanes != 1) { err("pmull: use %s", part2 ? "pmull2 v0.1q, v1.2d, v2.2d" : "pmull v0.1q, v1.1d, v2.1d"); return 0; }
            a64_W(c, w | ((u32)part2 << 30) | (3u << 22));
            return 1;
        }
        if (!sv_arr(c, &d, 0, &qd) || !sv_arr(c, &n, 1, &qn) || !sv_arr(c, &m, 2, &qm)) return 0;
        if (d.es != 16 || n.es != 8 || m.es != 8 || n.lanes != m.lanes || qn != part2 || !qd) { err("pmull: use %s", part2 ? "pmull2 v0.8h, v1.16b, v2.16b" : "pmull v0.8h, v1.8b, v2.8b"); return 0; }
        a64_W(c, w | ((u32)part2 << 30));
        return 1;
    }
    if (!sv_arr(c, &d, 0, &qd) || !sv_arr(c, &n, 1, &qn) || !sv_arr(c, &m, 2, &qm)) return 0;
    if (t->kind == 0) {
        if (!qd || d.es != n.es * 2 || n.es != m.es || n.lanes != m.lanes || d.lanes != (part2 ? n.lanes / 2 : n.lanes) || qn != part2 || qm != part2) {
            err("%s: bad arrangements (like %s v0.%s, v1.%s, v2.%s)", c->mn, c->mn, "4s", part2 ? "8h" : "4h", part2 ? "8h" : "4h");
            return 0;
        }
        if (d.lanes * d.es != 128) { err("%s: bad arrangements", c->mn); return 0; }
        int s = sv_esz(n.es);
        if (s > 2) { err("%s: bad element size", c->mn); return 0; }
        if ((t->opc == 0x9 || t->opc == 0xb || t->opc == 0xd) && s == 0) { err("%s: needs 16-bit or 32-bit source elements", c->mn); return 0; }
        a64_W(c, w | ((u32)part2 << 30) | ((u32)s << 22));
        return 1;
    }
    if (t->kind == 1) {
        if (!qd || !sv_same(c, &d, &n, 0, 1) || d.es != m.es * 2 || qm != part2 || d.lanes != m.lanes / (part2 ? 2 : 1) * (part2 ? 1 : 1)) {
            if (!(qd && d.es == m.es * 2 && d.lanes * d.es == 128 && qm == part2)) { err("%s: bad arrangements (like %s v0.4s, v1.4s, v2.%s)", c->mn, c->mn, part2 ? "8h" : "4h"); return 0; }
        }
        int s = sv_esz(m.es);
        if (s > 2) { err("%s: bad element size", c->mn); return 0; }
        a64_W(c, w | ((u32)part2 << 30) | ((u32)s << 22));
        return 1;
    }
    if (d.es * 2 != n.es || n.es != m.es || n.lanes != m.lanes || !qn || !qm || qd != part2) {
        err("%s: bad arrangements (like %s v0.%s, v1.8h, v2.8h)", c->mn, c->mn, part2 ? "16b" : "8b");
        return 0;
    }
    int s = sv_esz(d.es);
    if (s > 2) { err("%s: bad element size", c->mn); return 0; }
    a64_W(c, w | ((u32)part2 << 30) | ((u32)s << 22));
    return 1;
}

/* Routes an instruction with vector operands to its handler. */
int a64_simd_dispatch(Ctx *c)
{
    const MnEnt *e = a64_vlookup(c->mn);
    if (!e) { err("%s: this vector form is not supported", c->mn); return 0; }
    int r = e->fn(c, e);
    if (r < 0) { err("%s: unsupported operands", c->mn); return 0; }
    return r;
}

/* mov with vector operands (mov v0.16b, v1.16b and element moves). */
int a64_simd_mov(Ctx *c)
{
    const MnEnt *e = a64_vlookup("mov");
    if (!e) { err("mov: this vector form is not supported"); return 0; }
    int r = e->fn(c, e);
    if (r < 0) { err("mov: unsupported operands"); return 0; }
    return r;
}

#define E(n, f, a, b, c) { n, f, a, b, c }
#define ET3(n) E(n, h_three, 0, 0, 0)
#define ET2(n) E(n, h_two, 0, 0, 0)

const MnEnt a64_tab_simd[] = {
#include "a64_simd_tab.h"
    { NULL, NULL, 0, 0, 0 }
};
