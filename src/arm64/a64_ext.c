/* ARM64 extension instructions: pointer authentication, memory tagging, BTI, flag manipulation, wait-for, transactional, CSSC and RCPC3 and LS64. */
#include "a64.h"

#define PASSX (-1)

/* Parses operand i as a 64-bit register, optionally allowing sp and xzr. */
static int xreg(Ctx *c, int i, GReg *g, int sp_ok, int zr_ok)
{
    if (i >= c->n) { err("%s: missing operand %d", c->mn, i + 1); return 0; }
    if (!a64_p_gpr(c->tok[i], g)) { err("%s: operand %d: general register expected, found `%s'", c->mn, i + 1, c->tok[i]); return 0; }
    if (!g->w64) { err("%s: operand %d: use an x register, found `%s'", c->mn, i + 1, c->tok[i]); return 0; }
    if (g->sp && !sp_ok) { err("%s: operand %d: sp is not allowed here", c->mn, i + 1); return 0; }
    if (g->zr && !zr_ok) { err("%s: operand %d: xzr is not allowed here", c->mn, i + 1); return 0; }
    return 1;
}

/* Checks the operand count and reports a count error if it is outside lo..hi. */
static int ncount(Ctx *c, int lo, int hi)
{
    if (c->n < lo || c->n > hi) {
        if (lo == hi) err("%s: expected %d operand%s, found %d", c->mn, lo, lo == 1 ? "" : "s", c->n);
        else err("%s: expected %d to %d operands, found %d", c->mn, lo, hi, c->n);
        return 0;
    }
    return 1;
}

/* Parses a memory operand with its addressing mode for the extension loads and stores. */
static int emem(Ctx *c, int idx, int *rn, i64 *imm, int *mode, int allow_off)
{
    const char *s = c->tok[idx];
    while (*s == ' ') s++;
    if (*s != '[') { err("%s: operand %d: memory operand like [x0] expected, found `%s'", c->mn, idx + 1, c->tok[idx]); return 0; }
    const char *close = strchr(s, ']');
    if (!close) { err("%s: missing `]'", c->mn); return 0; }
    const char *after = close + 1;
    while (*after == ' ') after++;
    *mode = 0;
    if (*after == '!') { *mode = 1; after++; while (*after == ' ') after++; }
    if (*after) { err("%s: unexpected text after `]'", c->mn); return 0; }
    char b[80];
    size_t l = (size_t)(close - s - 1);
    if (l >= sizeof b) { err("%s: memory operand too long", c->mn); return 0; }
    memcpy(b, s + 1, l);
    b[l] = 0;
    a64_trim(b);
    char *comma = strchr(b, ',');
    char *off = NULL;
    if (comma) { *comma = 0; off = comma + 1; a64_trim(b); a64_trim(off); }
    GReg g;
    if (!a64_p_gpr(b, &g) || !g.w64 || g.zr) { err("%s: the base must be an x register or sp, found `%s'", c->mn, b); return 0; }
    *rn = g.n;
    *imm = 0;
    if (off) {
        if (!allow_off) { err("%s: no offset is allowed here", c->mn); return 0; }
        int unk;
        if (!a64_const(c, off, imm, &unk)) return 0;
        if (unk) *imm = 0;
    }
    if (c->n > idx + 1) {
        if (*mode == 1 || off) { err("%s: bad addressing mode", c->mn); return 0; }
        int unk;
        if (!allow_off) { err("%s: no post-index is allowed here", c->mn); return 0; }
        if (!a64_const(c, c->tok[idx + 1], imm, &unk)) return 0;
        if (unk) *imm = 0;
        *mode = 2;
    }
    return 1;
}

/* Instructions that are one fixed word. */
static int h_word(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 0, 0)) return 0;
    a64_W(c, (u32)e->a);
    return 1;
}

