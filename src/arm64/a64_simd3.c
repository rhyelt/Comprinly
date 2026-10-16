/* More NEON: shifts by immediate, by-element multiplies, structure loads and stores, crypto, dot product and complex arithmetic. */
#include "a64_simd.h"

enum { SH_R, SH_L, SH_N, SH_LL, SH_FX };

typedef struct { const char *n; int u; int opc; int kind; int scal; } TS;

static const TS ts[] = {
    { "sshr", 0, 0x00, SH_R, 1 }, { "ushr", 1, 0x00, SH_R, 1 }, { "ssra", 0, 0x02, SH_R, 1 }, { "usra", 1, 0x02, SH_R, 1 },
    { "srshr", 0, 0x04, SH_R, 1 }, { "urshr", 1, 0x04, SH_R, 1 }, { "srsra", 0, 0x06, SH_R, 1 }, { "ursra", 1, 0x06, SH_R, 1 },
    { "sri", 1, 0x08, SH_R, 1 }, { "shl", 0, 0x0a, SH_L, 1 }, { "sli", 1, 0x0a, SH_L, 1 },
    { "sqshl", 0, 0x0e, SH_L, 2 }, { "uqshl", 1, 0x0e, SH_L, 2 }, { "sqshlu", 1, 0x0c, SH_L, 2 },
    { "shrn", 0, 0x10, SH_N, 0 }, { "rshrn", 0, 0x11, SH_N, 0 },
    { "sqshrn", 0, 0x12, SH_N, 2 }, { "sqrshrn", 0, 0x13, SH_N, 2 }, { "sqshrun", 1, 0x10, SH_N, 2 }, { "sqrshrun", 1, 0x11, SH_N, 2 },
    { "uqshrn", 1, 0x12, SH_N, 2 }, { "uqrshrn", 1, 0x13, SH_N, 2 },
    { "sshll", 0, 0x14, SH_LL, 0 }, { "ushll", 1, 0x14, SH_LL, 0 },
    { "scvtf", 0, 0x1c, SH_FX, 2 }, { "ucvtf", 1, 0x1c, SH_FX, 2 }, { "fcvtzs", 0, 0x1f, SH_FX, 2 }, { "fcvtzu", 1, 0x1f, SH_FX, 2 },
    { NULL, 0, 0, 0, 0 }
};

/* Finds a shift-by-immediate table entry. */
static const TS *ts_find(const char *mn, int *p2)
{
    *p2 = 0;
    for (const TS *t = ts; t->n; t++) if (!strcmp(t->n, mn)) return t;
    size_t l = strlen(mn);
    if (l > 2 && mn[l - 1] == '2') {
        char b[24];
        snprintf(b, sizeof b, "%.*s", (int)l - 1, mn);
        for (const TS *t = ts; t->n; t++) if (!strcmp(t->n, b) && (t->kind == SH_N || t->kind == SH_LL)) { *p2 = 1; return t; }
    }
    return NULL;
}

/* True if a mnemonic is a shift-by-immediate instruction. */
int shift_known(const char *mn)
{
    int p;
    return ts_find(mn, &p) != NULL;
}

