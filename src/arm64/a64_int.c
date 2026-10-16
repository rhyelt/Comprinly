/* ARM64 integer instructions: add/sub, logic, moves, bitfields, shifts, conditional select, multiply and divide, branches, system and barriers. */
#include "a64.h"

/* Checks the operand count and reports a count error if it is outside lo..hi. */
static int usage(Ctx *c, int lo, int hi)
{
    if (lo == hi) err("%s: expected %d operand%s, found %d", c->mn, lo, lo == 1 ? "" : "s", c->n);
    else err("%s: expected %d to %d operands, found %d", c->mn, lo, hi, c->n);
    return 0;
}

#define NEED(lo, hi) do { if (c->n < (lo) || c->n > (hi)) return usage(c, (lo), (hi)); } while (0)

/* Operand i as any general register (including sp). */
static int gpr_any(Ctx *c, int i, GReg *r)
{
    if (!a64_p_gpr(c->tok[i], r)) {
        err("%s: operand %d: general register expected, found `%s'", c->mn, i + 1, c->tok[i]);
        return 0;
    }
    return 1;
}

/* Operand i as a general register where 31 means the zero register (sp is not allowed). */
static int gpr_zr(Ctx *c, int i, GReg *r)
{
    if (!gpr_any(c, i, r)) return 0;
    if (r->sp) { err("%s: operand %d: `%s' is not allowed here (use a normal register or zr)", c->mn, i + 1, c->tok[i]); return 0; }
    return 1;
}

/* Checks that two registers are both 32 or both 64-bit. */
static int sizes_match(Ctx *c, GReg a, GReg b)
{
    if (a.w64 != b.w64) { err("%s: register sizes do not match", c->mn); return 0; }
    return 1;
}

typedef struct { const char *n; int kind; int code; } ShName;

static const ShName shnames[] = {
    { "lsl", 1, 0 }, { "lsr", 1, 1 }, { "asr", 1, 2 }, { "ror", 1, 3 }, { "msl", 1, 4 },
    { "uxtb", 2, 0 }, { "uxth", 2, 1 }, { "uxtw", 2, 2 }, { "uxtx", 2, 3 }, { "sxtb", 2, 4 }, { "sxth", 2, 5 }, { "sxtw", 2, 6 }, { "sxtx", 2, 7 }, { NULL, 0, 0 }
};

/* Parses shift or extend suffix (lsl, uxtw ...) and amount. */
static int parse_sh(Ctx *c, const char *s, int *kind, int *code, i64 *amt, int *has_amt, int *unk)
{
    char w[8];
    size_t n = 0;
    while (isalpha((unsigned char)s[n]) && n < sizeof w - 1) { w[n] = (char)tolower((unsigned char)s[n]); n++; }
    w[n] = 0;
    if (isalpha((unsigned char)s[n])) return 0;
    int found = -1;
    for (int i = 0; shnames[i].n; i++) if (!strcmp(shnames[i].n, w)) found = i;
    if (found < 0) return 0;
    *kind = shnames[found].kind;
    *code = shnames[found].code;
    const char *rest = s + n;
    while (*rest == ' ' || *rest == '\t') rest++;
    *has_amt = 0;
    *amt = 0;
    if (unk) *unk = 0;
    if (*rest) {
        if (!a64_const(c, rest, amt, unk)) return -1;
        *has_amt = 1;
    }
    return 1;
}