/* bti with optional c, j or jc. */
static int h_bti(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 0, 1)) return 0;
    u32 w = 0xd503241fu;
    if (c->n) {
        char b[8];
        snprintf(b, sizeof b, "%s", c->tok[0]);
        for (char *p = b; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (!strcmp(b, "c")) w |= 1u << 6;
        else if (!strcmp(b, "j")) w |= 2u << 6;
        else if (!strcmp(b, "jc")) w |= 3u << 6;
        else { err("bti: the target must be c, j or jc"); return 0; }
    }
    a64_W(c, w);
    return 1;
}

/* Pointer authentication with two registers (pacia, autia ...). */
static int h_pac2(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 2)) return 0;
    GReg d, n;
    if (!xreg(c, 0, &d, 0, 0) || !xreg(c, 1, &n, 1, 0)) return 0;
    if (n.zr) { err("%s: xzr is not allowed here", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* Pointer authentication with one register (paciza, xpaci ...). */
static int h_pac1(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 1, 1)) return 0;
    GReg d;
    if (!xreg(c, 0, &d, 0, 0)) return 0;
    a64_W(c, (u32)e->a | (u32)d.n);
    return 1;
}

/* Authenticated branch with one register. */
static int h_br1(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 1, 1)) return 0;
    GReg n;
    if (!xreg(c, 0, &n, 0, 0)) return 0;
    a64_W(c, (u32)e->a | ((u32)n.n << 5));
    return 1;
}

/* Authenticated branch with two registers. */
static int h_br2(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 2)) return 0;
    GReg n, m;
    if (!xreg(c, 0, &n, 0, 0) || !xreg(c, 1, &m, 1, 0)) return 0;
    a64_W(c, (u32)e->a | ((u32)n.n << 5) | (u32)m.n);
    return 1;
}

/* pacga. */
static int h_pacga(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 3, 3)) return 0;
    GReg d, n, m;
    if (!xreg(c, 0, &d, 0, 0) || !xreg(c, 1, &n, 0, 1) || !xreg(c, 2, &m, 1, 0)) return 0;
    a64_W(c, 0x9ac03000u | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* ldraa and ldrab. */
static int h_ldra(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 3)) return 0;
    GReg t;
    int rn, mode;
    i64 imm;
    if (!xreg(c, 0, &t, 0, 1) || !emem(c, 1, &rn, &imm, &mode, 1)) return 0;
    if (mode == 2) { err("%s: post-index is not available", c->mn); return 0; }
    if (imm % 8 || imm < -4096 || imm > 4088) { err("%s: the offset must be a multiple of 8 from -4096 to 4088", c->mn); return 0; }
    u32 k = (u32)(imm / 8) & 0x3ff;
    a64_W(c, (u32)e->a | ((k >> 9) << 22) | ((k & 0x1ff) << 12) | ((u32)(mode == 1) << 11) | ((u32)rn << 5) | (u32)t.n);
    return 1;
}

/* irg (random tag). */
static int h_irg(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 2, 3)) return 0;
    GReg d, n, m;
    m.n = 31;
    if (!xreg(c, 0, &d, 1, 0) || !xreg(c, 1, &n, 1, 0)) return 0;
    if (c->n == 3 && !xreg(c, 2, &m, 0, 1)) return 0;
    a64_W(c, 0x9ac01000u | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* addg and subg (tag arithmetic). */
static int h_addg(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 4, 4)) return 0;
    GReg d, n;
    i64 o, t;
    int unk;
    if (!xreg(c, 0, &d, 1, 0) || !xreg(c, 1, &n, 1, 0)) return 0;
    if (!a64_const(c, c->tok[2], &o, &unk) || !a64_const(c, c->tok[3], &t, &unk)) return 0;
    if (o % 16 || o < 0 || o > 1008) { err("%s: the offset must be a multiple of 16 from 0 to 1008", c->mn); return 0; }
    if (t < 0 || t > 15) { err("%s: the tag offset must be 0 to 15", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)(o / 16) << 16) | ((u32)t << 10) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* Three register tag instructions. */