/* Vector shifts by immediate (shl, sshr, ushr, shrn, sli, sri ...). */
int h_shift(Ctx *c, const MnEnt *e)
{
    (void)e;
    int p2;
    const TS *t = ts_find(c->mn, &p2);
    if (!t) return PASS;
    if (c->n != 3) { sv_count(c, 3, 3); return 0; }
    GReg gg;
    if (a64_p_gpr(c->tok[0], &gg) || a64_p_gpr(c->tok[1], &gg)) return PASS;
    VReg d, n;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
    i64 sh;
    if (!sv_imm(c, 2, &sh)) return 0;
    u32 hd = ((u32)t->opc << 11) | ((u32)t->u << 29) | ((u32)n.n << 5) | (u32)d.n;
    if (d.scalar) {
        if (!n.scalar) { err("%s: operand 2 must be a scalar register", c->mn); return 0; }
        int es;
        u32 base = 0x5f000400u;
        if (t->kind == SH_N) {
            if (t->scal == 0) { err("%s: has no scalar form", c->mn); return 0; }
            if (n.es != d.es * 2 || d.es > 32) { err("%s: the source must be twice the destination size (like %s b1, h2, #3)", c->mn, c->mn); return 0; }
            es = d.es;
            if (sh < 1 || sh > es) { err("%s: the shift must be 1 to %d", c->mn, es); return 0; }
            a64_W(c, base | hd | (u32)((2 * es - sh) << 16));
            return 1;
        }
        if (t->kind == SH_LL) { err("%s: has no scalar form", c->mn); return 0; }
        if (d.sk != n.sk) { err("%s: operand sizes do not match", c->mn); return 0; }
        es = d.es;
        if (t->kind == SH_FX) {
            if (es != 16 && es != 32 && es != 64) { err("%s: needs h, s or d registers", c->mn); return 0; }
            if (sh < 1 || sh > es) { err("%s: the number of fraction bits must be 1 to %d", c->mn, es); return 0; }
            a64_W(c, base | hd | (u32)((2 * es - sh) << 16));
            return 1;
        }
        if (es != 64 && t->scal != 2) { err("%s: the scalar form only takes d registers", c->mn); return 0; }
        if (es > 64) { err("%s: bad register", c->mn); return 0; }
        if (t->kind == SH_R) {
            if (sh < 1 || sh > es) { err("%s: the shift must be 1 to %d", c->mn, es); return 0; }
            a64_W(c, base | hd | (u32)((2 * es - sh) << 16));
        } else {
            if (sh < 0 || sh >= es) { err("%s: the shift must be 0 to %d", c->mn, es - 1); return 0; }
            a64_W(c, base | hd | (u32)((es + sh) << 16));
        }
        return 1;
    }
    int qd, qn;
    if (!sv_arr(c, &d, 0, &qd) || !sv_arr(c, &n, 1, &qn)) return 0;
    u32 base = 0x0f000400u;
    if (t->kind == SH_N) {
        if (n.es != d.es * 2 || d.es > 32 || qn != 1 || qd != p2 || d.lanes != n.lanes * (p2 ? 2 : 1)) {
            err("%s: bad arrangements (like %s v0.%s, v1.8h, #3)", c->mn, c->mn, p2 ? "16b" : "8b");
            return 0;
        }
        if (sh < 1 || sh > d.es) { err("%s: the shift must be 1 to %d", c->mn, d.es); return 0; }
        a64_W(c, base | hd | ((u32)qd << 30) | (u32)((2 * d.es - sh) << 16));
        return 1;
    }
    if (t->kind == SH_LL) {
        if (d.es != n.es * 2 || n.es > 32 || qd != 1 || qn != p2 || n.lanes != d.lanes * (p2 ? 2 : 1)) {
            err("%s: bad arrangements (like %s v0.8h, v1.%s, #3)", c->mn, c->mn, p2 ? "16b" : "8b");
            return 0;
        }
        if (sh < 0 || sh >= n.es) { err("%s: the shift must be 0 to %d", c->mn, n.es - 1); return 0; }
        a64_W(c, base | hd | ((u32)p2 << 30) | (u32)((n.es + sh) << 16));
        return 1;
    }
    if (!sv_same(c, &d, &n, 0, 1)) return 0;
    int es = d.es;
    if (es > 64) { err("%s: bad element size", c->mn); return 0; }
    if (es == 64 && !qd) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
    if (t->kind == SH_FX) {
        if (es == 8) { err("%s: use .4h, .8h, .2s, .4s or .2d", c->mn); return 0; }
        if (sh < 1 || sh > es) { err("%s: the number of fraction bits must be 1 to %d", c->mn, es); return 0; }
        a64_W(c, base | hd | ((u32)qd << 30) | (u32)((2 * es - sh) << 16));
        return 1;
    }
    if (t->kind == SH_R) {
        if (sh < 1 || sh > es) { err("%s: the shift must be 1 to %d", c->mn, es); return 0; }
        a64_W(c, base | hd | ((u32)qd << 30) | (u32)((2 * es - sh) << 16));
    } else {
        if (sh < 0 || sh >= es) { err("%s: the shift must be 0 to %d", c->mn, es - 1); return 0; }
        a64_W(c, base | hd | ((u32)qd << 30) | (u32)((es + sh) << 16));
    }
    return 1;
}

typedef struct { const char *n; int u; int opc; int kind; } TE;

enum { EL_SAME, EL_LONG, EL_FP };

static const TE te[] = {
    { "mul", 0, 0x8, EL_SAME }, { "mla", 1, 0x0, EL_SAME }, { "mls", 1, 0x4, EL_SAME },
    { "sqdmulh", 0, 0xc, EL_SAME }, { "sqrdmulh", 0, 0xd, EL_SAME }, { "sqrdmlah", 1, 0xd, EL_SAME }, { "sqrdmlsh", 1, 0xf, EL_SAME },
    { "smlal", 0, 0x2, EL_LONG }, { "umlal", 1, 0x2, EL_LONG }, { "smlsl", 0, 0x6, EL_LONG }, { "umlsl", 1, 0x6, EL_LONG },
    { "smull", 0, 0xa, EL_LONG }, { "umull", 1, 0xa, EL_LONG },
    { "sqdmlal", 0, 0x3, EL_LONG }, { "sqdmlsl", 0, 0x7, EL_LONG }, { "sqdmull", 0, 0xb, EL_LONG },
    { "fmla", 0, 0x1, EL_FP }, { "fmls", 0, 0x5, EL_FP }, { "fmul", 0, 0x9, EL_FP }, { "fmulx", 1, 0x9, EL_FP },
    { NULL, 0, 0, 0 }
};

/* Finds a by-element table entry. */
static const TE *te_find(const char *mn, int *p2)
{
    *p2 = 0;
    for (const TE *t = te; t->n; t++) if (!strcmp(t->n, mn)) return t;
    size_t l = strlen(mn);
    if (l > 2 && mn[l - 1] == '2') {
        char b[24];
        snprintf(b, sizeof b, "%.*s", (int)l - 1, mn);
        for (const TE *t = te; t->n; t++) if (!strcmp(t->n, b) && t->kind == EL_LONG) { *p2 = 1; return t; }
    }
    return NULL;
}