/* Shared encoder for add, sub, adds, subs: immediate, shifted register and extended register forms. */
static int addsub_core(Ctx *c, int op, int S, GReg rd, GReg rn, int ofs)
{
    int nrem = c->n - ofs;
    if (nrem < 1 || nrem > 2) return usage(c, ofs + 1, ofs + 2);
    int sf = rd.w64;
    if (rd.w64 != rn.w64) { err("%s: register sizes do not match", c->mn); return 0; }
    if (S && rd.sp) { err("%s: the destination cannot be sp when flags are set", c->mn); return 0; }
    GReg rm;
    if (a64_p_gpr(c->tok[ofs], &rm)) {
        if (rm.sp) { err("%s: `%s' is not allowed as the second source", c->mn, c->tok[ofs]); return 0; }
        int kind = 0, code = 0, hasamt = 0, unk = 0;
        i64 amt = 0;
        if (nrem == 2) {
            int r = parse_sh(c, c->tok[ofs + 1], &kind, &code, &amt, &hasamt, &unk);
            if (r < 0) return 0;
            if (r == 0) { err("%s: shift or extend expected, found `%s'", c->mn, c->tok[ofs + 1]); return 0; }
        }
        int extended = rn.sp || (!S && rd.sp) || kind == 2;
        if (!extended) {
            if (!sizes_match(c, rd, rm)) return 0;
            int sh = kind == 1 ? code : 0;
            if (kind == 1 && sh > 2) { err("%s: only lsl, lsr and asr are allowed here", c->mn); return 0; }
            if (!hasamt) amt = 0;
            if (amt < 0 || amt > (sf ? 63 : 31)) { err("%s: shift amount out of range (0 to %d)", c->mn, sf ? 63 : 31); return 0; }
            a64_W(c, ((u32)sf << 31) | ((u32)op << 30) | ((u32)S << 29) | 0x0b000000u | ((u32)sh << 22) | ((u32)rm.n << 16) | ((u32)amt << 10) | ((u32)rn.n << 5) | (u32)rd.n);
            return 1;
        }
        if (rn.zr || (rd.zr && !S)) { err("%s: zr is not allowed with sp or an extended register", c->mn); return 0; }
        int option;
        if (kind == 2) option = code;
        else if (kind == 1) {
            if (code != 0) { err("%s: only lsl is allowed with sp", c->mn); return 0; }
            option = sf ? 3 : 2;
        } else option = sf ? 3 : 2;
        if (!hasamt) amt = 0;
        if (amt < 0 || amt > 4) { err("%s: extend shift amount must be 0 to 4", c->mn); return 0; }
        int rm64 = (option == 3 || option == 7);
        if (sf) {
            if (rm64 != rm.w64) { err("%s: the extended register must be %s for %s", c->mn, rm64 ? "an x register" : "a w register", rm64 ? "uxtx/sxtx/lsl" : "this extend"); return 0; }
        } else {
            if (rm.w64) { err("%s: register sizes do not match", c->mn); return 0; }
            if (rm64) { err("%s: uxtx and sxtx need 64-bit registers", c->mn); return 0; }
        }
        a64_W(c, ((u32)sf << 31) | ((u32)op << 30) | ((u32)S << 29) | 0x0b200000u | ((u32)rm.n << 16) | ((u32)option << 13) | ((u32)amt << 10) | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
    if (rn.zr || (rd.zr && !S)) { err("%s: zr is not allowed with an immediate here", c->mn); return 0; }
    const char *t = a64_strip_hash(c->tok[ofs]);
    if (t[0] == ':') {
        const char *e = strchr(t + 1, ':');
        if (!e) { err("%s: bad relocation modifier", c->mn); return 0; }
        char m[16];
        size_t n = (size_t)(e - t - 1);
        if (n >= sizeof m) { err("%s: unknown relocation modifier", c->mn); return 0; }
        memcpy(m, t + 1, n);
        m[n] = 0;
        if (strcasecmp(m, "lo12") && strcasecmp(m, "lo12_nc")) { err("%s: modifier `:%s:' is not supported here (use :lo12:)", c->mn, m); return 0; }
        if (op || S) { err("%s: :lo12: is only valid with add", c->mn); return 0; }
        Val v;
        if (!expr_eval_str(e + 1, &v, 0)) return 0;
        a64_reloc(c, RK_A64_LO12, 0, &v, (i64)c->out->code.n);
        a64_W(c, ((u32)sf << 31) | 0x11000000u | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
    i64 imm;
    int unk = 0;
    if (!a64_const(c, c->tok[ofs], &imm, &unk)) return 0;
    int sh = 0;
    int explicit_sh = 0;
    if (nrem == 2) {
        int kind, code, hasamt, u2;
        i64 amt;
        int r = parse_sh(c, c->tok[ofs + 1], &kind, &code, &amt, &hasamt, &u2);
        if (r < 0) return 0;
        if (r == 0 || kind != 1 || code != 0) { err("%s: only `lsl #0' or `lsl #12' may follow an immediate", c->mn); return 0; }
        if (!hasamt || (amt != 0 && amt != 12)) { err("%s: the immediate shift must be 0 or 12", c->mn); return 0; }
        sh = amt == 12;
        explicit_sh = 1;
    }
    if (!unk && imm < 0 && explicit_sh && -imm <= 4095) { imm = -imm; op ^= 1; }
    else if (!unk && imm < 0 && !explicit_sh) {
        i64 m = -imm;
        if (m <= 4095 || ((m & 0xfff) == 0 && (m >> 12) <= 4095)) { imm = m; op ^= 1; }
    }
    if (!unk && !explicit_sh && imm > 4095 && (imm & 0xfff) == 0 && (imm >> 12) <= 4095) { imm >>= 12; sh = 1; }
    if (unk) imm = 0;
    if (imm < 0 || imm > 4095) {
        err("%s: immediate %lld is out of range (0 to 4095, or a multiple of 4096 up to 16773120)", c->mn, (long long)imm);
        return 0;
    }
    a64_W(c, ((u32)sf << 31) | ((u32)op << 30) | ((u32)S << 29) | 0x11000000u | ((u32)sh << 22) | ((u32)imm << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* add, sub, adds, subs. */
static int h_addsub(Ctx *c, const MnEnt *e)
{
    NEED(3, 4);
    GReg rd, rn;
    if (!gpr_any(c, 0, &rd) || !gpr_any(c, 1, &rn)) return 0;
    return addsub_core(c, e->a, e->b, rd, rn, 2);
}

/* cmp and cmn. */
static int h_cmp(Ctx *c, const MnEnt *e)
{
    NEED(2, 3);
    GReg rn;
    if (!gpr_any(c, 0, &rn)) return 0;
    GReg rd = { 31, rn.w64, 0, 1 };
    return addsub_core(c, e->a, 1, rd, rn, 1);
}

/* neg, negs. */
static int h_neg(Ctx *c, const MnEnt *e)
{
    NEED(2, 3);
    GReg rd, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rm)) return 0;
    GReg zr = { 31, rd.w64, 0, 1 };
    return addsub_core(c, 1, e->b, rd, zr, 1);
}

/* Checks that an immediate can be used as a bitmask and encodes it. */
static int logic_imm_ok(Ctx *c, i64 v, int sf, int invert, u32 *enc)
{
    u64 u = (u64)v;
    if (!sf) {
        if (v < -2147483648LL || v > 4294967295LL) { err("%s: immediate does not fit in 32 bits", c->mn); return 0; }
        u &= 0xffffffffULL;
    }
    if (invert) u = ~u;
    if (!a64_logical_imm(u, sf, enc)) {
        err("%s: immediate 0x%llx cannot be encoded as a bitmask", c->mn, (unsigned long long)(sf ? (u64)v : (u64)v & 0xffffffffULL));
        return 0;
    }
    return 1;
}

/* Shared encoder for and, orr, eor, ands, bic, orn, eon, bics. */
static int logic_core(Ctx *c, int opc, int N, GReg rd, GReg rn, int ofs)
{
    int nrem = c->n - ofs;
    if (nrem < 1 || nrem > 2) return usage(c, ofs + 1, ofs + 2);
    int sf = rd.w64;
    GReg rm;
    if (a64_p_gpr(c->tok[ofs], &rm)) {
        if (rm.sp || rd.sp || rn.sp) { err("%s: sp is not allowed with register operands", c->mn); return 0; }
        if (!sizes_match(c, rd, rm) || !sizes_match(c, rd, rn)) return 0;
        int sh = 0;
        i64 amt = 0;
        if (nrem == 2) {
            int kind, code, hasamt, unk;
            int r = parse_sh(c, c->tok[ofs + 1], &kind, &code, &amt, &hasamt, &unk);
            if (r < 0) return 0;
            if (r == 0 || kind != 1 || code > 3) { err("%s: shift expected (lsl, lsr, asr or ror), found `%s'", c->mn, c->tok[ofs + 1]); return 0; }
            sh = code;
            if (!hasamt) amt = 0;
        }
        if (amt < 0 || amt > (sf ? 63 : 31)) { err("%s: shift amount out of range (0 to %d)", c->mn, sf ? 63 : 31); return 0; }
        a64_W(c, ((u32)sf << 31) | ((u32)opc << 29) | 0x0a000000u | ((u32)sh << 22) | ((u32)N << 21) | ((u32)rm.n << 16) | ((u32)amt << 10) | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
    if (nrem == 2) { err("%s: an immediate cannot be shifted", c->mn); return 0; }
    if (rn.sp) { err("%s: sp is not allowed as a source here", c->mn); return 0; }
    if (opc == 3 && rd.sp) { err("%s: the destination cannot be sp when flags are set", c->mn); return 0; }
    if (!sizes_match(c, rd, rn)) return 0;
    i64 imm;
    int unk;
    if (!a64_const(c, c->tok[ofs], &imm, &unk)) return 0;
    u32 enc = 0x1000;
    if (unk) imm = 1;
    if (!logic_imm_ok(c, imm, sf, N, &enc)) return 0;
    a64_W(c, ((u32)sf << 31) | ((u32)opc << 29) | 0x12000000u | (enc << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* and, orr, eor, ands and their inverted forms. */
static int h_logic(Ctx *c, const MnEnt *e)
{
    NEED(3, 4);
    GReg rd, rn;
    if (!gpr_any(c, 0, &rd) || !gpr_zr(c, 1, &rn)) return 0;
    if (e->a == 3 && rd.sp) { err("%s: the destination cannot be sp", c->mn); return 0; }
    return logic_core(c, e->a, e->b, rd, rn, 2);
}

/* tst. */
static int h_tst(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(2, 3);
    GReg rn;
    if (!gpr_zr(c, 0, &rn)) return 0;
    GReg rd = { 31, rn.w64, 0, 1 };
    return logic_core(c, 3, 0, rd, rn, 1);
}

/* mvn. */
static int h_mvn(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(2, 3);
    GReg rd, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rm)) return 0;
    GReg zr = { 31, rd.w64, 0, 1 };
    return logic_core(c, 1, 1, rd, zr, 1);
}

/* movz, movn, movk. */
static int h_movwide(Ctx *c, const MnEnt *e)
{
    NEED(2, 3);
    GReg rd;
    if (!gpr_zr(c, 0, &rd)) return 0;
    int sf = rd.w64;
    const char *t = a64_strip_hash(c->tok[1]);
    i64 amt = 0;
    int hasamt = 0;
    if (c->n == 3) {
        int kind, code, unk;
        int r = parse_sh(c, c->tok[2], &kind, &code, &amt, &hasamt, &unk);
        if (r < 0) return 0;
        if (r == 0 || kind != 1 || (code != 0 && code != 4)) { err("%s: `lsl #n' expected, found `%s'", c->mn, c->tok[2]); return 0; }
        if (!hasamt) { err("%s: shift amount expected", c->mn); return 0; }
    }
    if (t[0] == ':') {
        const char *en = strchr(t + 1, ':');
        if (!en) { err("%s: bad relocation modifier", c->mn); return 0; }
        char m[16];
        size_t n = (size_t)(en - t - 1);
        if (n >= sizeof m) { err("%s: unknown relocation modifier", c->mn); return 0; }
        memcpy(m, t + 1, n);
        m[n] = 0;
        int g = -1, nc = 0;
        if (!strncasecmp(m, "abs_g", 5) && m[5] >= '0' && m[5] <= '3') {
            g = m[5] - '0';
            if (m[6] == 0) nc = 0;
            else if (!strcasecmp(m + 6, "_nc")) nc = 1;
            else g = -1;
        }
        if (g < 0) { err("%s: modifier `:%s:' is not supported (use :abs_g0: to :abs_g3:, optionally with _nc)", c->mn, m); return 0; }
        if (g > 1 && !sf) { err("%s: :abs_g%d: needs a 64-bit register", c->mn, g); return 0; }
        Val v;
        if (!expr_eval_str(en + 1, &v, 0)) return 0;
        a64_reloc(c, RK_A64_MOVW, g | (nc << 2), &v, (i64)c->out->code.n);
        a64_W(c, ((u32)sf << 31) | ((u32)e->a << 29) | 0x12800000u | ((u32)g << 21) | (u32)rd.n);
        return 1;
    }
    i64 imm;
    int unk;
    if (!a64_const(c, c->tok[1], &imm, &unk)) return 0;
    if (unk) imm = 0;
    if (imm < 0 || imm > 65535) { err("%s: immediate %lld is out of range (0 to 65535)", c->mn, (long long)imm); return 0; }
    if (hasamt && (amt < 0 || amt % 16 != 0 || amt > (sf ? 48 : 16))) { err("%s: the shift must be %s", c->mn, sf ? "0, 16, 32 or 48" : "0 or 16"); return 0; }
    a64_W(c, ((u32)sf << 31) | ((u32)e->a << 29) | 0x12800000u | ((u32)(amt / 16) << 21) | ((u32)imm << 5) | (u32)rd.n);
    return 1;
}

int a64_simd_mov(Ctx *c);

/* mov: register, immediate, sp and bitmask forms. */
static int h_mov(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(2, 2);
    GReg rd, rm;
    if (!a64_p_gpr(c->tok[0], &rd)) {
        VReg v;
        if (a64_p_vec(c->tok[0], &v) || a64_p_vec(c->tok[1], &v)) return a64_simd_mov(c);
        err("%s: general register expected, found `%s'", c->mn, c->tok[0]);
        return 0;
    }
    if (a64_p_gpr(c->tok[1], &rm)) {
        if (!sizes_match(c, rd, rm)) return 0;
        if (rd.sp || rm.sp) {
            if (rd.zr || rm.zr) { err("%s: cannot mix sp and zr", c->mn); return 0; }
            a64_W(c, ((u32)rd.w64 << 31) | 0x11000000u | ((u32)rm.n << 5) | (u32)rd.n);
            return 1;
        }
        a64_W(c, ((u32)rd.w64 << 31) | 0x2a0003e0u | ((u32)rm.n << 16) | (u32)rd.n);
        return 1;
    }
    VReg vv;
    if (a64_p_vec(c->tok[1], &vv)) return a64_simd_mov(c);
    if (rd.zr) { err("%s: the destination cannot be %s", c->mn, c->tok[0]); return 0; }
    Val v;
    if (!a64_eval(c, c->tok[1], &v)) return 0;
    if (!v.unk && !val_isconst(&v)) {
        if (!rd.w64) { err("%s: an address does not fit in a 32-bit register; use an x register", c->mn); return 0; }
        if (rd.sp) { err("%s: cannot load an address into sp directly", c->mn); return 0; }
        return a64_load_addr(c, rd.n, &v);
    }
    if (rd.sp) {
        u32 enc;
        u64 u = (u64)v.n;
        if (!rd.w64) u &= 0xffffffffULL;
        if (!a64_logical_imm(u, rd.w64, &enc)) { err("%s: this immediate cannot be moved into sp", c->mn); return 0; }
        a64_W(c, ((u32)rd.w64 << 31) | 0x320003e0u | (enc << 10) | 31u);
        return 1;
    }
    return a64_mov_imm(c, rd, v.unk ? 0 : v.n, v.unk);
}

/* ldr x, =value pseudo form. */
static int h_ldr_eq(Ctx *c, GReg rd)
{
    const char *t = c->tok[1];
    t++;
    Val v;
    if (!expr_eval_str(t, &v, 0)) return 0;
    if (!v.unk && !val_isconst(&v)) {
        if (!rd.w64) { err("%s: an address does not fit in a 32-bit register", c->mn); return 0; }
        return a64_load_addr(c, rd.n, &v);
    }
    return a64_mov_imm(c, rd, v.unk ? 0 : v.n, v.unk);
}

/* Public entry for the ldr =value form. */
int a64_ldr_eq(Ctx *c, GReg rd)
{
    return h_ldr_eq(c, rd);
}

/* adr and adrp. */
static int h_adr(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    GReg rd;
    if (!gpr_zr(c, 0, &rd)) return 0;
    if (!rd.w64) { err("%s: an x register is required", c->mn); return 0; }
    Val v;
    if (!a64_eval(c, c->tok[1], &v)) return 0;
    u32 base = e->a ? 0x90000000u : 0x10000000u;
    if (!e->a && a64_same_sec(c, &v)) {
        i64 d = v.n - a64_cur_off(c);
        if (d < -(1 << 20) || d >= (1 << 20)) { err("%s: target is out of range (limit is 1 MB; use adrp)", c->mn); return 0; }
        a64_W(c, base | (((u32)d & 3) << 29) | ((((u32)d >> 2) & 0x7ffff) << 5) | (u32)rd.n);
        return 1;
    }
    if (v.unk) { a64_W(c, base | (u32)rd.n); return 1; }
    a64_reloc(c, e->a ? RK_A64_ADRP : RK_A64_ADR, 0, &v, (i64)c->out->code.n);
    a64_W(c, base | (u32)rd.n);
    return 1;
}

/* Shared bitfield encoder. */
static int bf_core(Ctx *c, int opc, GReg rd, GReg rn, i64 immr, i64 imms)
{
    int sf = rd.w64;
    int lim = sf ? 63 : 31;
    if (immr < 0 || immr > lim || imms < 0 || imms > lim) { err("%s: bit position out of range (0 to %d)", c->mn, lim); return 0; }
    a64_W(c, ((u32)sf << 31) | ((u32)opc << 29) | 0x13000000u | ((u32)sf << 22) | ((u32)immr << 16) | ((u32)imms << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* bfm, sbfm, ubfm. */
static int h_bfm(Ctx *c, const MnEnt *e)
{
    NEED(4, 4);
    GReg rd, rn;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !sizes_match(c, rd, rn)) return 0;
    i64 a, b;
    int u1, u2;
    if (!a64_const(c, c->tok[2], &a, &u1) || !a64_const(c, c->tok[3], &b, &u2)) return 0;
    return bf_core(c, e->a, rd, rn, a, b);
}

/* Bitfield aliases (sbfx, ubfx, sbfiz, ubfiz, bfi, bfxil). */
static int h_bfx(Ctx *c, const MnEnt *e)
{
    NEED(4, 4);
    GReg rd, rn;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !sizes_match(c, rd, rn)) return 0;
    i64 lsb, w;
    int u1, u2;
    if (!a64_const(c, c->tok[2], &lsb, &u1) || !a64_const(c, c->tok[3], &w, &u2)) return 0;
    int sz = rd.w64 ? 64 : 32;
    if (lsb < 0 || lsb >= sz) { err("%s: lsb out of range (0 to %d)", c->mn, sz - 1); return 0; }
    if (w < 1 || lsb + w > sz) { err("%s: width out of range (1 to %d)", c->mn, (int)(sz - lsb)); return 0; }
    if (e->b) return bf_core(c, e->a, rd, rn, (sz - lsb) % sz, w - 1);
    return bf_core(c, e->a, rd, rn, lsb, lsb + w - 1);
}

/* bfc. */
static int h_bfc(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(3, 3);
    GReg rd;
    if (!gpr_zr(c, 0, &rd)) return 0;
    i64 lsb, w;
    int u1, u2;
    if (!a64_const(c, c->tok[1], &lsb, &u1) || !a64_const(c, c->tok[2], &w, &u2)) return 0;
    int sz = rd.w64 ? 64 : 32;
    if (lsb < 0 || lsb >= sz) { err("%s: lsb out of range (0 to %d)", c->mn, sz - 1); return 0; }
    if (w < 1 || lsb + w > sz) { err("%s: width out of range (1 to %d)", c->mn, (int)(sz - lsb)); return 0; }
    GReg zr = { 31, rd.w64, 0, 1 };
    return bf_core(c, 1, rd, zr, (sz - lsb) % sz, w - 1);
}

/* Extend aliases (sxtb, sxth, sxtw, uxtb, uxth). */
static int h_ext(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    GReg rd, rn;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn)) return 0;
    if (rn.w64) { err("%s: the source must be a w register", c->mn); return 0; }
    if (e->c == 1 && rd.w64) { err("%s: the destination must be a w register", c->mn); return 0; }
    if (e->b == 31 && !rd.w64) { err("%s: the destination must be an x register", c->mn); return 0; }
    return bf_core(c, e->c == 1 ? 2 : 0, rd, rn, 0, e->b);
}

/* lsl, lsr, asr, ror with a register or immediate amount. */
static int h_shift(Ctx *c, const MnEnt *e)
{
    NEED(3, 3);
    GReg rd, rn, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !sizes_match(c, rd, rn)) return 0;
    int sf = rd.w64;
    if (a64_p_gpr(c->tok[2], &rm)) {
        if (rm.sp) { err("%s: sp is not allowed here", c->mn); return 0; }
        if (!sizes_match(c, rd, rm)) return 0;
        a64_W(c, ((u32)sf << 31) | 0x1ac02000u | ((u32)e->a << 10) | ((u32)rm.n << 16) | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
    i64 n;
    int unk;
    if (!a64_const(c, c->tok[2], &n, &unk)) return 0;
    int sz = sf ? 64 : 32;
    if (n < 0 || n >= sz) { err("%s: shift amount out of range (0 to %d)", c->mn, sz - 1); return 0; }
    switch (e->a) {
    case 0: return bf_core(c, 2, rd, rn, (sz - n) % sz, sz - 1 - n);
    case 1: return bf_core(c, 2, rd, rn, n, sz - 1);
    case 2: return bf_core(c, 0, rd, rn, n, sz - 1);
    default:
        a64_W(c, ((u32)sf << 31) | 0x13800000u | ((u32)sf << 22) | ((u32)rn.n << 16) | ((u32)n << 10) | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
}

/* extr and the ror immediate alias. */
static int h_extr(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(4, 4);
    GReg rd, rn, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !gpr_zr(c, 2, &rm) || !sizes_match(c, rd, rn) || !sizes_match(c, rd, rm)) return 0;
    i64 n;
    int unk;
    if (!a64_const(c, c->tok[3], &n, &unk)) return 0;
    int sz = rd.w64 ? 64 : 32;
    if (n < 0 || n >= sz) { err("%s: bit position out of range (0 to %d)", c->mn, sz - 1); return 0; }
    a64_W(c, ((u32)rd.w64 << 31) | 0x13800000u | ((u32)rd.w64 << 22) | ((u32)rm.n << 16) | ((u32)n << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* adc, adcs, sbc, sbcs. */
static int h_adc(Ctx *c, const MnEnt *e)
{
    NEED(3, 3);
    GReg rd, rn, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !gpr_zr(c, 2, &rm) || !sizes_match(c, rd, rn) || !sizes_match(c, rd, rm)) return 0;
    a64_W(c, ((u32)rd.w64 << 31) | ((u32)e->a << 30) | ((u32)e->b << 29) | 0x1a000000u | ((u32)rm.n << 16) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* ngc, ngcs. */
static int h_ngc(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    GReg rd, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rm) || !sizes_match(c, rd, rm)) return 0;
    a64_W(c, ((u32)rd.w64 << 31) | (1u << 30) | ((u32)e->b << 29) | 0x1a000000u | ((u32)rm.n << 16) | (31u << 5) | (u32)rd.n);
    return 1;
}

/* csel, csinc, csinv, csneg. */
static int h_csel(Ctx *c, const MnEnt *e)
{
    NEED(4, 4);
    GReg rd, rn, rm;
    int cc;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !gpr_zr(c, 2, &rm) || !sizes_match(c, rd, rn) || !sizes_match(c, rd, rm)) return 0;
    if (!a64_p_cond(c->tok[3], &cc)) { err("%s: condition code expected, found `%s'", c->mn, c->tok[3]); return 0; }
    a64_W(c, ((u32)rd.w64 << 31) | ((u32)e->a << 30) | 0x1a800000u | ((u32)rm.n << 16) | ((u32)cc << 12) | ((u32)e->b << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* cset, csetm. */
static int h_cset(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    GReg rd;
    int cc;
    if (!gpr_zr(c, 0, &rd)) return 0;
    if (!a64_p_cond(c->tok[1], &cc)) { err("%s: condition code expected, found `%s'", c->mn, c->tok[1]); return 0; }
    if (cc >= 14) { err("%s: the condition cannot be al or nv", c->mn); return 0; }
    a64_W(c, ((u32)rd.w64 << 31) | ((u32)e->a << 30) | 0x1a800000u | (31u << 16) | ((u32)(cc ^ 1) << 12) | ((u32)e->b << 10) | (31u << 5) | (u32)rd.n);
    return 1;
}

/* cinc, cinv, cneg. */
static int h_cinc(Ctx *c, const MnEnt *e)
{
    NEED(3, 3);
    GReg rd, rn;
    int cc;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !sizes_match(c, rd, rn)) return 0;
    if (!a64_p_cond(c->tok[2], &cc)) { err("%s: condition code expected, found `%s'", c->mn, c->tok[2]); return 0; }
    if (cc >= 14) { err("%s: the condition cannot be al or nv", c->mn); return 0; }
    a64_W(c, ((u32)rd.w64 << 31) | ((u32)e->a << 30) | 0x1a800000u | ((u32)rn.n << 16) | ((u32)(cc ^ 1) << 12) | ((u32)e->b << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* ccmp, ccmn. */
static int h_ccmp(Ctx *c, const MnEnt *e)
{
    NEED(4, 4);
    GReg rn, rm;
    int cc;
    i64 nzcv;
    int unk;
    if (!gpr_zr(c, 0, &rn)) return 0;
    if (!a64_const(c, c->tok[2], &nzcv, &unk)) return 0;
    if (unk) nzcv = 0;
    if (nzcv < 0 || nzcv > 15) { err("%s: nzcv must be 0 to 15", c->mn); return 0; }
    if (!a64_p_cond(c->tok[3], &cc)) { err("%s: condition code expected, found `%s'", c->mn, c->tok[3]); return 0; }
    u32 base = ((u32)rn.w64 << 31) | ((u32)e->a << 30) | 0x3a400000u | ((u32)cc << 12) | ((u32)rn.n << 5) | (u32)nzcv;
    if (a64_p_gpr(c->tok[1], &rm)) {
        if (!sizes_match(c, rn, rm)) return 0;
        if (rm.sp) { err("%s: sp is not allowed here", c->mn); return 0; }
        a64_W(c, base | ((u32)rm.n << 16));
        return 1;
    }
    i64 imm;
    if (!a64_const(c, c->tok[1], &imm, &unk)) return 0;
    if (unk) imm = 0;
    if (imm < 0 || imm > 31) { err("%s: immediate must be 0 to 31", c->mn); return 0; }
    a64_W(c, base | 0x800u | ((u32)imm << 16));
    return 1;
}

/* mul, mneg, smull, umull and friends. */
static int h_mul3(Ctx *c, const MnEnt *e)
{
    NEED(3, 3);
    GReg rd, rn, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !gpr_zr(c, 2, &rm)) return 0;
    if (e->c == 0) {
        if (!sizes_match(c, rd, rn) || !sizes_match(c, rd, rm)) return 0;
        a64_W(c, ((u32)rd.w64 << 31) | 0x1b000000u | ((u32)e->b << 15) | ((u32)rm.n << 16) | (31u << 10) | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
    if (e->c == 1) {
        if (!rd.w64 || rn.w64 || rm.w64) { err("%s: needs an x destination and w sources", c->mn); return 0; }
        a64_W(c, 0x9b200000u | ((u32)e->a << 23) | ((u32)e->b << 15) | ((u32)rm.n << 16) | (31u << 10) | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
    if (!rd.w64 || !rn.w64 || !rm.w64) { err("%s: needs x registers", c->mn); return 0; }
    a64_W(c, 0x9b400000u | ((u32)e->a << 23) | ((u32)rm.n << 16) | (31u << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* madd, msub, smaddl and friends. */
static int h_madd(Ctx *c, const MnEnt *e)
{
    NEED(4, 4);
    GReg rd, rn, rm, ra;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !gpr_zr(c, 2, &rm) || !gpr_zr(c, 3, &ra)) return 0;
    if (e->c == 0) {
        if (!sizes_match(c, rd, rn) || !sizes_match(c, rd, rm) || !sizes_match(c, rd, ra)) return 0;
        a64_W(c, ((u32)rd.w64 << 31) | 0x1b000000u | ((u32)e->b << 15) | ((u32)rm.n << 16) | ((u32)ra.n << 10) | ((u32)rn.n << 5) | (u32)rd.n);
        return 1;
    }
    if (!rd.w64 || rn.w64 || rm.w64 || !ra.w64) { err("%s: needs x for the destination and accumulator, w for the sources", c->mn); return 0; }
    a64_W(c, 0x9b200000u | ((u32)e->a << 23) | ((u32)e->b << 15) | ((u32)rm.n << 16) | ((u32)ra.n << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* sdiv, udiv. */
static int h_div(Ctx *c, const MnEnt *e)
{
    NEED(3, 3);
    GReg rd, rn, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !gpr_zr(c, 2, &rm) || !sizes_match(c, rd, rn) || !sizes_match(c, rd, rm)) return 0;
    a64_W(c, ((u32)rd.w64 << 31) | 0x1ac00000u | ((u32)e->a << 10) | ((u32)rm.n << 16) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* CRC32 instructions. */
static int h_crc(Ctx *c, const MnEnt *e)
{
    NEED(3, 3);
    GReg rd, rn, rm;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !gpr_zr(c, 2, &rm)) return 0;
    int x = e->b == 3;
    if (rd.w64 || rn.w64) { err("%s: the destination and first source must be w registers", c->mn); return 0; }
    if (rm.w64 != x) { err("%s: the data register must be %s", c->mn, x ? "an x register" : "a w register"); return 0; }
    a64_W(c, ((u32)x << 31) | 0x1ac04000u | ((u32)e->a << 12) | ((u32)e->b << 10) | ((u32)rm.n << 16) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* One source data processing (rbit, rev, clz, cls). */
static int h_dp1(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    GReg rd, rn;
    if (!gpr_zr(c, 0, &rd) || !gpr_zr(c, 1, &rn) || !sizes_match(c, rd, rn)) return 0;
    int op = e->a;
    int sf = rd.w64;
    if (e->b == 1 && !sf) { err("%s: needs x registers", c->mn); return 0; }
    if (op == 3 && !sf) op = 2;
    if (e->b == 2) op = sf ? 3 : 2;
    a64_W(c, ((u32)sf << 31) | 0x5ac00000u | ((u32)op << 10) | ((u32)rn.n << 5) | (u32)rd.n);
    return 1;
}

/* br, blr. */
static int h_br(Ctx *c, const MnEnt *e)
{
    NEED(1, 1);
    GReg rn;
    if (!gpr_zr(c, 0, &rn)) return 0;
    if (!rn.w64) { err("%s: needs an x register", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)rn.n << 5));
    return 1;
}

/* ret with an optional register. */
static int h_ret(Ctx *c, const MnEnt *e)
{
    (void)e;
    NEED(0, 1);
    int r = 30;
    if (c->n == 1) {
        GReg rn;
        if (!gpr_zr(c, 0, &rn)) return 0;
        if (!rn.w64) { err("ret: needs an x register", c->mn); return 0; }
        r = rn.n;
    }
    a64_W(c, 0xd65f0000u | ((u32)r << 5));
    return 1;
}

/* Instructions that are one fixed word. */
static int h_fixed(Ctx *c, const MnEnt *e)
{
    NEED(0, 0);
    a64_W(c, (u32)e->a);
    return 1;
}

/* b and bl. */
static int h_b(Ctx *c, const MnEnt *e)
{
    NEED(1, 1);
    Val v;
    if (!a64_eval(c, c->tok[0], &v)) return 0;
    return a64_enc_branch(c, e->a ? 0x94000000u : 0x14000000u, &v, RK_A64_B26, 26, e->a ? 1 : 0, 0);
}

/* cbz and cbnz. */
static int h_cbz(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    GReg rt;
    if (!gpr_zr(c, 0, &rt)) return 0;
    Val v;
    if (!a64_eval(c, c->tok[1], &v)) return 0;
    u32 base = ((u32)rt.w64 << 31) | 0x34000000u | ((u32)e->a << 24) | (u32)rt.n;
    int fits = 1;
    if (a64_same_sec(c, &v)) {
        i64 d = v.n - a64_cur_off(c);
        fits = !(d & 3) && (d >> 2) >= -(1 << 18) && (d >> 2) < (1 << 18);
    }
    if (c->floor >= 8 || !fits) {
        if (c->floor < 8) c->ic->need_wide = 8;
        a64_W(c, (base ^ (1u << 24)) | (2u << 5));
        return a64_enc_branch(c, 0x14000000u, &v, RK_A64_B26, 26, 0, 0);
    }
    return a64_enc_branch(c, base, &v, RK_A64_B19, 19, 0, 5);
}

/* tbz and tbnz. */
static int h_tbz(Ctx *c, const MnEnt *e)
{
    NEED(3, 3);
    GReg rt;
    if (!gpr_zr(c, 0, &rt)) return 0;
    i64 bit;
    int unk;
    if (!a64_const(c, c->tok[1], &bit, &unk)) return 0;
    if (unk) bit = 0;
    if (bit < 0 || bit > 63 || (!rt.w64 && bit > 31)) { err("%s: bit number out of range (0 to %d)", c->mn, rt.w64 ? 63 : 31); return 0; }
    if (!rt.w64 && 0) return 0;
    Val v;
    if (!a64_eval(c, c->tok[2], &v)) return 0;
    int b5 = bit >= 32;
    u32 base = ((u32)b5 << 31) | 0x36000000u | ((u32)e->a << 24) | ((u32)(bit & 31) << 19) | (u32)rt.n;
    int fits = 1;
    if (a64_same_sec(c, &v)) {
        i64 d = v.n - a64_cur_off(c);
        fits = !(d & 3) && (d >> 2) >= -(1 << 13) && (d >> 2) < (1 << 13);
    }
    if (c->floor >= 8 || !fits) {
        if (c->floor < 8) c->ic->need_wide = 8;
        a64_W(c, (base ^ (1u << 24)) | (2u << 5));
        return a64_enc_branch(c, 0x14000000u, &v, RK_A64_B26, 26, 0, 0);
    }
    return a64_enc_branch(c, base, &v, RK_A64_B14, 14, 0, 5);
}

/* svc, hvc, smc, brk, hlt. */
static int h_exc(Ctx *c, const MnEnt *e)
{
    NEED(0, 1);
    i64 imm = 0;
    int unk = 0;
    if (c->n == 1 && !a64_const(c, c->tok[0], &imm, &unk)) return 0;
    if (c->n == 0 && e->b) { err("%s: an immediate is required", c->mn); return 0; }
    if (unk) imm = 0;
    if (imm < 0 || imm > 65535) { err("%s: immediate out of range (0 to 65535)", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)imm << 5));
    return 1;
}

/* nop, yield, wfe, wfi, sev, csdb, esb and other hints. */
static int h_hint(Ctx *c, const MnEnt *e)
{
    int csync = e->a == 17 || e->a == 18;
    if (csync && c->n == 1 && strcasecmp(c->tok[0], "csync")) { err("%s: only `csync' may follow", c->mn); return 0; }
    if (csync && c->n <= 1) { a64_W(c, 0xd503201fu | ((u32)e->a << 5)); return 1; }
    NEED(e->b ? 1 : 0, e->b ? 1 : 0);
    i64 imm = e->a;
    if (e->b) {
        int unk;
        if (!a64_const(c, c->tok[0], &imm, &unk)) return 0;
        if (unk) imm = 0;
        if (imm < 0 || imm > 127) { err("%s: hint number must be 0 to 127", c->mn); return 0; }
    }
    a64_W(c, 0xd503201fu | ((u32)imm << 5));
    return 1;
}

static const struct { const char *n; int v; } barrier_names[] = {
    { "oshld", 1 }, { "oshst", 2 }, { "osh", 3 }, { "nshld", 5 }, { "nshst", 6 }, { "nsh", 7 },
    { "ishld", 9 }, { "ishst", 10 }, { "ish", 11 }, { "ld", 13 }, { "st", 14 }, { "sy", 15 }, { NULL, 0 }
};

/* Parses a barrier option name (sy, ish, ld, st ...). */
static int barrier_opt(Ctx *c, const char *s, int *out)
{
    const char *t = a64_strip_hash(s);
    for (int i = 0; barrier_names[i].n; i++) if (!strcasecmp(t, barrier_names[i].n)) { *out = barrier_names[i].v; return 1; }
    i64 v;
    int unk;
    if (isdigit((unsigned char)t[0]) || t[0] == '-' || t[0] == '(' || t[0] == '$') {
        if (!a64_const(c, s, &v, &unk)) return 0;
        if (v < 0 || v > 15) { err("%s: barrier option out of range (0 to 15)", c->mn); return 0; }
        *out = (int)v;
        return 1;
    }
    err("%s: unknown barrier option `%s'", c->mn, s);
    return 0;
}

/* dmb, dsb, isb. */
static int h_barrier(Ctx *c, const MnEnt *e)
{
    NEED(0, 1);
    int opt = 15;
    if (c->n == 1) { if (!barrier_opt(c, c->tok[0], &opt)) return 0; }
    else if (e->b) { err("%s: a barrier option is required", c->mn); return 0; }
    a64_W(c, (u32)e->a | ((u32)opt << 8));
    return 1;
}

typedef struct { const char *n; int op0, op1, crn, crm, op2; } SysReg;

static const SysReg sysregs[] = {
#include "a64_sysregs.h"
    { NULL, 0, 0, 0, 0, 0 }
};

/* Encodes a system register name (nzcv, tpidr_el0, s3_3_c15_c2_0 ...). */
static int sysreg_enc(Ctx *c, const char *s, u32 *out)
{
    char b[48];
    size_t n = strlen(s);
    if (n >= sizeof b) { err("%s: unknown system register `%s'", c->mn, s); return 0; }
    for (size_t i = 0; i <= n; i++) b[i] = (char)tolower((unsigned char)s[i]);
    for (int i = 0; sysregs[i].n; i++) {
        if (!strcmp(sysregs[i].n, b)) {
            const SysReg *r = &sysregs[i];
            *out = ((u32)(r->op0 & 1) << 19) | ((u32)r->op1 << 16) | ((u32)r->crn << 12) | ((u32)r->crm << 8) | ((u32)r->op2 << 5);
            return 1;
        }
    }
    int a, bb, cc, d, e2;
    if (b[0] == 's' && sscanf(b, "s%d_%d_c%d_c%d_%d", &a, &bb, &cc, &d, &e2) == 5) {
        if (a < 2 || a > 3 || bb < 0 || bb > 7 || cc < 0 || cc > 15 || d < 0 || d > 15 || e2 < 0 || e2 > 7) { err("%s: system register fields out of range in `%s'", c->mn, s); return 0; }
        *out = ((u32)(a & 1) << 19) | ((u32)bb << 16) | ((u32)cc << 12) | ((u32)d << 8) | ((u32)e2 << 5);
        return 1;
    }
    err("%s: unknown system register `%s' (use the s<op0>_<op1>_c<n>_c<m>_<op2> form for others)", c->mn, s);
    return 0;
}

/* mrs and msr. */
static int h_mrs(Ctx *c, const MnEnt *e)
{
    NEED(2, 2);
    GReg rt;
    u32 f;
    if (e->a) {
        if (!gpr_zr(c, 0, &rt) || !sysreg_enc(c, c->tok[1], &f)) return 0;
        if (!rt.w64) { err("mrs: needs an x register"); return 0; }
        a64_W(c, 0xd5300000u | f | (u32)rt.n);
        return 1;
    }
    static const struct { const char *n; int op1, op2; } pst[] = {
        { "spsel", 0, 5 }, { "daifset", 3, 6 }, { "daifclr", 3, 7 }, { "uao", 0, 3 }, { "pan", 0, 4 }, { "dit", 3, 2 }, { "ssbs", 3, 1 }, { "tco", 3, 4 }, { NULL, 0, 0 }
    };
    GReg probe;
    int isreg = a64_p_gpr(c->tok[1], &probe);
    for (int i = 0; pst[i].n && !isreg; i++) {
        if (!strcasecmp(c->tok[0], pst[i].n)) {
            i64 imm;
            int unk;
            if (!a64_const(c, c->tok[1], &imm, &unk)) return 0;
            if (unk) imm = 0;
            if (imm < 0 || imm > 15) { err("msr: immediate out of range (0 to 15)"); return 0; }
            a64_W(c, 0xd500401fu | ((u32)pst[i].op1 << 16) | ((u32)imm << 8) | ((u32)pst[i].op2 << 5));
            return 1;
        }
    }
    static const char *const ro[] = { "ccsidr_el1", "clidr_el1", "cntpct_el0", "cntvct_el0", "ctr_el0", "currentel", "dczid_el0", "icc_hppir1_el1", "icc_iar1_el1", "icc_rpr_el1", "isr_el1", "midr_el1", "mpidr_el1", "revidr_el1", "rndr", "rndrrs", "rvbar_el1", NULL };
    for (int i = 0; ro[i]; i++) if (!strcasecmp(c->tok[0], ro[i])) { err("msr: system register `%s' is read-only", c->tok[0]); return 0; }
    if (!strncasecmp(c->tok[0], "id_", 3)) { err("msr: system register `%s' is read-only", c->tok[0]); return 0; }
    if (!sysreg_enc(c, c->tok[0], &f) || !gpr_zr(c, 1, &rt)) return 0;
    if (!rt.w64) { err("msr: needs an x register"); return 0; }
    a64_W(c, 0xd5100000u | f | (u32)rt.n);
    return 1;
}

/* Reads the four numeric fields of a sys instruction. */
static int sys_fields(Ctx *c, int i, int *op1, int *crn, int *crm, int *op2)
{
    i64 v[4];
    int idx[4] = { i, i + 1, i + 2, i + 3 };
    for (int k = 0; k < 4; k++) {
        const char *t = a64_strip_hash(c->tok[idx[k]]);
        if (k == 1 || k == 2) {
            if ((t[0] == 'c' || t[0] == 'C') && isdigit((unsigned char)t[1])) t++;
            else { err("%s: operand %d: c<n> register expected", c->mn, idx[k] + 1); return 0; }
        }
        char *end;
        long x = strtol(t, &end, 0);
        if (*end || x < 0 || x > (k == 1 || k == 2 ? 15 : 7)) { err("%s: operand %d is out of range", c->mn, idx[k] + 1); return 0; }
        v[k] = x;
    }
    *op1 = (int)v[0];
    *crn = (int)v[1];
    *crm = (int)v[2];
    *op2 = (int)v[3];
    return 1;
}

/* sys and sysl. */
static int h_sys(Ctx *c, const MnEnt *e)
{
    int op1, crn, crm, op2;
    GReg rt = { 31, 1, 0, 1 };
    if (e->a) {
        NEED(5, 5);
        if (!gpr_zr(c, 0, &rt) || !sys_fields(c, 1, &op1, &crn, &crm, &op2)) return 0;
    } else {
        NEED(4, 5);
        if (!sys_fields(c, 0, &op1, &crn, &crm, &op2)) return 0;
        if (c->n == 5 && !gpr_zr(c, 4, &rt)) return 0;
    }
    a64_W(c, (e->a ? 0xd5280000u : 0xd5080000u) | ((u32)op1 << 16) | ((u32)crn << 12) | ((u32)crm << 8) | ((u32)op2 << 5) | (u32)rt.n);
    return 1;
}

typedef struct { const char *n; int op1, crn, crm, op2; int xt; } SysAlias;

static const SysAlias dcnames[] = {
    { "zva", 3, 7, 4, 1, 1 }, { "ivac", 0, 7, 6, 1, 1 }, { "isw", 0, 7, 6, 2, 1 }, { "cvac", 3, 7, 10, 1, 1 }, { "csw", 0, 7, 10, 2, 1 },
    { "cvau", 3, 7, 11, 1, 1 }, { "civac", 3, 7, 14, 1, 1 }, { "cisw", 0, 7, 14, 2, 1 }, { "cvap", 3, 7, 12, 1, 1 }, { "cvadp", 3, 7, 13, 1, 1 },
    { "gva", 3, 7, 4, 3, 1 }, { "gzva", 3, 7, 4, 4, 1 }, { NULL, 0, 0, 0, 0, 0 }
};

static const SysAlias icnames[] = {
    { "ialluis", 0, 7, 1, 0, 0 }, { "iallu", 0, 7, 5, 0, 0 }, { "ivau", 3, 7, 5, 1, 1 }, { NULL, 0, 0, 0, 0, 0 }
};

static const SysAlias tlbinames[] = {
    { "vmalle1is", 0, 8, 3, 0, 0 }, { "vae1is", 0, 8, 3, 1, 1 }, { "aside1is", 0, 8, 3, 2, 1 }, { "vaae1is", 0, 8, 3, 3, 1 },
    { "vale1is", 0, 8, 3, 5, 1 }, { "vaale1is", 0, 8, 3, 7, 1 }, { "vmalle1", 0, 8, 7, 0, 0 }, { "vae1", 0, 8, 7, 1, 1 },
    { "aside1", 0, 8, 7, 2, 1 }, { "vaae1", 0, 8, 7, 3, 1 }, { "vale1", 0, 8, 7, 5, 1 }, { "vaale1", 0, 8, 7, 7, 1 },
    { "alle1is", 4, 8, 3, 4, 0 }, { "alle1", 4, 8, 7, 4, 0 }, { "alle2is", 4, 8, 3, 0, 0 }, { "alle2", 4, 8, 7, 0, 0 },
    { "alle3is", 6, 8, 3, 0, 0 }, { "alle3", 6, 8, 7, 0, 0 }, { "vae2is", 4, 8, 3, 1, 1 }, { "vae2", 4, 8, 7, 1, 1 },
    { "vae3is", 6, 8, 3, 1, 1 }, { "vae3", 6, 8, 7, 1, 1 }, { "vale2is", 4, 8, 3, 5, 1 }, { "vale2", 4, 8, 7, 5, 1 },
    { "vale3is", 6, 8, 3, 5, 1 }, { "vale3", 6, 8, 7, 5, 1 }, { "ipas2e1is", 4, 8, 0, 1, 1 }, { "ipas2e1", 4, 8, 4, 1, 1 },
    { "ipas2le1is", 4, 8, 0, 5, 1 }, { "ipas2le1", 4, 8, 4, 5, 1 }, { "vmalls12e1is", 4, 8, 3, 6, 0 }, { "vmalls12e1", 4, 8, 7, 6, 0 },
    { "vmalle1os", 0, 8, 1, 0, 0 }, { "vae1os", 0, 8, 1, 1, 1 }, { "aside1os", 0, 8, 1, 2, 1 }, { "vaae1os", 0, 8, 1, 3, 1 },
    { "vale1os", 0, 8, 1, 5, 1 }, { "vaale1os", 0, 8, 1, 7, 1 }, { NULL, 0, 0, 0, 0, 0 }
};

static const SysAlias atnames[] = {
    { "s1e1r", 0, 7, 8, 0, 1 }, { "s1e1w", 0, 7, 8, 1, 1 }, { "s1e0r", 0, 7, 8, 2, 1 }, { "s1e0w", 0, 7, 8, 3, 1 },
    { "s1e2r", 4, 7, 8, 0, 1 }, { "s1e2w", 4, 7, 8, 1, 1 }, { "s12e1r", 4, 7, 8, 4, 1 }, { "s12e1w", 4, 7, 8, 5, 1 },
    { "s12e0r", 4, 7, 8, 6, 1 }, { "s12e0w", 4, 7, 8, 7, 1 }, { "s1e3r", 6, 7, 8, 0, 1 }, { "s1e3w", 6, 7, 8, 1, 1 },
    { "s1e1rp", 0, 7, 9, 0, 1 }, { "s1e1wp", 0, 7, 9, 1, 1 }, { NULL, 0, 0, 0, 0, 0 }
};

/* dc, ic, at and tlbi aliases. */
static int h_sysalias(Ctx *c, const MnEnt *e)
{
    NEED(1, 2);
    const SysAlias *t = e->a == 0 ? dcnames : e->a == 1 ? icnames : e->a == 2 ? tlbinames : atnames;
    for (; t->n; t++) {
        if (!strcasecmp(c->tok[0], t->n)) {
            GReg rt = { 31, 1, 0, 1 };
            if (c->n == 2) {
                if (!t->xt) { err("%s %s: takes no register", c->mn, t->n); return 0; }
                if (!gpr_zr(c, 1, &rt)) return 0;
            } else if (t->xt) { err("%s %s: needs a register", c->mn, t->n); return 0; }
            a64_W(c, 0xd5080000u | ((u32)t->op1 << 16) | ((u32)t->crn << 12) | ((u32)t->crm << 8) | ((u32)t->op2 << 5) | (u32)rt.n);
            return 1;
        }
    }
    err("%s: unknown operation `%s'", c->mn, c->tok[0]);
    return 0;
}

#define E(name, fn, a, b, c) { name, fn, a, b, c }

const MnEnt a64_tab_int[] = {
    E("add", h_addsub, 0, 0, 0), E("adds", h_addsub, 0, 1, 0), E("sub", h_addsub, 1, 0, 0), E("subs", h_addsub, 1, 1, 0),
    E("cmp", h_cmp, 1, 0, 0), E("cmn", h_cmp, 0, 0, 0),
    E("neg", h_neg, 0, 0, 0), E("negs", h_neg, 0, 1, 0),
    E("and", h_logic, 0, 0, 0), E("orr", h_logic, 1, 0, 0), E("eor", h_logic, 2, 0, 0), E("ands", h_logic, 3, 0, 0),
    E("bic", h_logic, 0, 1, 0), E("orn", h_logic, 1, 1, 0), E("eon", h_logic, 2, 1, 0), E("bics", h_logic, 3, 1, 0),
    E("tst", h_tst, 0, 0, 0), E("mvn", h_mvn, 0, 0, 0), E("mov", h_mov, 0, 0, 0),
    E("movn", h_movwide, 0, 0, 0), E("movz", h_movwide, 2, 0, 0), E("movk", h_movwide, 3, 0, 0),
    E("adr", h_adr, 0, 0, 0), E("adrp", h_adr, 1, 0, 0),
    E("sbfm", h_bfm, 0, 0, 0), E("bfm", h_bfm, 1, 0, 0), E("ubfm", h_bfm, 2, 0, 0),
    E("sbfx", h_bfx, 0, 0, 0), E("ubfx", h_bfx, 2, 0, 0), E("bfxil", h_bfx, 1, 0, 0),
    E("sbfiz", h_bfx, 0, 1, 0), E("ubfiz", h_bfx, 2, 1, 0), E("bfi", h_bfx, 1, 1, 0), E("bfc", h_bfc, 0, 0, 0),
    E("sxtb", h_ext, 0, 7, 0), E("sxth", h_ext, 0, 15, 0), E("sxtw", h_ext, 0, 31, 0), E("uxtb", h_ext, 0, 7, 1), E("uxth", h_ext, 0, 15, 1),
    E("lsl", h_shift, 0, 0, 0), E("lsr", h_shift, 1, 0, 0), E("asr", h_shift, 2, 0, 0), E("ror", h_shift, 3, 0, 0),
    E("lslv", h_shift, 0, 0, 0), E("lsrv", h_shift, 1, 0, 0), E("asrv", h_shift, 2, 0, 0), E("rorv", h_shift, 3, 0, 0),
    E("extr", h_extr, 0, 0, 0),
    E("adc", h_adc, 0, 0, 0), E("adcs", h_adc, 0, 1, 0), E("sbc", h_adc, 1, 0, 0), E("sbcs", h_adc, 1, 1, 0),
    E("ngc", h_ngc, 0, 0, 0), E("ngcs", h_ngc, 0, 1, 0),
    E("csel", h_csel, 0, 0, 0), E("csinc", h_csel, 0, 1, 0), E("csinv", h_csel, 1, 0, 0), E("csneg", h_csel, 1, 1, 0),
    E("cset", h_cset, 0, 1, 0), E("csetm", h_cset, 1, 0, 0),
    E("cinc", h_cinc, 0, 1, 0), E("cinv", h_cinc, 1, 0, 0), E("cneg", h_cinc, 1, 1, 0),
    E("ccmn", h_ccmp, 0, 0, 0), E("ccmp", h_ccmp, 1, 0, 0),
    E("mul", h_mul3, 0, 0, 0), E("mneg", h_mul3, 0, 1, 0),
    E("smull", h_mul3, 0, 0, 1), E("smnegl", h_mul3, 0, 1, 1), E("umull", h_mul3, 1, 0, 1), E("umnegl", h_mul3, 1, 1, 1),
    E("smulh", h_mul3, 0, 0, 2), E("umulh", h_mul3, 1, 0, 2),
    E("madd", h_madd, 0, 0, 0), E("msub", h_madd, 0, 1, 0),
    E("smaddl", h_madd, 0, 0, 1), E("smsubl", h_madd, 0, 1, 1), E("umaddl", h_madd, 1, 0, 1), E("umsubl", h_madd, 1, 1, 1),
    E("udiv", h_div, 2, 0, 0), E("sdiv", h_div, 3, 0, 0),
    E("crc32b", h_crc, 0, 0, 0), E("crc32h", h_crc, 0, 1, 0), E("crc32w", h_crc, 0, 2, 0), E("crc32x", h_crc, 0, 3, 0),
    E("crc32cb", h_crc, 1, 0, 0), E("crc32ch", h_crc, 1, 1, 0), E("crc32cw", h_crc, 1, 2, 0), E("crc32cx", h_crc, 1, 3, 0),
    E("rbit", h_dp1, 0, 0, 0), E("rev16", h_dp1, 1, 0, 0), E("rev", h_dp1, 3, 0, 0), E("rev32", h_dp1, 2, 1, 0), E("rev64", h_dp1, 3, 1, 0),
    E("clz", h_dp1, 4, 0, 0), E("cls", h_dp1, 5, 0, 0),
    E("b", h_b, 0, 0, 0), E("bl", h_b, 1, 0, 0),
    E("br", h_br, 0xd61f0000, 0, 0), E("blr", h_br, 0xd63f0000, 0, 0), E("ret", h_ret, 0, 0, 0),
    E("eret", h_fixed, 0xd69f03e0, 0, 0), E("drps", h_fixed, 0xd6bf03e0, 0, 0),
    E("cbz", h_cbz, 0, 0, 0), E("cbnz", h_cbz, 1, 0, 0), E("tbz", h_tbz, 0, 0, 0), E("tbnz", h_tbz, 1, 0, 0),
    E("svc", h_exc, 0xd4000001, 1, 0), E("hvc", h_exc, 0xd4000002, 1, 0), E("smc", h_exc, 0xd4000003, 1, 0),
    E("brk", h_exc, 0xd4200000, 1, 0), E("hlt", h_exc, 0xd4400000, 1, 0),
    E("dcps1", h_exc, 0xd4a00001, 0, 0), E("dcps2", h_exc, 0xd4a00002, 0, 0), E("dcps3", h_exc, 0xd4a00003, 0, 0),
    E("nop", h_hint, 0, 0, 0), E("yield", h_hint, 1, 0, 0), E("wfe", h_hint, 2, 0, 0), E("wfi", h_hint, 3, 0, 0),
    E("sev", h_hint, 4, 0, 0), E("sevl", h_hint, 5, 0, 0), E("dgh", h_hint, 6, 0, 0), E("esb", h_hint, 16, 0, 0),
    E("psb", h_hint, 17, 0, 0), E("tsb", h_hint, 18, 0, 0), E("csdb", h_hint, 20, 0, 0), E("hint", h_hint, 0, 1, 0),
    E("paciasp", h_hint, 25, 0, 0), E("pacibsp", h_hint, 27, 0, 0), E("autiasp", h_hint, 29, 0, 0), E("autibsp", h_hint, 31, 0, 0),
    E("paciaz", h_hint, 24, 0, 0), E("pacibz", h_hint, 26, 0, 0), E("autiaz", h_hint, 28, 0, 0), E("autibz", h_hint, 30, 0, 0),
    E("xpaclri", h_hint, 7, 0, 0), E("pacia1716", h_hint, 8, 0, 0), E("pacib1716", h_hint, 10, 0, 0), E("autia1716", h_hint, 12, 0, 0), E("autib1716", h_hint, 14, 0, 0),
    E("dmb", h_barrier, 0xd50330bf, 1, 0), E("dsb", h_barrier, 0xd503309f, 1, 0), E("isb", h_barrier, 0xd50330df, 0, 0), E("clrex", h_barrier, 0xd503305f, 0, 0),
    E("mrs", h_mrs, 1, 0, 0), E("msr", h_mrs, 0, 0, 0),
    E("sys", h_sys, 0, 0, 0), E("sysl", h_sys, 1, 0, 0),
    E("dc", h_sysalias, 0, 0, 0), E("ic", h_sysalias, 1, 0, 0), E("tlbi", h_sysalias, 2, 0, 0), E("at", h_sysalias, 3, 0, 0),
    { NULL, NULL, 0, 0, 0 }
};