static int h_reg3(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 3, 3)) return 0;
    GReg d, n, m;
    int dsp = e->b & 1, nsp = e->b & 2, msp = e->b & 4, dzr = e->b & 8;
    if (!xreg(c, 0, &d, dsp, dzr) || !xreg(c, 1, &n, nsp, 0) || !xreg(c, 2, &m, msp, 0)) return 0;
    a64_W(c, (u32)e->a | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* cmpp and subps. */
static int h_cmpp(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 2, 2)) return 0;
    GReg n, m;
    if (!xreg(c, 0, &n, 1, 0) || !xreg(c, 1, &m, 1, 0)) return 0;
    a64_W(c, 0xbac0001fu | ((u32)m.n << 16) | ((u32)n.n << 5));
    return 1;
}

/* ldg. */
static int h_ldg(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 2, 2)) return 0;
    GReg t;
    int rn, mode;
    i64 imm;
    if (!xreg(c, 0, &t, 0, 1) || !emem(c, 1, &rn, &imm, &mode, 1)) return 0;
    if (mode) { err("ldg: no writeback is available"); return 0; }
    if (imm % 16 || imm < -4096 || imm > 4080) { err("ldg: the offset must be a multiple of 16 from -4096 to 4080"); return 0; }
    a64_W(c, 0xd9600000u | (((u32)(imm / 16) & 0x1ff) << 12) | ((u32)rn << 5) | (u32)t.n);
    return 1;
}

/* stg family. */
static int h_stg(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 3)) return 0;
    GReg t;
    int rn, mode;
    i64 imm;
    if (!xreg(c, 0, &t, 1, 1) || !emem(c, 1, &rn, &imm, &mode, 1)) return 0;
    if (imm % 16 || imm < -4096 || imm > 4080) { err("%s: the offset must be a multiple of 16 from -4096 to 4080", c->mn); return 0; }
    u32 m = mode == 2 ? 0x400u : mode == 1 ? 0xc00u : 0x800u;
    a64_W(c, (u32)e->a | m | (((u32)(imm / 16) & 0x1ff) << 12) | ((u32)rn << 5) | (u32)t.n);
    return 1;
}

/* stgp. */
static int h_stgp(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 3, 4)) return 0;
    GReg a, b;
    int rn, mode;
    i64 imm;
    if (!xreg(c, 0, &a, 0, 1) || !xreg(c, 1, &b, 0, 1) || !emem(c, 2, &rn, &imm, &mode, 1)) return 0;
    if (imm % 16 || imm < -1024 || imm > 1008) { err("stgp: the offset must be a multiple of 16 from -1024 to 1008"); return 0; }
    u32 base = mode == 2 ? 0x68800000u : mode == 1 ? 0x69800000u : 0x69000000u;
    a64_W(c, base | (((u32)(imm / 16) & 0x7f) << 15) | ((u32)b.n << 10) | ((u32)rn << 5) | (u32)a.n);
    return 1;
}

/* ldgm and stgm. */
static int h_gm(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 2)) return 0;
    GReg t;
    int rn, mode;
    i64 imm;
    if (!xreg(c, 0, &t, 0, 1)) return 0;
    const char *s = c->tok[1];
    while (*s == ' ') s++;
    if (*s != '[') { err("%s: memory operand like [x0] expected", c->mn); return 0; }
    if (!emem(c, 1, &rn, &imm, &mode, 0)) return 0;
    a64_W(c, (u32)e->a | ((u32)rn << 5) | (u32)t.n);
    return 1;
}

/* setf8, setf16. */
static int h_flag(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 1, 1)) return 0;
    GReg n;
    if (!a64_p_gpr(c->tok[0], &n) || n.w64 || n.sp) { err("%s: a w register is expected", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)n.n << 5));
    return 1;
}