/* True if operand i is an indexed element. */
int elem_operand(Ctx *c, int i)
{
    VReg m;
    return i < c->n && a64_p_vec(c->tok[i], &m) && !m.scalar && m.lane >= 0;
}

/* Computes the H, L and M bits for an element index. */
static int elem_fields(Ctx *c, const VReg *m, int es, u32 *bits)
{
    int idx = m->lane;
    if (m->es != es) { err("%s: the element register must be .%c", c->mn, es == 16 ? 'h' : es == 32 ? 's' : 'd'); return 0; }
    if (es == 16) {
        if (m->n > 15) { err("%s: with .h elements the element register must be v0 to v15", c->mn); return 0; }
        if (idx > 7) { err("%s: the element index must be 0 to 7", c->mn); return 0; }
        *bits = ((u32)(idx & 1) << 20) | ((u32)((idx >> 1) & 1) << 21) | ((u32)(idx >> 2) << 11) | ((u32)m->n << 16);
        return 1;
    }
    if (es == 32) {
        if (idx > 3) { err("%s: the element index must be 0 to 3", c->mn); return 0; }
        *bits = ((u32)(idx & 1) << 21) | ((u32)(idx >> 1) << 11) | ((u32)(m->n & 15) << 16) | ((u32)(m->n >> 4) << 20);
        return 1;
    }
    if (idx > 1) { err("%s: the element index must be 0 or 1", c->mn); return 0; }
    *bits = ((u32)idx << 11) | ((u32)(m->n & 15) << 16) | ((u32)(m->n >> 4) << 20);
    return 1;
}