/* rmif. */
static int h_rmif(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 3, 3)) return 0;
    GReg n;
    i64 s, m;
    int unk;
    if (!xreg(c, 0, &n, 0, 1)) return 0;
    if (!a64_const(c, c->tok[1], &s, &unk) || !a64_const(c, c->tok[2], &m, &unk)) return 0;
    if (s < 0 || s > 63) { err("rmif: the rotate must be 0 to 63"); return 0; }
    if (m < 0 || m > 15) { err("rmif: the mask must be 0 to 15"); return 0; }
    a64_W(c, 0xba000400u | ((u32)s << 15) | ((u32)n.n << 5) | (u32)m);
    return 1;
}

/* udf. */
static int h_udf(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 0, 1)) return 0;
    i64 v = 0;
    int unk;
    if (c->n && !a64_const(c, c->tok[0], &v, &unk)) return 0;
    if (v < 0 || v > 0xffff) { err("udf: the value must fit in 16 bits"); return 0; }
    a64_W(c, (u32)v);
    return 1;
}

/* wfet and wfit. */
static int h_wf(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 1, 1)) return 0;
    GReg t;
    if (!xreg(c, 0, &t, 0, 1)) return 0;
    a64_W(c, (u32)e->a | (u32)t.n);
    return 1;
}

/* tcancel. */
static int h_tcancel(Ctx *c, const MnEnt *e)
{
    (void)e;
    if (!ncount(c, 1, 1)) return 0;
    i64 v;
    int unk;
    if (!a64_const(c, c->tok[0], &v, &unk)) return 0;
    if (v < 0 || v > 0xffff) { err("tcancel: the value must fit in 16 bits"); return 0; }
    a64_W(c, 0xd4600000u | ((u32)v << 5));
    return 1;
}

/* CSSC one operand forms (abs, cnt, ctz). */
static int h_cssc1(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 2)) return 0;
    GReg d, n;
    if (!a64_p_gpr(c->tok[0], &d) || !a64_p_gpr(c->tok[1], &n) || d.sp || n.sp) { err("%s: general registers expected", c->mn); return 0; }
    if (d.w64 != n.w64) { err("%s: both registers must be the same size", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)d.w64 << 31) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* CSSC three operand forms (smax, smin, umax, umin). */
static int h_cssc3(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 3, 3)) return 0;
    GReg d, n, m;
    if (!a64_p_gpr(c->tok[0], &d) || !a64_p_gpr(c->tok[1], &n) || d.sp || n.sp) { err("%s: general registers expected", c->mn); return PASSX; }
    int sf = d.w64;
    if (a64_p_gpr(c->tok[2], &m)) {
        if (m.sp || m.w64 != sf || n.w64 != sf) { err("%s: all registers must be the same size", c->mn); return 0; }
        a64_W(c, (u32)e->a | ((u32)sf << 31) | ((u32)m.n << 16) | ((u32)n.n << 5) | (u32)d.n);
        return 1;
    }
    if (n.w64 != sf) { err("%s: both registers must be the same size", c->mn); return 0; }
    i64 v;
    int unk;
    if (!a64_const(c, c->tok[2], &v, &unk)) return 0;
    int sgn = e->b;
    if (sgn ? (v < -128 || v > 127) : (v < 0 || v > 255)) { err("%s: the immediate must be %s", c->mn, sgn ? "-128 to 127" : "0 to 255"); return 0; }
    a64_W(c, (u32)e->c | ((u32)sf << 31) | (((u32)v & 0xff) << 10) | ((u32)n.n << 5) | (u32)d.n);
    return 1;
}

/* RCPC immediate offset forms. */
static int h_rcpc9(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 2)) return 0;
    GReg t;
    int rn, mode;
    i64 imm;
    if (!a64_p_gpr(c->tok[0], &t) || t.sp) { err("%s: operand 1: general register expected", c->mn); return 0; }
    int wide = e->b;
    if (wide == 1 && !t.w64) { err("%s: use an x register", c->mn); return 0; }
    if (wide == 2 && t.w64) { err("%s: use a w register", c->mn); return 0; }
    if (!emem(c, 1, &rn, &imm, &mode, 1)) return 0;
    if (mode) { err("%s: no writeback is available", c->mn); return 0; }
    if (imm < -256 || imm > 255) { err("%s: the offset must be -256 to 255", c->mn); return 0; }
    a64_W(c, (u32)e->a | (((u32)imm & 0x1ff) << 12) | ((u32)rn << 5) | (u32)t.n);
    return 1;
}

/* RCPC3 forms. */
static int h_rcpcx(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 2)) return 0;
    GReg t;
    int rn, mode;
    i64 imm;
    if (!a64_p_gpr(c->tok[0], &t) || t.sp) { err("%s: operand 1: general register expected", c->mn); return 0; }
    if (!emem(c, 1, &rn, &imm, &mode, 1)) return 0;
    if (mode) { err("%s: no writeback is available", c->mn); return 0; }
    if (imm < -256 || imm > 255) { err("%s: the offset must be -256 to 255", c->mn); return 0; }
    u32 w = t.w64 ? (u32)e->b : (u32)e->a;
    a64_W(c, w | (((u32)imm & 0x1ff) << 12) | ((u32)rn << 5) | (u32)t.n);
    return 1;
}

/* ld64b, st64b and friends. */
static int h_ls64(Ctx *c, const MnEnt *e)
{
    if (!ncount(c, 2, 2)) return 0;
    GReg t;
    int rn, mode;
    i64 imm;
    if (!xreg(c, 0, &t, 0, 1) || !emem(c, 1, &rn, &imm, &mode, 0)) return 0;
    a64_W(c, (u32)e->a | ((u32)rn << 5) | (u32)t.n);
    return 1;
}

#define E(n, f, a, b, c) { n, f, a, b, c }