/* Multiplies and multiply-accumulates with an element operand. */
int h_byelem(Ctx *c, const MnEnt *e)
{
    (void)e;
    int p2;
    const TE *t = te_find(c->mn, &p2);
    if (!t) return PASS;
    if (c->n != 3) { sv_count(c, 3, 3); return 0; }
    VReg d, n, m;
    if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &m)) return 0;
    if (m.lane < 0 || m.scalar) {
        if (!strcmp(c->mn, "sqrdmlah") || !strcmp(c->mn, "sqrdmlsh")) return h_dot(c, e);
        err("%s: operand 3 must be a vector element like v2.s[1]", c->mn);
        return 0;
    }
    u32 f;
    if (d.scalar) {
        if (!n.scalar) { err("%s: operand 2 must be a scalar register", c->mn); return 0; }
        if (t->kind == EL_LONG) {
            if (t->opc != 0x3 && t->opc != 0x7 && t->opc != 0xb) { err("%s: has no scalar form", c->mn); return 0; }
            int s = n.es == 16 ? 1 : n.es == 32 ? 2 : -1;
            if (s < 0 || d.es != n.es * 2) { err("%s: the scalar form is s from h, or d from s", c->mn); return 0; }
            if (!elem_fields(c, &m, n.es, &f)) return 0;
            a64_W(c, 0x5f000000u | ((u32)s << 22) | f | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (t->kind == EL_SAME) {
            if (t->opc != 0xc && t->opc != 0xd && t->opc != 0xf) { err("%s: has no scalar form", c->mn); return 0; }
            if (d.sk != n.sk || (d.es != 16 && d.es != 32)) { err("%s: the scalar form takes h or s registers", c->mn); return 0; }
            if (t->opc == 0xd && t->u) { }
            if (!elem_fields(c, &m, d.es, &f)) return 0;
            a64_W(c, 0x5f000000u | ((u32)t->u << 29) | ((u32)(d.es == 16 ? 1 : 2) << 22) | f | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (d.sk != n.sk) { err("%s: operand sizes do not match", c->mn); return 0; }
        if (d.es == 16) {
            if (!elem_fields(c, &m, 16, &f)) return 0;
            a64_W(c, 0x5f000000u | ((u32)t->opc << 12) | ((u32)t->u << 29) | f | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (d.es != 32 && d.es != 64) { err("%s: needs h, s or d registers", c->mn); return 0; }
        if (!elem_fields(c, &m, d.es, &f)) return 0;
        a64_W(c, 0x5f800000u | ((u32)t->u << 29) | ((u32)(d.es == 64) << 22) | f | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    int qd, qn;
    if (!sv_arr(c, &d, 0, &qd) || !sv_arr(c, &n, 1, &qn)) return 0;
    if (t->kind == EL_LONG) {
        if (d.es != n.es * 2 || (n.es != 16 && n.es != 32) || qn != p2 || qd != 1 || n.lanes != d.lanes * (p2 ? 2 : 1)) {
            err("%s: bad arrangements (like %s v0.4s, v1.%s, v2.h[1])", c->mn, c->mn, p2 ? "8h" : "4h");
            return 0;
        }
        if (!elem_fields(c, &m, n.es, &f)) return 0;
        a64_W(c, 0x0f000000u | ((u32)p2 << 30) | ((u32)t->u << 29) | ((u32)(n.es == 16 ? 1 : 2) << 22) | f | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!sv_same(c, &d, &n, 0, 1)) return 0;
    if (t->kind == EL_SAME) {
        if (d.es != 16 && d.es != 32) { err("%s: use .4h, .8h, .2s or .4s", c->mn); return 0; }
        if (!elem_fields(c, &m, d.es, &f)) return 0;
        a64_W(c, 0x0f000000u | ((u32)qd << 30) | ((u32)t->u << 29) | ((u32)(d.es == 16 ? 1 : 2) << 22) | f | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (d.es == 16) {
        if (!elem_fields(c, &m, 16, &f)) return 0;
        a64_W(c, 0x0f000000u | ((u32)qd << 30) | ((u32)t->u << 29) | f | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (d.es != 32 && d.es != 64) { err("%s: floating-point vectors use .2s, .4s, .2d (or .4h, .8h)", c->mn); return 0; }
    if (d.es == 64 && !qd) { err("%s: .1d is not valid, use .2d", c->mn); return 0; }
    if (!elem_fields(c, &m, d.es, &f)) return 0;
    a64_W(c, 0x0f800000u | ((u32)qd << 30) | ((u32)t->u << 29) | ((u32)(d.es == 64) << 22) | f | ((u32)t->opc << 12) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* True if a mnemonic has a by-element form. */
int byelem_known(const char *mn)
{
    int p;
    return te_find(mn, &p) != NULL;
}

/* Parses the [base] or [base], post operand of a structure load or store. */
static int ls_mem(Ctx *c, int idx, GReg *rn)
{
    const char *m = c->tok[idx];
    while (*m == ' ') m++;
    if (*m != '[') { err("%s: operand %d: expected a memory operand like [x0], found `%s'", c->mn, idx + 1, c->tok[idx]); return 0; }
    char b[40];
    const char *close = strchr(m, ']');
    if (!close || close[1]) { err("%s: bad memory operand `%s' (only [xn] is allowed here)", c->mn, c->tok[idx]); return 0; }
    size_t l = (size_t)(close - m - 1);
    if (l >= sizeof b) { err("%s: bad memory operand", c->mn); return 0; }
    memcpy(b, m + 1, l);
    b[l] = 0;
    a64_trim(b);
    char *comma = strchr(b, ',');
    if (comma) {
        char *rest = comma + 1;
        *comma = 0;
        a64_trim(b);
        a64_trim(rest);
        const char *z = a64_strip_hash(rest);
        if (strcmp(z, "0")) { err("%s: structure loads have no offset inside the brackets, use post-index (like [x0], #16)", c->mn); return 0; }
    }
    if (!a64_p_gpr(b, rn) || !rn->w64) { err("%s: the base must be an x register or sp, found `%s'", c->mn, b); return 0; }
    return 1;
}

/* ld1 to ld4, st1 to st4, ld1r and lane forms. */
int h_ldst_s(Ctx *c, const MnEnt *e)
{
    int N = e->a, store = e->b, rep = e->c;
    if (c->n < 2 || c->n > 3) { sv_count(c, 2, 3); return 0; }
    VList L;
    if (!sv_parse_list(c, 0, &L, 1)) return 0;
    GReg rn;
    if (!ls_mem(c, 1, &rn)) return 0;
    if (L.n != N && !(N == 1 && L.n >= 1 && L.n <= 4 && !L.hasl && !rep)) {
        err("%s: expects %d register%s in the list, found %d", c->mn, N, N == 1 ? "" : "s", L.n);
        return 0;
    }
    int post = 0;
    u32 rm = 31;
    i64 immv = 0;
    int immpost = 0;
    if (c->n == 3) {
        GReg r;
        post = 1;
        if (a64_p_gpr(c->tok[2], &r)) {
            if (!r.w64 || r.n == 31) { err("%s: the post-index register must be x0 to x30", c->mn); return 0; }
            rm = (u32)r.n;
        } else {
            if (!sv_imm(c, 2, &immv)) return 0;
            immpost = 1;
        }
    }
    int nr = L.n;
    VReg v0 = L.v[0];
    u32 word;
    if (L.hasl || rep) {
        if (!rep && !L.hasl) { err("%s: this form needs a lane index like {v0.s}[1]", c->mn); return 0; }
        if (rep && L.hasl) { err("%s: the replicate form has no lane index", c->mn); return 0; }
        if (v0.es > 64) { err("%s: bad element size", c->mn); return 0; }
        int bytes = nr * (v0.es / 8);
        if (immpost) {
            if (immv != bytes) { err("%s: the post-index immediate must be %d for this register list", c->mn, bytes); return 0; }
        }
        if (rep) {
            int q;
            if (!sv_arr(c, &v0, 0, &q)) return 0;
            int sz = sv_esz(v0.es);
            word = 0x0d40c000u | ((u32)q << 30) | ((u32)((N & 1) == 0) << 21) | ((u32)(N >= 3 ? 0x2000 : 0)) | ((u32)sz << 10);
        } else {
            int idx = L.lane, q, S, sz;
            int es = v0.es;
            if (idx < 0 || idx >= 128 / es) { err("%s: the lane index must be 0 to %d", c->mn, 128 / es - 1); return 0; }
            u32 opc;
            if (es == 8) { q = idx >> 3; S = (idx >> 2) & 1; sz = idx & 3; opc = 0; }
            else if (es == 16) { q = idx >> 2; S = (idx >> 1) & 1; sz = (idx & 1) << 1; opc = 2; }
            else if (es == 32) { q = idx >> 1; S = idx & 1; sz = 0; opc = 4; }
            else { q = idx; S = 0; sz = 1; opc = 4; }
            if (N >= 3) opc |= 1;
            word = 0x0d000000u | ((u32)q << 30) | ((u32)(!store) << 22) | ((u32)((N & 1) == 0) << 21) | (opc << 13) | ((u32)S << 12) | ((u32)sz << 10);
        }
    } else {
        int q;
        if (!v0.has_arr) { err("%s: the registers need an arrangement like .16b", c->mn); return 0; }
        if (!sv_arr(c, &v0, 0, &q)) return 0;
        int sz = sv_esz(v0.es);
        if (sz > 3) { err("%s: bad element size", c->mn); return 0; }
        if (N > 1 && sz == 3 && !q) { err("%s: .1d is only available for ld1/st1", c->mn); return 0; }
        static const u32 op1[] = { 0, 0x7, 0xa, 0x6, 0x2 };
        u32 opcode = N == 1 ? op1[nr] : N == 2 ? 0x8u : N == 3 ? 0x4u : 0x0u;
        int total = nr * (q ? 16 : 8);
        if (immpost && immv != total) { err("%s: the post-index immediate must be %d for this register list", c->mn, total); return 0; }
        word = 0x0c000000u | ((u32)q << 30) | ((u32)(!store) << 22) | (opcode << 12) | ((u32)sz << 10);
    }
    if (post) word |= (1u << 23) | (rm << 16);
    a64_W(c, word | ((u32)rn.n << 5) | (u32)v0.n);
    return 1;
}

typedef struct { const char *n; u32 base; char pat; } TC;

static const TC tcr[] = {
    { "aese", 0x4e284800u, 'A' }, { "aesd", 0x4e285800u, 'A' }, { "aesmc", 0x4e286800u, 'A' }, { "aesimc", 0x4e287800u, 'A' },
    { "sha1c", 0x5e000000u, 'Q' }, { "sha1p", 0x5e001000u, 'Q' }, { "sha1m", 0x5e002000u, 'Q' }, { "sha1su0", 0x5e003000u, '4' },
    { "sha256h", 0x5e004000u, 'Q' }, { "sha256h2", 0x5e005000u, 'Q' }, { "sha256su1", 0x5e006000u, '4' },
    { "sha1h", 0x5e280800u, 'S' }, { "sha1su1", 0x5e281800u, 'F' }, { "sha256su0", 0x5e282800u, 'F' },
    { "sha512h", 0xce608000u, 'H' }, { "sha512h2", 0xce608400u, 'H' }, { "sha512su1", 0xce608800u, '2' }, { "rax1", 0xce608c00u, '2' }, { "sha512su0", 0xcec08000u, 'D' },
    { "sm3partw1", 0xce60c000u, '4' }, { "sm3partw2", 0xce60c400u, '4' }, { "sm3ss1", 0xce400000u, 'G' },
    { "sm3tt1a", 0xce408000u, 'T' }, { "sm3tt1b", 0xce408400u, 'T' }, { "sm3tt2a", 0xce408800u, 'T' }, { "sm3tt2b", 0xce408c00u, 'T' },
    { "sm4e", 0xcec08400u, 'F' }, { "sm4ekey", 0xce60c800u, '4' },
    { "eor3", 0xce000000u, 'B' }, { "bcax", 0xce200000u, 'B' }, { "xar", 0xce800000u, 'X' },
    { NULL, 0, 0 }
};

/* Name of a crypto table row. */
const char *crypto_names(int i)
{
    return tcr[i].n;
}

/* Operand i must have a given arrangement. */
static int need_arr(Ctx *c, int i, VReg *v, int es, int lanes)
{
    if (!sv_vreg(c, i, v)) return 0;
    if (v->scalar || !v->has_arr || v->lane >= 0 || v->es != es || v->lanes != lanes) {
        err("%s: operand %d must be v%d.%s", c->mn, i + 1, v->n, sv_arrname(es, lanes));
        return 0;
    }
    return 1;
}

/* AES, SHA and SM3/SM4 instructions. */
int h_crypto(Ctx *c, const MnEnt *e)
{
    (void)e;
    const TC *t = NULL;
    for (const TC *k = tcr; k->n; k++) if (!strcmp(k->n, c->mn)) t = k;
    if (!t) return PASS;
    VReg d, n, m, a;
    u32 w = t->base;
    switch (t->pat) {
    case 'A':
        if (c->n != 2) { sv_count(c, 2, 2); return 0; }
        if (!need_arr(c, 0, &d, 8, 16) || !need_arr(c, 1, &n, 8, 16)) return 0;
        a64_W(c, w | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    case 'Q':
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
        if (!strcmp(c->mn, "sha1c") || !strcmp(c->mn, "sha1p") || !strcmp(c->mn, "sha1m")) {
            if (d.sk != 'q' || n.sk != 's') { err("%s: the form is %s q0, s1, v2.4s", c->mn, c->mn); return 0; }
        } else if (d.sk != 'q' || n.sk != 'q') { err("%s: the form is %s q0, q1, v2.4s", c->mn, c->mn); return 0; }
        if (!need_arr(c, 2, &m, 32, 4)) return 0;
        a64_W(c, w | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    case 'S':
        if (c->n != 2) { sv_count(c, 2, 2); return 0; }
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
        if (d.sk != 's' || n.sk != 's') { err("sha1h: the form is sha1h s0, s1"); return 0; }
        a64_W(c, w | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    case '4':
    case '2': {
        int es = t->pat == '4' ? 32 : 64, ln = t->pat == '4' ? 4 : 2;
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        if (!need_arr(c, 0, &d, es, ln) || !need_arr(c, 1, &n, es, ln) || !need_arr(c, 2, &m, es, ln)) {
            if (!strcmp(c->mn, "sha1su0") || !strcmp(c->mn, "sha256su1")) return 0;
            return 0;
        }
        a64_W(c, w | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    case 'F':
    case 'D': {
        int es = t->pat == 'F' ? 32 : 64, ln = t->pat == 'F' ? 4 : 2;
        if (c->n != 2) { sv_count(c, 2, 2); return 0; }
        if (!need_arr(c, 0, &d, es, ln) || !need_arr(c, 1, &n, es, ln)) return 0;
        a64_W(c, w | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    case 'H':
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || d.sk != 'q' || n.sk != 'q') { err("%s: the form is %s q0, q1, v2.2d", c->mn, c->mn); return 0; }
        if (!need_arr(c, 2, &m, 64, 2)) return 0;
        a64_W(c, w | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    case 'G':
    case 'B': {
        int es = t->pat == 'G' ? 32 : 8, ln = t->pat == 'G' ? 4 : 16;
        if (c->n != 4) { sv_count(c, 4, 4); return 0; }
        if (!need_arr(c, 0, &d, es, ln) || !need_arr(c, 1, &n, es, ln) || !need_arr(c, 2, &m, es, ln) || !need_arr(c, 3, &a, es, ln)) return 0;
        a64_W(c, w | ((u32)m.n << 16) | ((u32)a.n << 10) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    case 'T': {
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        if (!need_arr(c, 0, &d, 32, 4) || !need_arr(c, 1, &n, 32, 4)) return 0;
        if (!sv_vreg(c, 2, &m)) return 0;
        if (m.es != 32 || m.lane < 0 || m.lane > 3) { err("%s: operand 3 must be an element like v2.s[1]", c->mn); return 0; }
        a64_W(c, w | ((u32)m.n << 16) | ((u32)m.lane << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    case 'X': {
        if (c->n != 4) { sv_count(c, 4, 4); return 0; }
        if (!need_arr(c, 0, &d, 64, 2) || !need_arr(c, 1, &n, 64, 2) || !need_arr(c, 2, &m, 64, 2)) return 0;
        i64 im;
        if (!sv_imm(c, 3, &im)) return 0;
        if (im < 0 || im > 63) { err("xar: the rotate must be 0 to 63"); return 0; }
        a64_W(c, w | ((u32)m.n << 16) | ((u32)im << 10) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    }
    return PASS;
}


/* Parses the rotation operand of fcadd and fcmla. */
static int rotv(Ctx *c, int i, int *rot, int fcadd)
{
    i64 r;
    if (!sv_imm(c, i, &r)) return 0;
    if (fcadd) {
        if (r != 90 && r != 270) { err("%s: the rotation must be 90 or 270", c->mn); return 0; }
        *rot = r == 270;
        return 1;
    }
    if (r != 0 && r != 90 && r != 180 && r != 270) { err("%s: the rotation must be 0, 90, 180 or 270", c->mn); return 0; }
    *rot = (int)(r / 90);
    return 1;
}

/* True if a register has the given arrangement. */
static int arr_is(const VReg *v, int es, int lanes)
{
    return !v->scalar && v->has_arr && v->lane < 0 && v->es == es && v->lanes == lanes;
}

/* Reports the correct form of a dot product instruction. */
static int hd_bad(Ctx *c, const char *form)
{
    err("%s: the form is %s", c->mn, form);
    return 0;
}

/* sdot, udot, usdot, fmla by element for half, bfdot and similar. */
int h_dot(Ctx *c, const MnEnt *e)
{
    (void)e;
    const char *m = c->mn;
    VReg d, n, v;
    if (!strcmp(m, "sqrdmlah") || !strcmp(m, "sqrdmlsh")) {
        u32 b = !strcmp(m, "sqrdmlah") ? 0x8400u : 0x8c00u;
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &v)) return 0;
        if (d.scalar) {
            if (!n.scalar || !v.scalar || d.sk != n.sk || d.sk != v.sk || (d.es != 16 && d.es != 32)) return hd_bad(c, "sqrdmlah h0, h1, h2 (or s registers)");
            a64_W(c, 0x7e000000u | b | ((u32)(d.es == 16 ? 1 : 2) << 22) | ((u32)v.n << 16) | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        int q;
        if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1) || !sv_same(c, &d, &v, 0, 2)) return 0;
        if (d.es != 16 && d.es != 32) { err("%s: use .4h, .8h, .2s or .4s", c->mn); return 0; }
        a64_W(c, 0x2e000000u | b | ((u32)q << 30) | ((u32)(d.es == 16 ? 1 : 2) << 22) | ((u32)v.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!strcmp(m, "bfcvtn") || !strcmp(m, "bfcvtn2")) {
        int p2 = m[6] == '2';
        if (c->n != 2) { sv_count(c, 2, 2); return 0; }
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
        if (!arr_is(&d, 16, p2 ? 8 : 4) || !arr_is(&n, 32, 4)) return hd_bad(c, p2 ? "bfcvtn2 v0.8h, v1.4s" : "bfcvtn v0.4h, v1.4s");
        a64_W(c, 0x0ea16800u | ((u32)p2 << 30) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!strcmp(m, "smmla") || !strcmp(m, "ummla") || !strcmp(m, "usmmla") || !strcmp(m, "bfmmla")) {
        u32 b = !strcmp(m, "smmla") ? 0x4e80a400u : !strcmp(m, "ummla") ? 0x6e80a400u : !strcmp(m, "usmmla") ? 0x4e80ac00u : 0x6e40ec00u;
        int bf = m[0] == 'b';
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        VReg mm;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &mm)) return 0;
        if (!arr_is(&d, 32, 4) || !arr_is(&n, bf ? 16 : 8, bf ? 8 : 16) || !arr_is(&mm, bf ? 16 : 8, bf ? 8 : 16)) return hd_bad(c, bf ? "bfmmla v0.4s, v1.8h, v2.8h" : "smmla v0.4s, v1.16b, v2.16b");
        a64_W(c, b | ((u32)mm.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!strcmp(m, "fcadd")) {
        if (c->n != 4) { sv_count(c, 4, 4); return 0; }
        VReg mm;
        int q, rot;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &mm)) return 0;
        if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1) || !sv_same(c, &d, &mm, 0, 2)) return 0;
        if (d.es < 16 || (d.es == 64 && !q)) return hd_bad(c, "fcadd v0.4s, v1.4s, v2.4s, #90");
        if (!rotv(c, 3, &rot, 1)) return 0;
        a64_W(c, 0x2e00e400u | ((u32)q << 30) | ((u32)sv_esz(d.es) << 22) | ((u32)mm.n << 16) | ((u32)rot << 12) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!strcmp(m, "fcmla")) {
        if (c->n != 4) { sv_count(c, 4, 4); return 0; }
        int q, rot;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &v)) return 0;
        if (!sv_arr(c, &d, 0, &q) || !sv_same(c, &d, &n, 0, 1)) return 0;
        if (d.es < 16 || (d.es == 64 && !q)) return hd_bad(c, "fcmla v0.4s, v1.4s, v2.4s, #90");
        if (!rotv(c, 3, &rot, 0)) return 0;
        if (v.lane >= 0) {
            u32 f;
            if (v.es != d.es) { err("fcmla: the element size must match the destination"); return 0; }
            if (d.es == 16) {
                int mx = q ? 4 : 2;
                if (v.lane >= mx) { err("fcmla: the element index must be 0 to %d", mx - 1); return 0; }
                f = (v.lane & 1 ? 0x200000u : 0) | (v.lane & 2 ? 0x800u : 0) | ((u32)v.n << 16);
            } else if (d.es == 32) {
                if (!q) { err("fcmla: by element needs .4s (or .8h, .4h)"); return 0; }
                if (v.lane > 1) { err("fcmla: the element index must be 0 or 1"); return 0; }
                f = ((u32)v.lane << 11) | ((u32)v.n << 16);
            } else { err("fcmla: no by-element form for .2d"); return 0; }
            a64_W(c, 0x2f001000u | ((u32)q << 30) | ((u32)sv_esz(d.es) << 22) | f | ((u32)rot << 13) | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (!sv_same(c, &d, &v, 0, 2)) return 0;
        a64_W(c, 0x2e00c400u | ((u32)q << 30) | ((u32)sv_esz(d.es) << 22) | ((u32)v.n << 16) | ((u32)rot << 11) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!strcmp(m, "fmlal") || !strcmp(m, "fmlsl") || !strcmp(m, "fmlal2") || !strcmp(m, "fmlsl2")) {
        int sub = m[3] == 's', two = m[5] == '2';
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        int q;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &v)) return 0;
        if (!sv_arr(c, &d, 0, &q) || d.es != 32 || !arr_is(&n, 16, d.lanes * 1)) return hd_bad(c, "fmlal v0.2s, v1.2h, v2.2h (or .4s with .4h)");
        if (v.lane >= 0) {
            if (v.es != 16 || v.n > 15 || v.lane > 7) { err("%s: the element must be v0 to v15 .h[0..7]", c->mn); return 0; }
            u32 opc = (sub ? 0x4u : 0u) | (two ? 0x8u : 0u);
            u32 f = (v.lane & 1 ? 0x100000u : 0) | (v.lane & 2 ? 0x200000u : 0) | (v.lane & 4 ? 0x800u : 0) | ((u32)v.n << 16);
            a64_W(c, 0x0f800000u | ((u32)q << 30) | ((u32)two << 29) | (opc << 12) | f | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (!arr_is(&v, 16, d.lanes)) return hd_bad(c, "fmlal v0.2s, v1.2h, v2.2h");
        u32 base = two ? 0x2e20cc00u : 0x0e20ec00u;
        if (sub) base |= 0x00800000u;
        a64_W(c, base | ((u32)q << 30) | ((u32)v.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!strcmp(m, "bfmlalb") || !strcmp(m, "bfmlalt")) {
        int top = m[6] == 't';
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &v)) return 0;
        if (!arr_is(&d, 32, 4) || !arr_is(&n, 16, 8)) return hd_bad(c, "bfmlalb v0.4s, v1.8h, v2.8h");
        if (v.lane >= 0) {
            if (v.es != 16 || v.n > 15 || v.lane > 7) { err("%s: the element must be v0 to v15 .h[0..7]", c->mn); return 0; }
            u32 f = (v.lane & 1 ? 0x100000u : 0) | (v.lane & 2 ? 0x200000u : 0) | (v.lane & 4 ? 0x800u : 0) | ((u32)v.n << 16);
            a64_W(c, 0x0fc0f000u | ((u32)top << 30) | f | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (!arr_is(&v, 16, 8)) return hd_bad(c, "bfmlalb v0.4s, v1.8h, v2.8h");
        a64_W(c, 0x2ec0fc00u | ((u32)top << 30) | ((u32)v.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (!strcmp(m, "bfdot")) {
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        int q;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &v)) return 0;
        if (!sv_arr(c, &d, 0, &q) || d.es != 32 || !arr_is(&n, 16, q ? 8 : 4)) return hd_bad(c, "bfdot v0.4s, v1.8h, v2.8h");
        if (v.lane >= 0) {
            if (v.es != 16 || v.lane > 3) { err("bfdot: the element is v2.2h[0..3]"); return 0; }
            u32 f = (v.lane & 1 ? 0x200000u : 0) | (v.lane & 2 ? 0x800u : 0) | ((u32)v.n << 16);
            a64_W(c, 0x0f40f000u | ((u32)q << 30) | f | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (!arr_is(&v, 16, q ? 8 : 4)) return hd_bad(c, "bfdot v0.4s, v1.8h, v2.8h");
        a64_W(c, 0x2e40fc00u | ((u32)q << 30) | ((u32)v.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    {
        int u = 0, vec = 0x9400, el = 0xe000, esz = 2, ok = 0, noved = 0;
        if (!strcmp(m, "sdot")) ok = 1;
        else if (!strcmp(m, "udot")) { ok = 1; u = 1; }
        else if (!strcmp(m, "usdot")) { ok = 1; vec = 0x9c00; el = 0xf000; }
        else if (!strcmp(m, "sudot")) { ok = 1; el = 0xf000; esz = 0; noved = 1; }
        if (!ok) return PASS;
        if (c->n != 3) { sv_count(c, 3, 3); return 0; }
        int q;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n) || !sv_vreg(c, 2, &v)) return 0;
        if (!sv_arr(c, &d, 0, &q) || d.es != 32 || !arr_is(&n, 8, q ? 16 : 8)) return hd_bad(c, "sdot v0.4s, v1.16b, v2.16b");
        if (v.lane >= 0) {
            if (v.es != 8 || v.lanes != 4 || v.lane > 3) { err("%s: the element is v2.4b[0..3]", c->mn); return 0; }
            u32 f = (v.lane & 1 ? 0x200000u : 0) | (v.lane & 2 ? 0x800u : 0) | ((u32)v.n << 16);
            a64_W(c, 0x0f000000u | ((u32)q << 30) | ((u32)u << 29) | ((u32)esz << 22) | (u32)el | f | ((u32)n.n << 5) | (u32)d.n);
            return 1;
        }
        if (noved) { err("sudot: only the by-element form exists"); return 0; }
        if (!arr_is(&v, 8, q ? 16 : 8)) return hd_bad(c, "sdot v0.4s, v1.16b, v2.16b");
        a64_W(c, 0x0e000000u | ((u32)q << 30) | ((u32)u << 29) | (u32)vec | (2u << 22) | ((u32)v.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
}

/* bfcvt, fcmla, fcadd and other less common vector instructions. */
int h_misc(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!strcmp(c->mn, "bfcvt")) {
        if (c->n != 2) { sv_count(c, 2, 2); return 0; }
        VReg d, n;
        if (!sv_vreg(c, 0, &d) || !sv_vreg(c, 1, &n)) return 0;
        if (!d.scalar || !n.scalar || d.sk != 'h' || n.sk != 's') { err("bfcvt: the form is bfcvt h0, s1"); return 0; }
        a64_W(c, 0x1e634000u | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    return PASS;
}