const MnEnt a64_tab_ext[] = {
    E("retaa", h_word, 0xd65f0bff, 0, 0), E("retab", h_word, 0xd65f0fff, 0, 0),
    E("eretaa", h_word, 0xd69f0bff, 0, 0), E("eretab", h_word, 0xd69f0fff, 0, 0),
    E("sb", h_word, 0xd50330ff, 0, 0), E("ssbb", h_word, 0xd503309f, 0, 0), E("pssbb", h_word, 0xd503349f, 0, 0),
    E("cfinv", h_word, 0xd500401f, 0, 0), E("xaflag", h_word, 0xd500403f, 0, 0), E("axflag", h_word, 0xd500405f, 0, 0),
    E("tcommit", h_word, 0xd503307f, 0, 0),
    E("bti", h_bti, 0, 0, 0),
    E("pacia", h_pac2, 0xdac10000, 0, 0), E("pacib", h_pac2, 0xdac10400, 0, 0), E("pacda", h_pac2, 0xdac10800, 0, 0), E("pacdb", h_pac2, 0xdac10c00, 0, 0),
    E("autia", h_pac2, 0xdac11000, 0, 0), E("autib", h_pac2, 0xdac11400, 0, 0), E("autda", h_pac2, 0xdac11800, 0, 0), E("autdb", h_pac2, 0xdac11c00, 0, 0),
    E("paciza", h_pac1, 0xdac123e0, 0, 0), E("pacizb", h_pac1, 0xdac127e0, 0, 0), E("pacdza", h_pac1, 0xdac12be0, 0, 0), E("pacdzb", h_pac1, 0xdac12fe0, 0, 0),
    E("autiza", h_pac1, 0xdac133e0, 0, 0), E("autizb", h_pac1, 0xdac137e0, 0, 0), E("autdza", h_pac1, 0xdac13be0, 0, 0), E("autdzb", h_pac1, 0xdac13fe0, 0, 0),
    E("xpaci", h_pac1, 0xdac143e0, 0, 0), E("xpacd", h_pac1, 0xdac147e0, 0, 0),
    E("braaz", h_br1, 0xd61f081f, 0, 0), E("brabz", h_br1, 0xd61f0c1f, 0, 0), E("blraaz", h_br1, 0xd63f081f, 0, 0), E("blrabz", h_br1, 0xd63f0c1f, 0, 0),
    E("braa", h_br2, 0xd71f0800, 0, 0), E("brab", h_br2, 0xd71f0c00, 0, 0), E("blraa", h_br2, 0xd73f0800, 0, 0), E("blrab", h_br2, 0xd73f0c00, 0, 0),
    E("pacga", h_pacga, 0, 0, 0),
    E("ldraa", h_ldra, 0xf8200400, 0, 0), E("ldrab", h_ldra, 0xf8a00400, 0, 0),
    E("irg", h_irg, 0, 0, 0), E("addg", h_addg, 0x91800000, 0, 0), E("subg", h_addg, 0xd1800000, 0, 0),
    E("gmi", h_reg3, 0x9ac01400, 0x8 | 0x2 | 0x0, 0),
    E("subp", h_reg3, 0x9ac00000, 0x8 | 0x2 | 0x4, 0), E("subps", h_reg3, 0xbac00000, 0x8 | 0x2 | 0x4, 0),
    E("cmpp", h_cmpp, 0, 0, 0),
    E("ldg", h_ldg, 0, 0, 0),
    E("stg", h_stg, 0xd9200000, 0, 0), E("stzg", h_stg, 0xd9600000, 0, 0), E("st2g", h_stg, 0xd9a00000, 0, 0), E("stz2g", h_stg, 0xd9e00000, 0, 0),
    E("stgp", h_stgp, 0, 0, 0),
    E("ldgm", h_gm, 0xd9e00000, 0, 0), E("stgm", h_gm, 0xd9a00000, 0, 0), E("stzgm", h_gm, 0xd9200000, 0, 0),
    E("setf8", h_flag, 0x3a00080d, 0, 0), E("setf16", h_flag, 0x3a00480d, 0, 0),
    E("rmif", h_rmif, 0, 0, 0),
    E("udf", h_udf, 0, 0, 0),
    E("wfet", h_wf, 0xd5031000, 0, 0), E("wfit", h_wf, 0xd5031020, 0, 0),
    E("tstart", h_wf, 0xd5233060, 0, 0), E("ttest", h_wf, 0xd5233160, 0, 0),
    E("tcancel", h_tcancel, 0, 0, 0),
    E("abs", h_cssc1, 0x5ac02000, 0, 0), E("cnt", h_cssc1, 0x5ac01c00, 0, 0), E("ctz", h_cssc1, 0x5ac01800, 0, 0),
    E("smax", h_cssc3, 0x1ac06000, 1, 0x11c00000), E("umax", h_cssc3, 0x1ac06400, 0, 0x11c40000),
    E("smin", h_cssc3, 0x1ac06800, 1, 0x11c80000), E("umin", h_cssc3, 0x1ac06c00, 0, 0x11cc0000),
    E("ldapurb", h_rcpc9, 0x19400000, 2, 0), E("ldapurh", h_rcpc9, 0x59400000, 2, 0),
    E("ldapursb", h_rcpcx, 0x19c00000, 0x19800000, 0), E("ldapursh", h_rcpcx, 0x59c00000, 0x59800000, 0),
    E("ldapursw", h_rcpc9, 0x99800000, 1, 0),
    E("ldapur", h_rcpcx, 0x99400000, 0xd9400000, 0),
    E("stlurb", h_rcpc9, 0x19000000, 2, 0), E("stlurh", h_rcpc9, 0x59000000, 2, 0),
    E("stlur", h_rcpcx, 0x99000000, 0xd9000000, 0),
    E("ld64b", h_ls64, 0xf83fd000, 0, 0), E("st64b", h_ls64, 0xf83f9000, 0, 0),
    { NULL, NULL, 0, 0, 0 }
};
