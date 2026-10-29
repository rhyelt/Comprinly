/* RISC-V assembler (RV32 and RV64 with the M, A, F, D, C, Zicsr and common Z extensions). Instructions come from a pattern table; pseudo instructions (li, la, call, mv, branches) are expanded here. Compressed instructions are chosen automatically when the C extension is on. */
#include "cly.h"

typedef struct {
    char mn[48];
    char *tok[10];
    int n;
    IC *ic;
    InsnOut *out;
    int floor;
    int rv64;
    int ext;
    int cmp;
    int opt;
    int mut;
    int aq, rl;
    int must16;
} RC;

typedef struct {
    const char *name;
    u32 base;
    const char *pat;
    int ext;
    int only;
} RvEnt;

/* Offset of the instruction being built. */
static i64 rv_cur(RC *c)
{
    return c->ic->start_off + (i64)c->out->code.n;
}

/* Appends a 32-bit instruction word. */
static void w32(RC *c, u32 w)
{
    badle(&c->out->code, w, 4);
}

/* Appends a 16-bit compressed instruction. */
static void w16(RC *c, u32 w)
{
    badle(&c->out->code, w, 2);
}

/* Adds a relocation to the instruction output. */
static void rv_reloc(RC *c, int kind, const Val *v, i64 off)
{
    InsnOut *o = c->out;
    if (o->nrel >= o->caprel) {
        o->caprel = o->caprel ? o->caprel * 2 : 8;
        o->relocs = xrealloc(o->relocs, sizeof(Reloc) * (size_t)o->caprel);
    }
    Reloc *r = &o->relocs[o->nrel++];
    memset(r, 0, sizeof *r);
    r->off = off;
    r->size = 0;
    r->kind = kind;
    r->v = *v;
}

/* True if a symbol is in the current section (branch distance can be computed now). */
static int same_sec(RC *c, const Val *v)
{
    return !v->unk && v->pos == c->ic->cur_sec && v->neg < 0 && !v->ext && !v->hasreg;
}

/* Value for the current position as a symbol relative offset. */
static void here_val(RC *c, Val *v, i64 off)
{
    val_const(v, off);
    v->pos = c->ic->cur_sec >= 0 ? c->ic->cur_sec : -1;
}

static const char *const xnames[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static const char *const fnames[32] = {
    "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7", "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7",
    "fs2", "fs3", "fs4", "fs5", "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
};

/* Parses a register name with a prefix letter and number (x5, f10). */
static int num_reg(const char *s, char pfx)
{
    if (s[0] != pfx || !isdigit((unsigned char)s[1])) return -1;
    int v = 0;
    const char *p = s + 1;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; if (v > 99) return -1; }
    if (*p || v > 31) return -1;
    if (s[1] == '0' && s[2]) return -1;
    return v;
}

/* Integer register number from any name (x0..x31, zero, ra, sp, a0, t0, s0 ...). -1 if not a register. */
static int rv_xreg(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '$') s++;
    char b[8];
    size_t n = 0;
    for (; s[n] && n < 7; n++) b[n] = (char)tolower((unsigned char)s[n]);
    if (s[n]) return -1;
    b[n] = 0;
    int v = num_reg(b, 'x');
    if (v >= 0) return v;
    if (!strcmp(b, "fp")) return 8;
    for (int i = 0; i < 32; i++) if (!strcmp(b, xnames[i])) return i;
    return -1;
}

/* Floating point register number (f0..f31, ft0, fa0, fs0 ...). -1 if not a register. */
static int rv_freg(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '$') s++;
    char b[8];
    size_t n = 0;
    for (; s[n] && n < 7; n++) b[n] = (char)tolower((unsigned char)s[n]);
    if (s[n]) return -1;
    b[n] = 0;
    int v = num_reg(b, 'f');
    if (v >= 0) return v;
    for (int i = 0; i < 32; i++) if (!strcmp(b, fnames[i])) return i;
    return -1;
}

/* Removes trailing blanks in place. */
static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

/* Splits the operands at top-level commas. */
static int tokenize(const char *rest, RC *c)
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
            trim(buf);
            if (c->n >= 10) { err("too many operands"); return 0; }
            if (!buf[0]) { err("empty operand"); return 0; }
            c->tok[c->n++] = xstrdup(buf);
            n = 0;
            if (end) break;
            continue;
        }
        if (n + 1 >= sizeof buf) { err("operand too long"); return 0; }
        buf[n++] = ch;
    }
    return 1;
}

/* Reports the expected operand form for an instruction. */
static int usage(RC *c, const char *form)
{
    err("%s: expected `%s %s'", c->mn, c->mn, form);
    return 0;
}

/* True if a value may change between passes (unknown or symbol based). */
static int is_mutable(const Val *v)
{
    return v->unk || v->pos >= 0 || v->neg >= 0 || v->ext || v->hasreg;
}

/* Evaluates an operand expression and notes if it is not final yet. */
static int ev(RC *c, const char *s, Val *v)
{
    if (!expr_eval_str(s, v, 0)) return 0;
    if (is_mutable(v)) c->mut = 1;
    return 1;
}

/* Evaluates an expression that must be constant. */
static int const_of(RC *c, const char *s, i64 *out, int *unk)
{
    Val v;
    if (!ev(c, s, &v)) return 0;
    if (unk) *unk = 0;
    if (v.unk) {
        if (unk) *unk = 1;
        *out = 0;
        return 1;
    }
    if (!val_isconst(&v)) { err("%s: constant expression expected here", c->mn); return 0; }
    *out = v.n;
    return 1;
}

/* Operand i as an integer register. */
static int get_x(RC *c, int i, int *r)
{
    int v = rv_xreg(c->tok[i]);
    if (v < 0) { err("%s: operand %d: integer register expected, found `%s'", c->mn, i + 1, c->tok[i]); return 0; }
    *r = v;
    return 1;
}

/* Operand i as a floating point register. */
static int get_f(RC *c, int i, int *r)
{
    int v = rv_freg(c->tok[i]);
    if (v < 0) { err("%s: operand %d: floating-point register expected, found `%s'", c->mn, i + 1, c->tok[i]); return 0; }
    *r = v;
    return 1;
}

enum { RM_NONE, RM_HI, RM_LO, RM_PCHI, RM_PCLO, RM_GOTHI };

/* Recognizes %hi(sym), %lo(sym), %pcrel_hi(sym) and similar operators. */
static int reloc_op(const char *s, int *kind, char *inner, size_t isz)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '%') return 0;
    const char *p = s + 1;
    const char *names[] = { "hi", "lo", "pcrel_hi", "pcrel_lo", "got_pcrel_hi", NULL };
    int ks[] = { RM_HI, RM_LO, RM_PCHI, RM_PCLO, RM_GOTHI };
    for (int i = 0; names[i]; i++) {
        size_t n = strlen(names[i]);
        if (!strncasecmp(p, names[i], n)) {
            const char *q = p + n;
            while (*q == ' ') q++;
            if (*q != '(') return 0;
            q++;
            int d = 1;
            const char *e = q;
            while (*e && d) { if (*e == '(') d++; else if (*e == ')') d--; if (d) e++; }
            if (d) return 0;
            size_t len = (size_t)(e - q);
            if (len >= isz) return 0;
            memcpy(inner, q, len);
            inner[len] = 0;
            const char *tail = e + 1;
            while (*tail == ' ') tail++;
            if (*tail) return -1;
            *kind = ks[i];
            return 1;
        }
    }
    return 0;
}

/* Builds an I-type word. */
static u32 enc_i(u32 base, int rd, int rs1, i64 imm)
{
    return base | ((u32)rd << 7) | ((u32)rs1 << 15) | (((u32)imm & 0xfff) << 20);
}

/* Builds a B-type word (branch offset bits scattered). */
static u32 enc_b(u32 base, int rs1, int rs2, i64 off)
{
    u32 o = (u32)off;
    return base | (((o >> 11) & 1) << 7) | (((o >> 1) & 15) << 8) | ((u32)rs1 << 15) | ((u32)rs2 << 20) | (((o >> 5) & 0x3f) << 25) | (((o >> 12) & 1) << 31);
}

/* Builds a J-type word. */
static u32 enc_j(u32 base, int rd, i64 off)
{
    u32 o = (u32)off;
    return base | ((u32)rd << 7) | (((o >> 12) & 0xff) << 12) | (((o >> 11) & 1) << 20) | (((o >> 1) & 0x3ff) << 21) | (((o >> 20) & 1) << 31);
}

/* True if v fits a signed field of the given width. */
static int fits_s(i64 v, int bits)
{
    i64 lim = (i64)1 << (bits - 1);
    return v >= -lim && v < lim;
}

static int cr3(int r) { return r >= 8 && r <= 15; }

/* Scatters a jump offset into the compressed c.j format. */
static u32 cj_bits(i64 off)
{
    u32 o = (u32)off;
    return (((o >> 11) & 1) << 12) | (((o >> 4) & 1) << 11) | (((o >> 8) & 3) << 9) | (((o >> 10) & 1) << 8) | (((o >> 6) & 1) << 7) | (((o >> 7) & 1) << 6) | (((o >> 1) & 7) << 3) | (((o >> 5) & 1) << 2);
}

/* Scatters a branch offset into the compressed c.beqz format. */
static u32 cb_bits(i64 off, int rs)
{
    u32 o = (u32)off;
    return (((o >> 8) & 1) << 12) | (((o >> 3) & 3) << 10) | ((u32)(rs - 8) << 7) | (((o >> 6) & 3) << 5) | (((o >> 1) & 3) << 3) | (((o >> 5) & 1) << 2);
}

/* Tries to turn a 32-bit instruction into its 16-bit compressed form. Returns 0 if there is none. */
static int compress(RC *c, u32 w, u32 *h)
{
    int rv64 = c->rv64;
    u32 op = w & 0x7f;
    int rd = (int)((w >> 7) & 31);
    u32 f3 = (w >> 12) & 7;
    int rs1 = (int)((w >> 15) & 31);
    int rs2 = (int)((w >> 20) & 31);
    u32 f7 = w >> 25;
    i64 imm = (i64)(int32_t)w >> 20;
    i64 simm = (((i64)(int32_t)(w & 0xfe000000u)) >> 20) | ((w >> 7) & 31);
    switch (op) {
    case 0x13:
        if (f3 == 0) {
            if (rd == 0 && rs1 == 0 && imm == 0) { *h = 0x0001; return 1; }
            if (rs1 == 2 && cr3(rd) && imm > 0 && (imm & 3) == 0 && imm < 1024) {
                u32 m = (u32)imm;
                *h = (((m >> 4) & 3) << 11) | (((m >> 6) & 15) << 7) | (((m >> 2) & 1) << 6) | (((m >> 3) & 1) << 5) | ((u32)(rd - 8) << 2);
                return 1;
            }
            if (rd == rs1 && rd != 0 && imm != 0 && imm >= -32 && imm <= 31) {
                *h = 0x0001u | ((((u32)imm >> 5) & 1) << 12) | ((u32)rd << 7) | (((u32)imm & 31) << 2);
                return 1;
            }
            if (rd == 2 && rs1 == 2 && imm != 0 && (imm & 15) == 0 && imm >= -512 && imm <= 496) {
                u32 m = (u32)imm;
                *h = 0x6101u | (((m >> 9) & 1) << 12) | (((m >> 4) & 1) << 6) | (((m >> 6) & 1) << 5) | (((m >> 7) & 3) << 3) | (((m >> 5) & 1) << 2);
                return 1;
            }
            if (rs1 == 0 && rd != 0 && imm >= -32 && imm <= 31) {
                *h = 0x4001u | ((((u32)imm >> 5) & 1) << 12) | ((u32)rd << 7) | (((u32)imm & 31) << 2);
                return 1;
            }
            if (imm == 0 && rd != 0 && rs1 != 0) { *h = 0x8002u | ((u32)rd << 7) | ((u32)rs1 << 2); return 1; }
            return 0;
        }
        if (f3 == 1) {
            u32 sh = (w >> 20) & (rv64 ? 63 : 31);
            if ((w >> 26) != 0 || (!rv64 && ((w >> 25) & 1))) return 0;
            if (rd == rs1 && rd != 0 && sh != 0) { *h = 0x0002u | ((sh >> 5) << 12) | ((u32)rd << 7) | ((sh & 31) << 2); return 1; }
            return 0;
        }
        if (f3 == 5) {
            u32 sh = (w >> 20) & (rv64 ? 63 : 31);
            if (!rv64 && ((w >> 25) & 1)) return 0;
            u32 hi = w >> 26;
            if (rd != rs1 || !cr3(rd) || sh == 0) return 0;
            if (hi == 0) { *h = 0x8001u | ((sh >> 5) << 12) | ((u32)(rd - 8) << 7) | ((sh & 31) << 2); return 1; }
            if (hi == 0x10) { *h = 0x8401u | ((sh >> 5) << 12) | ((u32)(rd - 8) << 7) | ((sh & 31) << 2); return 1; }
            return 0;
        }
        if (f3 == 7) {
            if (rd == rs1 && cr3(rd) && imm >= -32 && imm <= 31) {
                *h = 0x8801u | ((((u32)imm >> 5) & 1) << 12) | ((u32)(rd - 8) << 7) | (((u32)imm & 31) << 2);
                return 1;
            }
        }
        return 0;
    case 0x1b:
        if (rv64 && f3 == 0 && rd == rs1 && rd != 0 && imm >= -32 && imm <= 31) {
            *h = 0x2001u | ((((u32)imm >> 5) & 1) << 12) | ((u32)rd << 7) | (((u32)imm & 31) << 2);
            return 1;
        }
        return 0;
    case 0x37: {
        i64 v = (i64)(int32_t)(w & 0xfffff000u) >> 12;
        if (rd != 0 && rd != 2 && v != 0 && v >= -32 && v <= 31) {
            *h = 0x6001u | ((((u32)v >> 5) & 1) << 12) | ((u32)rd << 7) | (((u32)v & 31) << 2);
            return 1;
        }
        return 0;
    }
    case 0x33:
        if (f7 == 0 && f3 == 0 && rd != 0) {
            if (rs1 == 0 && rs2 != 0) { *h = 0x8002u | ((u32)rd << 7) | ((u32)rs2 << 2); return 1; }
            if (rd == rs1 && rs2 != 0) { *h = 0x9002u | ((u32)rd << 7) | ((u32)rs2 << 2); return 1; }
            if (rd == rs2 && rs1 != 0) { *h = 0x9002u | ((u32)rd << 7) | ((u32)rs1 << 2); return 1; }
            return 0;
        }
        if (f7 == 0x20 && f3 == 0 && rd == rs1 && cr3(rd) && cr3(rs2)) { *h = 0x8c01u | ((u32)(rd - 8) << 7) | ((u32)(rs2 - 8) << 2); return 1; }
        if (f7 == 0 && (f3 == 4 || f3 == 6 || f3 == 7) && cr3(rd) && cr3(rs1) && cr3(rs2)) {
            u32 sel = f3 == 4 ? 0x8c21u : f3 == 6 ? 0x8c41u : 0x8c61u;
            if (rd == rs1) { *h = sel | ((u32)(rd - 8) << 7) | ((u32)(rs2 - 8) << 2); return 1; }
            if (rd == rs2) { *h = sel | ((u32)(rd - 8) << 7) | ((u32)(rs1 - 8) << 2); return 1; }
        }
        return 0;
    case 0x3b:
        if (rv64 && f7 == 0 && f3 == 0 && cr3(rd) && cr3(rs1) && cr3(rs2) && rd != rs1 && rd == rs2) { *h = 0x9c21u | ((u32)(rd - 8) << 7) | ((u32)(rs1 - 8) << 2); return 1; }
        if (!rv64 || rd != rs1 || !cr3(rd) || !cr3(rs2)) return 0;
        if (f7 == 0x20 && f3 == 0) { *h = 0x9c01u | ((u32)(rd - 8) << 7) | ((u32)(rs2 - 8) << 2); return 1; }
        if (f7 == 0 && f3 == 0) { *h = 0x9c21u | ((u32)(rd - 8) << 7) | ((u32)(rs2 - 8) << 2); return 1; }
        return 0;
    case 0x03:
        if (f3 == 2) {
            if (rs1 == 2 && rd != 0 && imm >= 0 && imm < 256 && (imm & 3) == 0) {
                u32 m = (u32)imm;
                *h = 0x4002u | (((m >> 5) & 1) << 12) | ((u32)rd << 7) | (((m >> 2) & 7) << 4) | (((m >> 6) & 3) << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rd) && imm >= 0 && imm < 128 && (imm & 3) == 0) {
                u32 m = (u32)imm;
                *h = 0x4000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 2) & 1) << 6) | (((m >> 6) & 1) << 5) | ((u32)(rd - 8) << 2);
                return 1;
            }
        }
        if (f3 == 3 && rv64) {
            if (rs1 == 2 && rd != 0 && imm >= 0 && imm < 512 && (imm & 7) == 0) {
                u32 m = (u32)imm;
                *h = 0x6002u | (((m >> 5) & 1) << 12) | ((u32)rd << 7) | (((m >> 3) & 3) << 5) | (((m >> 6) & 7) << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rd) && imm >= 0 && imm < 256 && (imm & 7) == 0) {
                u32 m = (u32)imm;
                *h = 0x6000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 6) & 3) << 5) | ((u32)(rd - 8) << 2);
                return 1;
            }
        }
        return 0;
    case 0x07:
        if (f3 == 2 && !rv64 && (c->ext & RVX_F)) {
            if (rs1 == 2 && imm >= 0 && imm < 256 && (imm & 3) == 0) {
                u32 m = (u32)imm;
                *h = 0x6002u | (((m >> 5) & 1) << 12) | ((u32)rd << 7) | (((m >> 2) & 7) << 4) | (((m >> 6) & 3) << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rd) && imm >= 0 && imm < 128 && (imm & 3) == 0) {
                u32 m = (u32)imm;
                *h = 0x6000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 2) & 1) << 6) | (((m >> 6) & 1) << 5) | ((u32)(rd - 8) << 2);
                return 1;
            }
        }
        if (f3 == 3 && (c->ext & RVX_D)) {
            if (rs1 == 2 && imm >= 0 && imm < 512 && (imm & 7) == 0) {
                u32 m = (u32)imm;
                *h = 0x2002u | (((m >> 5) & 1) << 12) | ((u32)rd << 7) | (((m >> 3) & 3) << 5) | (((m >> 6) & 7) << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rd) && imm >= 0 && imm < 256 && (imm & 7) == 0) {
                u32 m = (u32)imm;
                *h = 0x2000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 6) & 3) << 5) | ((u32)(rd - 8) << 2);
                return 1;
            }
        }
        return 0;
    case 0x23:
        if (f3 == 2) {
            if (rs1 == 2 && simm >= 0 && simm < 256 && (simm & 3) == 0) {
                u32 m = (u32)simm;
                *h = 0xc002u | (((m >> 2) & 15) << 9) | (((m >> 6) & 3) << 7) | ((u32)rs2 << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rs2) && simm >= 0 && simm < 128 && (simm & 3) == 0) {
                u32 m = (u32)simm;
                *h = 0xc000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 2) & 1) << 6) | (((m >> 6) & 1) << 5) | ((u32)(rs2 - 8) << 2);
                return 1;
            }
        }
        if (f3 == 3 && rv64) {
            if (rs1 == 2 && simm >= 0 && simm < 512 && (simm & 7) == 0) {
                u32 m = (u32)simm;
                *h = 0xe002u | (((m >> 3) & 7) << 10) | (((m >> 6) & 7) << 7) | ((u32)rs2 << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rs2) && simm >= 0 && simm < 256 && (simm & 7) == 0) {
                u32 m = (u32)simm;
                *h = 0xe000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 6) & 3) << 5) | ((u32)(rs2 - 8) << 2);
                return 1;
            }
        }
        return 0;
    case 0x27:
        if (f3 == 2 && !rv64 && (c->ext & RVX_F)) {
            if (rs1 == 2 && simm >= 0 && simm < 256 && (simm & 3) == 0) {
                u32 m = (u32)simm;
                *h = 0xe002u | (((m >> 2) & 15) << 9) | (((m >> 6) & 3) << 7) | ((u32)rs2 << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rs2) && simm >= 0 && simm < 128 && (simm & 3) == 0) {
                u32 m = (u32)simm;
                *h = 0xe000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 2) & 1) << 6) | (((m >> 6) & 1) << 5) | ((u32)(rs2 - 8) << 2);
                return 1;
            }
        }
        if (f3 == 3 && (c->ext & RVX_D)) {
            if (rs1 == 2 && simm >= 0 && simm < 512 && (simm & 7) == 0) {
                u32 m = (u32)simm;
                *h = 0xa002u | (((m >> 3) & 7) << 10) | (((m >> 6) & 7) << 7) | ((u32)rs2 << 2);
                return 1;
            }
            if (cr3(rs1) && cr3(rs2) && simm >= 0 && simm < 256 && (simm & 7) == 0) {
                u32 m = (u32)simm;
                *h = 0xa000u | (((m >> 3) & 7) << 10) | ((u32)(rs1 - 8) << 7) | (((m >> 6) & 3) << 5) | ((u32)(rs2 - 8) << 2);
                return 1;
            }
        }
        return 0;
    case 0x67:
        if (f3 == 0 && imm == 0 && rs1 != 0) {
            if (rd == 0) { *h = 0x8002u | ((u32)rs1 << 7); return 1; }
            if (rd == 1) { *h = 0x9002u | ((u32)rs1 << 7); return 1; }
        }
        return 0;
    case 0x73:
        if (w == 0x00100073u) { *h = 0x9002u; return 1; }
        return 0;
    }
    return 0;
}

/* compress, but only when compressed code is enabled. */
static int rv_try_c(RC *c, u32 w, u32 *h)
{
    if (!c->cmp) return 0;
    return compress(c, w, h);
}

/* Emits an instruction, compressed when allowed and when the size is not fixed. */
static void emit(RC *c, u32 w)
{
    u32 h;
    if (c->floor < 4 && rv_try_c(c, w, &h)) { w16(c, h); return; }
    w32(c, w);
}

/* Emits a full 32-bit instruction regardless of compression. */
static void emit4(RC *c, u32 w)
{
    w32(c, w);
}

/* Emits an instruction of a sequence, compressed if possible. */
static void emit_seq(RC *c, u32 w)
{
    u32 h;
    if (rv_try_c(c, w, &h)) w16(c, h);
    else w32(c, w);
}

/* Pads the output to the size chosen in an earlier pass so layout stays stable. */
static void finish(RC *c)
{
    int n = (int)c->out->code.n;
    while (n < c->floor) {
        if (c->floor - n >= 4 || !c->cmp) { w32(c, 0x00000013u); n += 4; }
        else { w16(c, 0x0001u); n += 2; }
    }
    if (n > c->ic->wide && (n > 4 || (n == 4 && c->mut && c->cmp))) c->ic->need_wide = n;
}

#define RV_NOP 0x00000013u

static const struct { const char *n; int v; } csrs[] = {
    { "ustatus", 0x000 }, { "uie", 0x004 }, { "utvec", 0x005 }, { "uscratch", 0x040 }, { "uepc", 0x041 }, { "ucause", 0x042 }, { "utval", 0x043 }, { "uip", 0x044 },
    { "fflags", 0x001 }, { "frm", 0x002 }, { "fcsr", 0x003 }, { "vstart", 0x008 }, { "vxsat", 0x009 }, { "vxrm", 0x00a }, { "vcsr", 0x00f },
    { "seed", 0x015 }, { "cycle", 0xc00 }, { "time", 0xc01 }, { "instret", 0xc02 }, { "cycleh", 0xc80 }, { "timeh", 0xc81 }, { "instreth", 0xc82 },
    { "vl", 0xc20 }, { "vtype", 0xc21 }, { "vlenb", 0xc22 },
    { "sstatus", 0x100 }, { "sedeleg", 0x102 }, { "sideleg", 0x103 }, { "sie", 0x104 }, { "stvec", 0x105 }, { "scounteren", 0x106 }, { "senvcfg", 0x10a },
    { "sscratch", 0x140 }, { "sepc", 0x141 }, { "scause", 0x142 }, { "stval", 0x143 }, { "sip", 0x144 }, { "stimecmp", 0x14d }, { "satp", 0x180 },
    { "hstatus", 0x600 }, { "hedeleg", 0x602 }, { "hideleg", 0x603 }, { "hie", 0x604 }, { "htimedelta", 0x605 }, { "hcounteren", 0x606 }, { "hgeie", 0x607 },
    { "htval", 0x643 }, { "hip", 0x644 }, { "hvip", 0x645 }, { "htinst", 0x64a }, { "hgatp", 0x680 },
    { "vsstatus", 0x200 }, { "vsie", 0x204 }, { "vstvec", 0x205 }, { "vsscratch", 0x240 }, { "vsepc", 0x241 }, { "vscause", 0x242 }, { "vstval", 0x243 }, { "vsip", 0x244 }, { "vsatp", 0x280 },
    { "mvendorid", 0xf11 }, { "marchid", 0xf12 }, { "mimpid", 0xf13 }, { "mhartid", 0xf14 }, { "mconfigptr", 0xf15 },
    { "mstatus", 0x300 }, { "misa", 0x301 }, { "medeleg", 0x302 }, { "mideleg", 0x303 }, { "mie", 0x304 }, { "mtvec", 0x305 }, { "mcounteren", 0x306 },
    { "mstatush", 0x310 }, { "menvcfg", 0x30a }, { "mscratch", 0x340 }, { "mepc", 0x341 }, { "mcause", 0x342 }, { "mtval", 0x343 }, { "mip", 0x344 },
    { "mtinst", 0x34a }, { "mtval2", 0x34b }, { "mcountinhibit", 0x320 },
    { "mcycle", 0xb00 }, { "minstret", 0xb02 }, { "mcycleh", 0xb80 }, { "minstreth", 0xb82 },
    { "tselect", 0x7a0 }, { "tdata1", 0x7a1 }, { "tdata2", 0x7a2 }, { "tdata3", 0x7a3 }, { "dcsr", 0x7b0 }, { "dpc", 0x7b1 }, { "dscratch0", 0x7b2 }, { "dscratch1", 0x7b3 },
    { NULL, 0 }
};

/* CSR number from a name (mstatus, cycle ...) or a constant. */
static int csr_num(RC *c, const char *s, int *out)
{
    for (int i = 0; csrs[i].n; i++) if (!strcasecmp(s, csrs[i].n)) { *out = csrs[i].v; return 1; }
    char buf[24];
    if (!strncasecmp(s, "pmpcfg", 6) && isdigit((unsigned char)s[6])) {
        int k = atoi(s + 6);
        if (k >= 0 && k < 16) { *out = 0x3a0 + k; return 1; }
    }
    if (!strncasecmp(s, "pmpaddr", 7) && isdigit((unsigned char)s[7])) {
        int k = atoi(s + 7);
        if (k >= 0 && k < 64) { *out = 0x3b0 + k; return 1; }
    }
    if (!strncasecmp(s, "hpmcounter", 10) && isdigit((unsigned char)s[10])) {
        int k = atoi(s + 10);
        int h = strchr(s + 10, 'h') != NULL;
        if (k >= 3 && k < 32) { *out = (h ? 0xc80 : 0xc00) + k; return 1; }
    }
    if (!strncasecmp(s, "mhpmcounter", 11) && isdigit((unsigned char)s[11])) {
        int k = atoi(s + 11);
        int h = strchr(s + 11, 'h') != NULL;
        if (k >= 3 && k < 32) { *out = (h ? 0xb80 : 0xb00) + k; return 1; }
    }
    if (!strncasecmp(s, "mhpmevent", 9) && isdigit((unsigned char)s[9])) {
        int k = atoi(s + 9);
        if (k >= 3 && k < 32) { *out = 0x320 + k; return 1; }
    }
    i64 v;
    int unk;
    snprintf(buf, sizeof buf, "%s", s);
    if (!const_of(c, buf, &v, &unk)) return 0;
    if (!unk && (v < 0 || v > 0xfff)) { err("%s: CSR number %lld is out of range (0 to 4095)", c->mn, (long long)v); return 0; }
    *out = (int)v;
    return 1;
}

static const char *const rmnames[] = { "rne", "rtz", "rdn", "rup", "rmm", "", "", "dyn" };

/* Rounding mode from its name (rne, rtz, rdn, rup, rmm, dyn). */
static int rm_of(const char *s)
{
    for (int i = 0; i < 8; i++) if (rmnames[i][0] && !strcasecmp(s, rmnames[i])) return i;
    return -1;
}

#define F(f7, f3, op) (((u32)(f7) << 25) | ((u32)(f3) << 12) | (op))
#define FP2(n, f5, f3, pat) \
    { n ".s", (((u32)(f5)) << 27) | ((u32)(f3) << 12) | 0x53, pat, RVX_F, 0 }, \
    { n ".d", (((u32)(f5)) << 27) | (1u << 25) | ((u32)(f3) << 12) | 0x53, pat, RVX_D, 0 }
#define FPH2(n, f5, f3, pat) \
    { n ".h", (((u32)(f5)) << 27) | (2u << 25) | ((u32)(f3) << 12) | 0x53, pat, RVX_ZFH, 0 }
#define FP4(n, op, pat) \
    { n ".s", (op), pat, RVX_F, 0 }, \
    { n ".d", (op) | (1u << 25), pat, RVX_D, 0 }

static const RvEnt rv_tab[] = {
    { "lui", 0x37, "d,u", RVX_I, 0 }, { "auipc", 0x17, "d,u", RVX_I, 0 },
    { "lb", 0x03, "d,l", RVX_I, 0 }, { "lh", 0x1003, "d,l", RVX_I, 0 }, { "lw", 0x2003, "d,l", RVX_I, 0 }, { "ld", 0x3003, "d,l", RVX_I, 2 },
    { "lbu", 0x4003, "d,l", RVX_I, 0 }, { "lhu", 0x5003, "d,l", RVX_I, 0 }, { "lwu", 0x6003, "d,l", RVX_I, 2 },
    { "sb", 0x23, "t,m", RVX_I, 0 }, { "sh", 0x1023, "t,m", RVX_I, 0 }, { "sw", 0x2023, "t,m", RVX_I, 0 }, { "sd", 0x3023, "t,m", RVX_I, 2 },
    { "addi", 0x13, "d,s,i", RVX_I, 0 }, { "slti", 0x2013, "d,s,i", RVX_I, 0 }, { "sltiu", 0x3013, "d,s,i", RVX_I, 0 },
    { "xori", 0x4013, "d,s,i", RVX_I, 0 }, { "ori", 0x6013, "d,s,i", RVX_I, 0 }, { "andi", 0x7013, "d,s,i", RVX_I, 0 },
    { "slli", 0x1013, "d,s,h", RVX_I, 0 }, { "srli", 0x5013, "d,s,h", RVX_I, 0 }, { "srai", 0x40005013, "d,s,h", RVX_I, 0 },
    { "add", F(0, 0, 0x33), "d,s,t", RVX_I, 0 }, { "sub", F(0x20, 0, 0x33), "d,s,t", RVX_I, 0 }, { "sll", F(0, 1, 0x33), "d,s,t", RVX_I, 0 },
    { "slt", F(0, 2, 0x33), "d,s,t", RVX_I, 0 }, { "sltu", F(0, 3, 0x33), "d,s,t", RVX_I, 0 }, { "xor", F(0, 4, 0x33), "d,s,t", RVX_I, 0 },
    { "srl", F(0, 5, 0x33), "d,s,t", RVX_I, 0 }, { "sra", F(0x20, 5, 0x33), "d,s,t", RVX_I, 0 }, { "or", F(0, 6, 0x33), "d,s,t", RVX_I, 0 },
    { "and", F(0, 7, 0x33), "d,s,t", RVX_I, 0 },
    { "addiw", 0x1b, "d,s,i", RVX_I, 2 }, { "slliw", 0x101b, "d,s,w", RVX_I, 2 }, { "srliw", 0x501b, "d,s,w", RVX_I, 2 }, { "sraiw", 0x4000501b, "d,s,w", RVX_I, 2 },
    { "addw", F(0, 0, 0x3b), "d,s,t", RVX_I, 2 }, { "subw", F(0x20, 0, 0x3b), "d,s,t", RVX_I, 2 }, { "sllw", F(0, 1, 0x3b), "d,s,t", RVX_I, 2 },
    { "srlw", F(0, 5, 0x3b), "d,s,t", RVX_I, 2 }, { "sraw", F(0x20, 5, 0x3b), "d,s,t", RVX_I, 2 },
    { "mul", F(1, 0, 0x33), "d,s,t", RVX_M, 0 }, { "mulh", F(1, 1, 0x33), "d,s,t", RVX_M, 0 }, { "mulhsu", F(1, 2, 0x33), "d,s,t", RVX_M, 0 },
    { "mulhu", F(1, 3, 0x33), "d,s,t", RVX_M, 0 }, { "div", F(1, 4, 0x33), "d,s,t", RVX_M, 0 }, { "divu", F(1, 5, 0x33), "d,s,t", RVX_M, 0 },
    { "rem", F(1, 6, 0x33), "d,s,t", RVX_M, 0 }, { "remu", F(1, 7, 0x33), "d,s,t", RVX_M, 0 },
    { "mulw", F(1, 0, 0x3b), "d,s,t", RVX_M, 2 }, { "divw", F(1, 4, 0x3b), "d,s,t", RVX_M, 2 }, { "divuw", F(1, 5, 0x3b), "d,s,t", RVX_M, 2 },
    { "remw", F(1, 6, 0x3b), "d,s,t", RVX_M, 2 }, { "remuw", F(1, 7, 0x3b), "d,s,t", RVX_M, 2 },
    { "ecall", 0x73, "", RVX_I, 0 }, { "ebreak", 0x100073, "", RVX_I, 0 }, { "uret", 0x200073, "", RVX_I, 0 }, { "sret", 0x10200073, "", RVX_I, 0 },
    { "mret", 0x30200073, "", RVX_I, 0 }, { "wfi", 0x10500073, "", RVX_I, 0 }, { "dret", 0x7b200073, "", RVX_I, 0 },
    { "fence.i", 0x100f, "", RVX_ZIFENCEI, 0 }, { "fence.tso", 0x8330000f, "", RVX_I, 0 }, { "pause", 0x0100000f, "", RVX_I, 0 },
    { "csrrw", 0x1073, "d,c,s", RVX_ZICSR, 0 }, { "csrrs", 0x2073, "d,c,s", RVX_ZICSR, 0 }, { "csrrc", 0x3073, "d,c,s", RVX_ZICSR, 0 },
    { "csrrwi", 0x5073, "d,c,z", RVX_ZICSR, 0 }, { "csrrsi", 0x6073, "d,c,z", RVX_ZICSR, 0 }, { "csrrci", 0x7073, "d,c,z", RVX_ZICSR, 0 },
    { "lr.w", 0x1000202f, "d,a", RVX_A, 0 }, { "sc.w", 0x1800202f, "d,t,a", RVX_A, 0 },
    { "lr.d", 0x1000302f, "d,a", RVX_A, 2 }, { "sc.d", 0x1800302f, "d,t,a", RVX_A, 2 },
    { "amoswap.w", 0x0800202f, "d,t,a", RVX_A, 0 }, { "amoadd.w", 0x0000202f, "d,t,a", RVX_A, 0 }, { "amoxor.w", 0x2000202f, "d,t,a", RVX_A, 0 },
    { "amoand.w", 0x6000202f, "d,t,a", RVX_A, 0 }, { "amoor.w", 0x4000202f, "d,t,a", RVX_A, 0 }, { "amomin.w", 0x8000202f, "d,t,a", RVX_A, 0 },
    { "amomax.w", 0xa000202f, "d,t,a", RVX_A, 0 }, { "amominu.w", 0xc000202f, "d,t,a", RVX_A, 0 }, { "amomaxu.w", 0xe000202f, "d,t,a", RVX_A, 0 },
    { "amoswap.d", 0x0800302f, "d,t,a", RVX_A, 2 }, { "amoadd.d", 0x0000302f, "d,t,a", RVX_A, 2 }, { "amoxor.d", 0x2000302f, "d,t,a", RVX_A, 2 },
    { "amoand.d", 0x6000302f, "d,t,a", RVX_A, 2 }, { "amoor.d", 0x4000302f, "d,t,a", RVX_A, 2 }, { "amomin.d", 0x8000302f, "d,t,a", RVX_A, 2 },
    { "amomax.d", 0xa000302f, "d,t,a", RVX_A, 2 }, { "amominu.d", 0xc000302f, "d,t,a", RVX_A, 2 }, { "amomaxu.d", 0xe000302f, "d,t,a", RVX_A, 2 },
    { "flw", 0x2007, "D,l", RVX_F, 0 }, { "fld", 0x3007, "D,l", RVX_D, 0 }, { "fsw", 0x2027, "T,m", RVX_F, 0 }, { "fsd", 0x3027, "T,m", RVX_D, 0 },
    FP4("fmadd", 0x43, "D,S,T,U,r"), FP4("fmsub", 0x47, "D,S,T,U,r"), FP4("fnmsub", 0x4b, "D,S,T,U,r"), FP4("fnmadd", 0x4f, "D,S,T,U,r"),
    FP2("fadd", 0x00, 0, "D,S,T,r"), FP2("fsub", 0x01, 0, "D,S,T,r"), FP2("fmul", 0x02, 0, "D,S,T,r"), FP2("fdiv", 0x03, 0, "D,S,T,r"),
    FP2("fsqrt", 0x0b, 0, "D,S,r"),
    FP2("fsgnj", 0x04, 0, "D,S,T"), FP2("fsgnjn", 0x04, 1, "D,S,T"), FP2("fsgnjx", 0x04, 2, "D,S,T"),
    FP2("fmin", 0x05, 0, "D,S,T"), FP2("fmax", 0x05, 1, "D,S,T"),
    FP2("feq", 0x14, 2, "d,S,T"), FP2("flt", 0x14, 1, "d,S,T"), FP2("fle", 0x14, 0, "d,S,T"),
    FP2("fclass", 0x1c, 1, "d,S"),
    { "fcvt.s.d", 0x40100053, "D,S,r", RVX_D, 0 }, { "fcvt.d.s", 0x42000053, "D,S", RVX_D, 0 },
    { "fcvt.w.s", 0xc0000053, "d,S,r", RVX_F, 0 }, { "fcvt.wu.s", 0xc0100053, "d,S,r", RVX_F, 0 },
    { "fcvt.l.s", 0xc0200053, "d,S,r", RVX_F, 2 }, { "fcvt.lu.s", 0xc0300053, "d,S,r", RVX_F, 2 },
    { "fcvt.s.w", 0xd0000053, "D,s,r", RVX_F, 0 }, { "fcvt.s.wu", 0xd0100053, "D,s,r", RVX_F, 0 },
    { "fcvt.s.l", 0xd0200053, "D,s,r", RVX_F, 2 }, { "fcvt.s.lu", 0xd0300053, "D,s,r", RVX_F, 2 },
    { "fcvt.w.d", 0xc2000053, "d,S,r", RVX_D, 0 }, { "fcvt.wu.d", 0xc2100053, "d,S,r", RVX_D, 0 },
    { "fcvt.l.d", 0xc2200053, "d,S,r", RVX_D, 2 }, { "fcvt.lu.d", 0xc2300053, "d,S,r", RVX_D, 2 },
    { "fcvt.d.w", 0xd2000053, "D,s", RVX_D, 0 }, { "fcvt.d.wu", 0xd2100053, "D,s", RVX_D, 0 },
    { "fcvt.d.l", 0xd2200053, "D,s,r", RVX_D, 2 }, { "fcvt.d.lu", 0xd2300053, "D,s,r", RVX_D, 2 },
    { "fmv.x.w", 0xe0000053, "d,S", RVX_F, 0 }, { "fmv.w.x", 0xf0000053, "D,s", RVX_F, 0 },
    { "fmv.x.d", 0xe2000053, "d,S", RVX_D, 2 }, { "fmv.d.x", 0xf2000053, "D,s", RVX_D, 2 },
    { "andn", F(0x20, 7, 0x33), "d,s,t", RVX_ZBB, 0 }, { "orn", F(0x20, 6, 0x33), "d,s,t", RVX_ZBB, 0 }, { "xnor", F(0x20, 4, 0x33), "d,s,t", RVX_ZBB, 0 },
    { "clz", 0x60001013, "d,s", RVX_ZBB, 0 }, { "ctz", 0x60101013, "d,s", RVX_ZBB, 0 }, { "cpop", 0x60201013, "d,s", RVX_ZBB, 0 },
    { "clzw", 0x6000101b, "d,s", RVX_ZBB, 2 }, { "ctzw", 0x6010101b, "d,s", RVX_ZBB, 2 }, { "cpopw", 0x6020101b, "d,s", RVX_ZBB, 2 },
    { "max", F(5, 6, 0x33), "d,s,t", RVX_ZBB, 0 }, { "maxu", F(5, 7, 0x33), "d,s,t", RVX_ZBB, 0 }, { "min", F(5, 4, 0x33), "d,s,t", RVX_ZBB, 0 }, { "minu", F(5, 5, 0x33), "d,s,t", RVX_ZBB, 0 },
    { "sext.b", 0x60401013, "d,s", RVX_ZBB, 0 }, { "sext.h", 0x60501013, "d,s", RVX_ZBB, 0 },
    { "rol", F(0x30, 1, 0x33), "d,s,t", RVX_ZBB, 0 }, { "ror", F(0x30, 5, 0x33), "d,s,t", RVX_ZBB, 0 },
    { "rolw", F(0x30, 1, 0x3b), "d,s,t", RVX_ZBB, 2 }, { "rorw", F(0x30, 5, 0x3b), "d,s,t", RVX_ZBB, 2 },
    { "rori", 0x60005013, "d,s,h", RVX_ZBB, 0 }, { "roriw", 0x6000501b, "d,s,w", RVX_ZBB, 2 },
    { "orc.b", 0x28705013, "d,s", RVX_ZBB, 0 },
    { "sh1add", F(0x10, 2, 0x33), "d,s,t", RVX_ZBA, 0 }, { "sh2add", F(0x10, 4, 0x33), "d,s,t", RVX_ZBA, 0 }, { "sh3add", F(0x10, 6, 0x33), "d,s,t", RVX_ZBA, 0 },
    { "add.uw", F(4, 0, 0x3b), "d,s,t", RVX_ZBA, 2 }, { "sh1add.uw", F(0x10, 2, 0x3b), "d,s,t", RVX_ZBA, 2 }, { "sh2add.uw", F(0x10, 4, 0x3b), "d,s,t", RVX_ZBA, 2 },
    { "sh3add.uw", F(0x10, 6, 0x3b), "d,s,t", RVX_ZBA, 2 }, { "slli.uw", 0x0800101b, "d,s,h", RVX_ZBA, 2 },
    { "bclr", F(0x24, 1, 0x33), "d,s,t", RVX_ZBS, 0 }, { "bext", F(0x24, 5, 0x33), "d,s,t", RVX_ZBS, 0 }, { "binv", F(0x34, 1, 0x33), "d,s,t", RVX_ZBS, 0 }, { "bset", F(0x14, 1, 0x33), "d,s,t", RVX_ZBS, 0 },
    { "bclri", 0x48001013, "d,s,h", RVX_ZBS, 0 }, { "bexti", 0x48005013, "d,s,h", RVX_ZBS, 0 }, { "binvi", 0x68001013, "d,s,h", RVX_ZBS, 0 }, { "bseti", 0x28001013, "d,s,h", RVX_ZBS, 0 },
    { "clmul", F(5, 1, 0x33), "d,s,t", RVX_ZBC, 0 }, { "clmulr", F(5, 2, 0x33), "d,s,t", RVX_ZBC, 0 }, { "clmulh", F(5, 3, 0x33), "d,s,t", RVX_ZBC, 0 },
    { "czero.eqz", F(7, 5, 0x33), "d,s,t", RVX_ZICOND, 0 }, { "czero.nez", F(7, 7, 0x33), "d,s,t", RVX_ZICOND, 0 },
    { "flh", 0x1007, "D,l", RVX_ZFHMIN, 0 }, { "fsh", 0x1027, "T,m", RVX_ZFHMIN, 0 },
    { "fmadd.h", 0x4000043, "D,S,T,U,r", RVX_ZFH, 0 }, { "fmsub.h", 0x4000047, "D,S,T,U,r", RVX_ZFH, 0 },
    { "fnmsub.h", 0x400004b, "D,S,T,U,r", RVX_ZFH, 0 }, { "fnmadd.h", 0x400004f, "D,S,T,U,r", RVX_ZFH, 0 },
    FPH2("fadd", 0x00, 0, "D,S,T,r"), FPH2("fsub", 0x01, 0, "D,S,T,r"), FPH2("fmul", 0x02, 0, "D,S,T,r"), FPH2("fdiv", 0x03, 0, "D,S,T,r"),
    FPH2("fsqrt", 0x0b, 0, "D,S,r"),
    FPH2("fsgnj", 0x04, 0, "D,S,T"), FPH2("fsgnjn", 0x04, 1, "D,S,T"), FPH2("fsgnjx", 0x04, 2, "D,S,T"),
    FPH2("fmin", 0x05, 0, "D,S,T"), FPH2("fmax", 0x05, 1, "D,S,T"),
    FPH2("feq", 0x14, 2, "d,S,T"), FPH2("flt", 0x14, 1, "d,S,T"), FPH2("fle", 0x14, 0, "d,S,T"),
    FPH2("fclass", 0x1c, 1, "d,S"),
    { "fcvt.s.h", 0x40200053, "D,S", RVX_ZFHMIN, 0 }, { "fcvt.h.s", 0x44000053, "D,S,r", RVX_ZFHMIN, 0 },
    { "fcvt.d.h", 0x42200053, "D,S", RVX_ZFHMIN | RVX_D, 0 }, { "fcvt.h.d", 0x44100053, "D,S,r", RVX_ZFHMIN | RVX_D, 0 },
    { "fcvt.w.h", 0xc4000053, "d,S,r", RVX_ZFH, 0 }, { "fcvt.wu.h", 0xc4100053, "d,S,r", RVX_ZFH, 0 },
    { "fcvt.l.h", 0xc4200053, "d,S,r", RVX_ZFH, 2 }, { "fcvt.lu.h", 0xc4300053, "d,S,r", RVX_ZFH, 2 },
    { "fcvt.h.w", 0xd4000053, "D,s,r", RVX_ZFH, 0 }, { "fcvt.h.wu", 0xd4100053, "D,s,r", RVX_ZFH, 0 },
    { "fcvt.h.l", 0xd4200053, "D,s,r", RVX_ZFH, 2 }, { "fcvt.h.lu", 0xd4300053, "D,s,r", RVX_ZFH, 2 },
    { "fmv.x.h", 0xe4000053, "d,S", RVX_ZFHMIN, 0 }, { "fmv.h.x", 0xf4000053, "D,s", RVX_ZFHMIN, 0 },
    { "pack", F(4, 4, 0x33), "d,s,t", RVX_ZBKB, 0 }, { "packh", F(4, 7, 0x33), "d,s,t", RVX_ZBKB, 0 }, { "packw", F(4, 4, 0x3b), "d,s,t", RVX_ZBKB, 2 },
    { "brev8", 0x68705013, "d,s", RVX_ZBKB, 0 }, { "zip", 0x08f01013, "d,s", RVX_ZBKB, 1 }, { "unzip", 0x08f05013, "d,s", RVX_ZBKB, 1 },
    { "xperm4", F(0x14, 2, 0x33), "d,s,t", RVX_ZBKX, 0 }, { "xperm8", F(0x14, 4, 0x33), "d,s,t", RVX_ZBKX, 0 },
    { "aes64ds", F(0x1d, 0, 0x33), "d,s,t", RVX_ZKND, 2 }, { "aes64dsm", F(0x1f, 0, 0x33), "d,s,t", RVX_ZKND, 2 }, { "aes64im", 0x30001013, "d,s", RVX_ZKND, 2 },
    { "aes64es", F(0x19, 0, 0x33), "d,s,t", RVX_ZKNE, 2 }, { "aes64esm", F(0x1b, 0, 0x33), "d,s,t", RVX_ZKNE, 2 },
    { "aes64ks1i", 0x31001013, "d,s,k", RVX_ZKND | RVX_ZKNE, 2 }, { "aes64ks2", F(0x3f, 0, 0x33), "d,s,t", RVX_ZKND | RVX_ZKNE, 2 },
    { "sha256sig0", 0x10201013, "d,s", RVX_ZKNH, 0 }, { "sha256sig1", 0x10301013, "d,s", RVX_ZKNH, 0 },
    { "sha256sum0", 0x10001013, "d,s", RVX_ZKNH, 0 }, { "sha256sum1", 0x10101013, "d,s", RVX_ZKNH, 0 },
    { "sha512sig0", 0x10601013, "d,s", RVX_ZKNH, 2 }, { "sha512sig1", 0x10701013, "d,s", RVX_ZKNH, 2 },
    { "sha512sum0", 0x10401013, "d,s", RVX_ZKNH, 2 }, { "sha512sum1", 0x10501013, "d,s", RVX_ZKNH, 2 },
    { "sm3p0", 0x10801013, "d,s", RVX_ZKSH, 0 }, { "sm3p1", 0x10901013, "d,s", RVX_ZKSH, 0 },
    { "sm4ed", 0x30000033, "d,s,t,b", RVX_ZKSED, 0 }, { "sm4ks", 0x34000033, "d,s,t,b", RVX_ZKSED, 0 },
    { "cbo.clean", 0x0010200f, "a", RVX_ZICBOM, 0 }, { "cbo.flush", 0x0020200f, "a", RVX_ZICBOM, 0 }, { "cbo.inval", 0x0000200f, "a", RVX_ZICBOM, 0 },
    { "cbo.zero", 0x0040200f, "a", RVX_ZICBOZ, 0 },
    { "prefetch.i", 0x00006013, "P", RVX_ZICBOP, 0 }, { "prefetch.r", 0x00106013, "P", RVX_ZICBOP, 0 }, { "prefetch.w", 0x00306013, "P", RVX_ZICBOP, 0 },
    { "sinval.vma", 0x16000073, "s,t", RVX_SVINVAL, 0 }, { "sfence.w.inval", 0x18000073, "", RVX_SVINVAL, 0 }, { "sfence.inval.ir", 0x18100073, "", RVX_SVINVAL, 0 },
    { "hinval.vvma", 0x26000073, "s,t", RVX_SVINVAL | RVX_H, 0 }, { "hinval.gvma", 0x66000073, "s,t", RVX_SVINVAL | RVX_H, 0 },
    { "hfence.vvma", 0x22000073, "s,t", RVX_H, 0 }, { "hfence.gvma", 0x62000073, "s,t", RVX_H, 0 },
    { "hlv.b", 0x60004073, "d,a", RVX_H, 0 }, { "hlv.bu", 0x60104073, "d,a", RVX_H, 0 }, { "hlv.h", 0x64004073, "d,a", RVX_H, 0 }, { "hlv.hu", 0x64104073, "d,a", RVX_H, 0 },
    { "hlvx.hu", 0x64304073, "d,a", RVX_H, 0 }, { "hlv.w", 0x68004073, "d,a", RVX_H, 0 }, { "hlvx.wu", 0x68304073, "d,a", RVX_H, 0 },
    { "hlv.wu", 0x68104073, "d,a", RVX_H, 2 }, { "hlv.d", 0x6c004073, "d,a", RVX_H, 2 },
    { "hsv.b", 0x62004073, "t,a", RVX_H, 0 }, { "hsv.h", 0x66004073, "t,a", RVX_H, 0 }, { "hsv.w", 0x6a004073, "t,a", RVX_H, 0 }, { "hsv.d", 0x6e004073, "t,a", RVX_H, 2 },
    { "wrs.nto", 0x00d00073, "", RVX_ZAWRS, 0 }, { "wrs.sto", 0x01d00073, "", RVX_ZAWRS, 0 },
    { "ntl.p1", 0x00200033, "", RVX_I, 0 }, { "ntl.pall", 0x00300033, "", RVX_I, 0 }, { "ntl.s1", 0x00400033, "", RVX_I, 0 }, { "ntl.all", 0x00500033, "", RVX_I, 0 },
    { NULL, 0, NULL, 0, 0 }
};

/* Looks up an instruction in the pattern table, handling .aq and .rl suffixes. */
static const RvEnt *rv_find(const char *mn, int *aq, int *rl)
{
    for (int pass = 0; pass < 2; pass++) {
        char buf[48];
        snprintf(buf, sizeof buf, "%s", mn);
        size_t n = strlen(buf);
        int a = 0, r = 0;
        if (pass == 1) {
            if (n > 5 && !strcmp(buf + n - 5, ".aqrl")) { buf[n - 5] = 0; a = r = 1; }
            else if (n > 3 && !strcmp(buf + n - 3, ".aq")) { buf[n - 3] = 0; a = 1; }
            else if (n > 3 && !strcmp(buf + n - 3, ".rl")) { buf[n - 3] = 0; r = 1; }
            else return NULL;
        }
        for (const RvEnt *e = rv_tab; e->name; e++) {
            if (!strcmp(e->name, buf)) {
                if (pass == 1 && e->ext != RVX_A) return NULL;
                *aq = a;
                *rl = r;
                return e;
            }
        }
    }
    return NULL;
}

static const char *const pseudos[] = {
    "nop", "li", "la", "lla", "mv", "not", "neg", "negw", "sext.w", "zext.b", "zext.w", "zext.h", "seqz", "snez", "sltz", "sgtz",
    "beq", "bne", "blt", "bge", "bltu", "bgeu", "beqz", "bnez", "blez", "bgez", "bltz", "bgtz", "bgt", "ble", "bgtu", "bleu",
    "j", "jal", "jr", "jalr", "ret", "call", "tail", "fence", "csrr", "csrw", "csrs", "csrc", "csrwi", "csrsi", "csrci",
    "rdcycle", "rdtime", "rdinstret", "rdcycleh", "rdtimeh", "rdinstreth", "frcsr", "fscsr", "frrm", "fsrm", "frflags", "fsflags",
    "fmv.s", "fabs.s", "fneg.s", "fmv.d", "fabs.d", "fneg.d", "fmv.h", "fabs.h", "fneg.h", "sfence.vma", "rev8", "fgt.s", "fge.s", "fgt.d", "fge.d", "unimp", "lga", "fence.i",
    NULL
};

static int calias_known(const char *n);

/* True if the mnemonic is a RISC-V instruction or pseudo instruction. */
int rv_known(const char *mn, int rvext)
{
    (void)rvext;
    char buf[48];
    size_t i = 0;
    for (; mn[i] && i < sizeof buf - 1; i++) buf[i] = (char)tolower((unsigned char)mn[i]);
    buf[i] = 0;
    for (int k = 0; pseudos[k]; k++) if (!strcmp(pseudos[k], buf)) return 1;
    if (calias_known(buf)) return 1;
    int aq, rl;
    return rv_find(buf, &aq, &rl) != NULL;
}

/* Parses offset(base) operands with optional %lo relocation. */
static int parse_mem(RC *c, const char *s, int *base, Val *off, int *rk, int *plain)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", s);
    trim(buf);
    *rk = RM_NONE;
    *plain = 0;
    val_const(off, 0);
    size_t n = strlen(buf);
    {
        int rr0 = rv_xreg(buf);
        if (rr0 >= 0) { *base = rr0; *plain = 2; return 1; }
    }
    if (n && buf[0] == '[' && buf[n - 1] == ']') {
        buf[n - 1] = 0;
        char *in = buf + 1;
        trim(in);
        char *p = in;
        while (*p && *p != '+' && *p != '-' && *p != ',') p++;
        char save = *p;
        *p = 0;
        char r[260];
        snprintf(r, sizeof r, "%s", in);
        trim(r);
        int b = rv_xreg(r);
        if (b < 0) { err("%s: bad base register `%s'", c->mn, r); return 0; }
        *base = b;
        *p = save;
        if (!save) return 1;
        char *e = p;
        if (*e == ',') e++;
        trim(e);
        if (!*e) return 1;
        return ev(c, e, off);
    }
    if (n && buf[n - 1] == ')') {
        int d = 0;
        size_t i = n;
        while (i > 0) {
            i--;
            if (buf[i] == ')') d++;
            else if (buf[i] == '(') { d--; if (d == 0) break; }
        }
        if (d != 0) { err("%s: unbalanced parentheses in `%s'", c->mn, s); return 0; }
        char r[32];
        size_t rl = n - 1 - i - 1;
        if (rl >= sizeof r) { err("%s: bad memory operand `%s'", c->mn, s); return 0; }
        memcpy(r, buf + i + 1, rl);
        r[rl] = 0;
        trim(r);
        int b = rv_xreg(r);
        if (b >= 0) {
            *base = b;
            buf[i] = 0;
            trim(buf);
            if (!buf[0]) return 1;
            char inner[200];
            int k;
            int rr = reloc_op(buf, &k, inner, sizeof inner);
            if (rr < 0) { err("%s: junk after relocation operator in `%s'", c->mn, s); return 0; }
            if (rr == 1) {
                if (k == RM_LO || k == RM_PCLO) { *rk = k; return ev(c, inner, off); }
                err("%s: only %%lo or %%pcrel_lo may be used in a memory offset", c->mn);
                return 0;
            }
            return ev(c, buf, off);
        }
    }
    *plain = 1;
    return ev(c, buf, off);
}

/* Reads a 20-bit upper immediate or %hi/%pcrel_hi operator. */
static int imm20_of(RC *c, const char *s, i64 *v, int *rk, Val *sym)
{
    char inner[200];
    int k;
    int rr = reloc_op(s, &k, inner, sizeof inner);
    *rk = RM_NONE;
    if (rr < 0) { err("%s: junk after relocation operator", c->mn); return 0; }
    if (rr == 1) {
        if (!ev(c, inner, sym)) return 0;
        if (k == RM_GOTHI) {
            if (!sym->ext && sym->pos < 0) { err("%s: %%got_pcrel_hi needs a symbol", c->mn); return 0; }
            sym->wk = 7;
            k = RM_PCHI;
        }
        if (k == RM_HI || k == RM_PCHI) {
            *rk = k;
            if (k == RM_HI && val_isconst(sym)) { *v = ((sym->n + 0x800) >> 12) & 0xfffff; *rk = RM_NONE; }
            else *v = 0;
            return 1;
        }
        err("%s: %%lo cannot be used here", c->mn);
        return 0;
    }
    Val x;
    if (!ev(c, s, &x)) return 0;
    if (x.unk) { *v = 0; return 1; }
    if (!val_isconst(&x)) { err("%s: constant expression expected here (use %%hi(symbol) for addresses)", c->mn); return 0; }
    *v = x.n;
    return 1;
}

/* Reads a signed 12-bit immediate or %lo operator. */
static int simm12_of(RC *c, const char *s, i64 *v, int *rk, Val *sym, int store)
{
    (void)store;
    char inner[200];
    int k;
    int rr = reloc_op(s, &k, inner, sizeof inner);
    *rk = RM_NONE;
    if (rr < 0) { err("%s: junk after relocation operator", c->mn); return 0; }
    if (rr == 1) {
        if (!ev(c, inner, sym)) return 0;
        if (k == RM_LO) {
            if (val_isconst(sym)) { *v = (i64)(((sym->n & 0xfff) ^ 0x800) - 0x800); return 1; }
            *rk = RM_LO;
            *v = 0;
            return 1;
        }
        if (k == RM_PCLO) { *rk = RM_PCLO; *v = 0; return 1; }
        err("%s: %%hi cannot be used here", c->mn);
        return 0;
    }
    Val x;
    if (!ev(c, s, &x)) return 0;
    if (x.unk) { *v = 0; return 1; }
    if (!val_isconst(&x)) {
        *sym = x;
        *rk = RM_LO;
        *v = 0;
        return 1;
    }
    *v = x.n;
    return 1;
}

/* Adds the low-part relocation for an I or S type instruction. */
static void put_lo_reloc(RC *c, int rk, const Val *sym, int store, i64 at)
{
    if (rk == RM_LO) rv_reloc(c, store ? RK_RV_LO12S : RK_RV_LO12I, sym, at);
    else if (rk == RM_PCLO) rv_reloc(c, store ? RK_RV_PCLO12S : RK_RV_PCLO12I, sym, at);
}

/* Encodes one table pattern: walks the pattern letters and parses the matching operands. */
static int do_pattern(RC *c, const RvEnt *e)
{
    const char *pat = e->pat;
    u32 w = e->base;
    int ti = 0;
    int nreq = 0, nopt = 0;
    for (const char *p = pat; *p; p++) if (*p != ',') { if (*p == 'r') nopt++; else nreq++; }
    if (c->n < nreq || c->n > nreq + nopt) {
        char form[64] = "";
        for (const char *p = pat; *p; p++) {
            const char *nm = NULL;
            switch (*p) {
            case 'd': nm = "rd"; break; case 's': nm = "rs1"; break; case 't': nm = "rs2"; break;
            case 'D': nm = "frd"; break; case 'S': nm = "frs1"; break; case 'T': nm = "frs2"; break; case 'U': nm = "frs3"; break;
            case 'i': nm = "imm"; break; case 'h': case 'w': nm = "shamt"; break; case 'u': nm = "imm20"; break;
            case 'l': nm = "imm(rs1)"; break; case 'm': nm = "imm(rs1)"; break; case 'a': nm = "(rs1)"; break;
            case 'c': nm = "csr"; break; case 'z': nm = "uimm5"; break; case 'r': nm = "[rm]"; break;
            }
            if (nm) { if (form[0]) strcat(form, ", "); strcat(form, nm); }
        }
        return usage(c, form);
    }
    int rm = 7;
    int hasrm = 0;
    int rk = RM_NONE;
    Val sym;
    memset(&sym, 0, sizeof sym);
    i64 pcrel_at = -1;
    int memrk = RM_NONE;
    int lastmem = 0;
    Val mval;
    memset(&mval, 0, sizeof mval);
    int storeform = 0;
    for (const char *p = pat; *p; p++) {
        if (*p == ',') continue;
        if (*p == 'r') {
            if (ti < c->n) {
                int v = rm_of(c->tok[ti]);
                if (v < 0) { err("%s: bad rounding mode `%s' (rne, rtz, rdn, rup, rmm or dyn)", c->mn, c->tok[ti]); return 0; }
                rm = v;
                hasrm = 1;
                ti++;
            }
            continue;
        }
        const char *t = c->tok[ti];
        int r;
        switch (*p) {
        case 'd': if (!get_x(c, ti, &r)) return 0; w |= (u32)r << 7; break;
        case 's': if (!get_x(c, ti, &r)) return 0; w |= (u32)r << 15; break;
        case 't': if (!get_x(c, ti, &r)) return 0; w |= (u32)r << 20; break;
        case 'D': if (!get_f(c, ti, &r)) return 0; w |= (u32)r << 7; break;
        case 'S': if (!get_f(c, ti, &r)) return 0; w |= (u32)r << 15; break;
        case 'T': if (!get_f(c, ti, &r)) return 0; w |= (u32)r << 20; break;
        case 'U': if (!get_f(c, ti, &r)) return 0; w |= (u32)r << 27; break;
        case 'i': {
            i64 v;
            if (!simm12_of(c, t, &v, &rk, &sym, 0)) return 0;
            if (rk == RM_NONE && !fits_s(v, 12)) { err("%s: immediate %lld does not fit in 12 bits (-2048 to 2047)", c->mn, (long long)v); return 0; }
            w |= ((u32)v & 0xfff) << 20;
            break;
        }
        case 'h': case 'w': {
            i64 v;
            int unk;
            if (!const_of(c, t, &v, &unk)) return 0;
            int lim = (*p == 'h' && c->rv64) ? 63 : 31;
            if (!unk && (v < 0 || v > lim)) { err("%s: shift amount %lld is out of range (0 to %d)", c->mn, (long long)v, lim); return 0; }
            w |= ((u32)v & 63) << 20;
            break;
        }
        case 'u': {
            i64 v;
            if (!imm20_of(c, t, &v, &rk, &sym)) return 0;
            if (rk == RM_NONE && (v < -524288 || v > 1048575)) { err("%s: immediate %lld does not fit in 20 bits (0 to 1048575)", c->mn, (long long)v); return 0; }
            w |= ((u32)v & 0xfffff) << 12;
            break;
        }
        case 'l': case 'm': {
            int base, plain;
            if (!parse_mem(c, t, &base, &mval, &memrk, &plain)) return 0;
            if (plain == 1) {
                err("%s: expected `offset(register)', found `%s'", c->mn, t);
                return 0;
            }
            i64 v = 0;
            if (is_mutable(&mval)) {
                if (mval.unk) v = 0;
                else if (memrk == RM_NONE) { memrk = RM_LO; }
                if (memrk == RM_NONE && !mval.unk) v = mval.n;
            } else v = mval.n;
            if (memrk == RM_NONE && !mval.unk && !fits_s(v, 12)) { err("%s: offset %lld does not fit in 12 bits (-2048 to 2047)", c->mn, (long long)v); return 0; }
            w |= (u32)base << 15;
            lastmem = 1;
            storeform = *p == 'm';
            if (*p == 'l') w |= ((u32)v & 0xfff) << 20;
            else w |= (((u32)v & 0x1f) << 7) | ((((u32)v >> 5) & 0x7f) << 25);
            break;
        }
        case 'a': {
            int base, plain, k2;
            Val ov;
            if (!parse_mem(c, t, &base, &ov, &k2, &plain)) return 0;
            if (plain == 1) { err("%s: expected `(register)', found `%s'", c->mn, t); return 0; }
            else if (plain == 2) { }
            else if (!val_isconst(&ov) || ov.n != 0) { err("%s: atomic operations take (register) with no offset", c->mn); return 0; }
            w |= (u32)base << 15;
            break;
        }
        case 'b': {
            i64 v;
            int unk;
            if (!const_of(c, t, &v, &unk)) return 0;
            if (!unk && (v < 0 || v > 3)) { err("%s: the byte select must be 0 to 3", c->mn); return 0; }
            w |= ((u32)v & 3) << 30;
            break;
        }
        case 'k': {
            i64 v;
            int unk;
            if (!const_of(c, t, &v, &unk)) return 0;
            if (!unk && (v < 0 || v > 10)) { err("%s: the round number must be 0 to 10", c->mn); return 0; }
            w |= ((u32)v & 15) << 20;
            break;
        }
        case 'P': {
            int base, plain;
            Val ov;
            int k2;
            if (!parse_mem(c, t, &base, &ov, &k2, &plain)) return 0;
            if (plain == 1) { err("%s: expected `offset(register)', found `%s'", c->mn, t); return 0; }
            i64 v = plain == 2 ? 0 : ov.n;
            if (plain != 2 && !val_isconst(&ov)) { err("%s: the offset must be a constant", c->mn); return 0; }
            if (v < -2048 || v > 2016 || (v & 31)) { err("%s: the offset must be a multiple of 32 from -2048 to 2016", c->mn); return 0; }
            w |= (u32)base << 15 | ((((u32)v >> 5) & 0x7f) << 25);
            break;
        }
        case 'c': {
            int num;
            if (!csr_num(c, t, &num)) return 0;
            w |= (u32)num << 20;
            break;
        }
        case 'z': {
            i64 v;
            int unk;
            if (!const_of(c, t, &v, &unk)) return 0;
            if (!unk && (v < 0 || v > 31)) { err("%s: immediate %lld is out of range (0 to 31)", c->mn, (long long)v); return 0; }
            w |= ((u32)v & 31) << 15;
            break;
        }
        }
        ti++;
    }
    if (strchr(pat, 'r')) {
        if (!hasrm) rm = 7;
        w |= (u32)rm << 12;
    }
    if (c->aq) w |= 1u << 26;
    if (c->rl) w |= 1u << 25;
    int opc = (int)(e->base & 0x7f);
    if (opc == 0x37 || opc == 0x17) {
        if (rk == RM_HI) {
            i64 at = (i64)c->out->code.n;
            rv_reloc(c, RK_RV_HI20, &sym, at);
        } else if (rk == RM_PCHI) {
            rv_reloc(c, RK_RV_PCHI20, &sym, (i64)c->out->code.n);
        }
        (void)pcrel_at;
        emit(c, w);
        return 1;
    }
    if (lastmem && memrk != RM_NONE) {
        put_lo_reloc(c, memrk, &mval, storeform, (i64)c->out->code.n);
        emit4(c, w);
        return 1;
    }
    if (rk != RM_NONE) {
        put_lo_reloc(c, rk, &sym, 0, (i64)c->out->code.n);
        emit4(c, w);
        return 1;
    }
    emit(c, w);
    return 1;
}

typedef struct { int op; i64 imm; } Step;
enum { S_LUI, S_ADDI, S_ADDIW, S_SLLI, S_SLLIUW, S_BSETI, S_BCLRI, S_ADD0, S_SRLI };

static int li_gen(i64 val, int rv64, int ext, Step *st);

/* Recursive helper that splits a constant into lui/addi/slli steps. */
static int li_rec(i64 val, int rv64, int ext, Step *st)
{
    if (val >= -2147483648LL && val <= 2147483647LL) {
        i64 hi20 = ((val + 0x800) >> 12) & 0xfffff;
        i64 lo12 = ((val & 0xfff) ^ 0x800) - 0x800;
        int n = 0;
        if (hi20) { st[n].op = S_LUI; st[n].imm = hi20; n++; }
        if (lo12 || !hi20) { st[n].op = (rv64 && hi20) ? S_ADDIW : S_ADDI; st[n].imm = lo12; n++; }
        return n;
    }
    i64 lo12 = ((val & 0xfff) ^ 0x800) - 0x800;
    u64 hi52 = ((u64)val + 0x800ull) >> 12;
    int shift = 12 + __builtin_ctzll(hi52);
    i64 h = (i64)(hi52 >> (shift - 12));
    int bits = 64 - shift;
    if (bits < 64) h = (h << (64 - bits)) >> (64 - bits);
    if (shift > 12 && !(h >= -2048 && h <= 2047)) {
        i64 sh12 = (i64)((u64)h << 12);
        if (sh12 >= -2147483648LL && sh12 <= 2147483647LL) { shift -= 12; h = sh12; }
    }
    int n = li_rec(h, rv64, ext, st);
    st[n].op = S_SLLI;
    st[n].imm = shift;
    n++;
    if (lo12) { st[n].op = S_ADDI; st[n].imm = lo12; n++; }
    return n;
}

/* Plans the shortest instruction steps to load a constant. */
static int li_gen(i64 val, int rv64, int ext, Step *st)
{
    int n = li_rec(val, rv64, ext, st);
    if (n > 1 && (ext & RVX_ZBS) && val > 0 && (rv64 || val < 0x80000000LL) && (val & (val - 1)) == 0) {
        st[0].op = S_BSETI;
        st[0].imm = __builtin_ctzll((u64)val);
        return 1;
    }
    if (rv64 && n > 2 && (ext & RVX_ZBS) && val < 0 && ((~val) & ((~val) - 1)) == 0) {
        st[0].op = S_ADDI; st[0].imm = -1;
        st[1].op = S_BCLRI; st[1].imm = __builtin_ctzll((u64)~val);
        return 2;
    }
    if (rv64 && n > 2 && val > 0) {
        int lz = __builtin_clzll((u64)val);
        u64 sv = (u64)val << lz;
        u64 vs[2] = { sv | ((1ull << lz) - 1), sv };
        for (int k = 0; k < 2; k++) {
            Step t[10];
            int m = li_rec((i64)vs[k], rv64, ext, t);
            if (m < n) {
                memcpy(st, t, sizeof(Step) * (size_t)m);
                st[m].op = S_SRLI; st[m].imm = lz;
                n = m + 1;
            }
        }
    }
    if (rv64 && n > 2 && (ext & RVX_ZBA) && val > 0 && val <= 0xffffffffLL) {
        Step t[8];
        i64 v2 = (i64)((u64)val | (0xffffffffull << 32));
        int m = li_rec(v2, rv64, ext, t);
        if (m + 1 < n) {
            memcpy(st, t, sizeof(Step) * (size_t)m);
            st[m].op = S_SLLIUW; st[m].imm = 0;
            return m + 1;
        }
    }
    return n;
}

/* Emits the steps for loading a constant. */
static void emit_li(RC *c, int rd, i64 val)
{
    if (!c->rv64) val = (i64)(int32_t)val;
    Step st[10];
    int n = li_gen(val, c->rv64, c->ext, st);
    int src = 0;
    for (int i = 0; i < n; i++) {
        u32 w = 0;
        int first = i == 0;
        int rs = first ? 0 : rd;
        switch (st[i].op) {
        case S_LUI: w = 0x37u | ((u32)rd << 7) | ((u32)st[i].imm << 12); src = 1; break;
        case S_ADDI: w = enc_i(0x13, rd, src || !first ? rd : 0, st[i].imm); break;
        case S_ADDIW: w = enc_i(0x1b, rd, rd, st[i].imm); break;
        case S_SLLI: w = enc_i(0x1013, rd, rd, st[i].imm); break;
        case S_SLLIUW: w = 0x0800003bu | ((u32)rd << 7) | ((u32)rd << 15); break;
        case S_SRLI: w = 0x5013u | ((u32)rd << 7) | ((u32)rd << 15) | ((u32)st[i].imm << 20); break;
        case S_BSETI: w = 0x28001013u | ((u32)rd << 7) | ((u32)st[i].imm << 20); break;
        case S_BCLRI: w = 0x48001013u | ((u32)rd << 7) | ((u32)rd << 15) | ((u32)st[i].imm << 20); break;
        }
        (void)rs;
        emit_seq(c, w);
    }
}

/* li pseudo instruction. */
static int h_li(RC *c)
{
    if (c->n != 2) return usage(c, "rd, immediate");
    int rd;
    if (!get_x(c, 0, &rd)) return 0;
    Val v;
    if (!ev(c, c->tok[1], &v)) return 0;
    if (!v.unk && !val_isconst(&v)) {
        err("%s: constant expected (use `la' to load an address)", c->mn);
        return 0;
    }
    i64 val = v.unk ? 0 : v.n;
    if (!c->rv64 && !v.unk && (val < -2147483648LL || val > 4294967295LL)) { err("li: immediate %lld does not fit in 32 bits", (long long)val); return 0; }
    emit_li(c, rd, val);
    return 1;
}

/* Emits an auipc plus follow-up instruction pair with matching relocations. */
static void pcrel_pair(RC *c, int rd_hi, int rd_lo, int base_lo, u32 lo_op_base, const Val *target, int store, int kind_only)
{
    (void)kind_only;
    i64 at = rv_cur(c);
    Val loc;
    here_val(c, &loc, at);
    rv_reloc(c, RK_RV_PCHI20, target, (i64)c->out->code.n);
    w32(c, 0x17u | ((u32)rd_hi << 7));
    rv_reloc(c, store ? RK_RV_PCLO12S : RK_RV_PCLO12I, &loc, (i64)c->out->code.n);
    if (store) w32(c, lo_op_base | ((u32)base_lo << 15) | ((u32)rd_lo << 20));
    else w32(c, lo_op_base | ((u32)rd_lo << 7) | ((u32)base_lo << 15));
}

/* la and lla pseudo instructions. */
static int h_la(RC *c)
{
    if (c->n != 2) return usage(c, "rd, symbol");
    int rd;
    if (!get_x(c, 0, &rd)) return 0;
    Val v;
    if (!ev(c, c->tok[1], &v)) return 0;
    if (c->opt && !v.unk && val_isconst(&v) && v.n >= -2147483648LL && v.n <= 2147483647LL) {
        emit_li(c, rd, v.n);
        return 1;
    }
    pcrel_pair(c, rd, rd, rd, 0x13u, &v, 0, 0);
    return 1;
}

/* Load or store with a symbol address (lw rd, sym). */
static int h_ldst_sym(RC *c, const RvEnt *e, int isstore)
{
    int rt = -1;
    int fp = (e->pat[0] == 'D' || e->pat[0] == 'T');
    int r;
    if (isstore) { if (fp ? !get_f(c, 0, &r) : !get_x(c, 0, &r)) return 0; }
    else { if (fp ? !get_f(c, 0, &r) : !get_x(c, 0, &r)) return 0; }
    Val v;
    if (!ev(c, c->tok[1], &v)) return 0;
    if (c->n == 3) { if (!get_x(c, 2, &rt)) return 0; }
    else if (isstore || fp) rt = 31;
    else rt = r;
    u32 b = e->base & ~0x7fu;
    u32 opc = e->base & 0x7f;
    pcrel_pair(c, rt, r, rt, b | opc, &v, isstore, 0);
    return 1;
}

/* Maps beq, bne, blt, bge, bltu, bgeu and the swapped pseudo forms to fields. */
static int branch_names(const char *mn, int *f3, int *swap)
{
    static const struct { const char *n; int f3, swap; } t[] = {
        { "beq", 0, 0 }, { "bne", 1, 0 }, { "blt", 4, 0 }, { "bge", 5, 0 }, { "bltu", 6, 0 }, { "bgeu", 7, 0 },
        { "bgt", 4, 1 }, { "ble", 5, 1 }, { "bgtu", 6, 1 }, { "bleu", 7, 1 }, { NULL, 0, 0 }
    };
    for (int i = 0; t[i].n; i++) if (!strcmp(mn, t[i].n)) { *f3 = t[i].f3; *swap = t[i].swap; return 1; }
    return 0;
}

/* Emits a conditional branch; uses an inverted branch plus jump when the target is out of range. */
static int emit_branch(RC *c, int f3, int rs1, int rs2, const Val *v)
{
    i64 pc = rv_cur(c);
    int known = same_sec(c, v);
    int unk = v->unk;
    i64 off = known ? v->n - pc : 0;
    int cand = c->cmp && rs2 == 0 && (f3 == 0 || f3 == 1) && cr3(rs1);
    if (known || unk) {
        if (known && (off & 1)) { err("%s: branch target is not 2-byte aligned", c->mn); return 0; }
        if (cand && c->floor < 4 && (unk || (off >= -256 && off <= 254))) {
            w16(c, (f3 == 0 ? 0xc001u : 0xe001u) | cb_bits(off, rs1));
            return 1;
        }
        if (c->floor < 8 && (unk || (off >= -4096 && off <= 4094))) {
            w32(c, enc_b(0x63u | ((u32)f3 << 12), rs1, rs2, off));
            if (cand) c->mut = 1;
            return 1;
        }
        int inv = f3 ^ 1;
        w32(c, enc_b(0x63u | ((u32)inv << 12), rs1, rs2, 8));
        i64 joff = off - 4;
        if (!fits_s(joff, 21)) { err("%s: branch target is out of range (more than 1MB away)", c->mn); return 0; }
        w32(c, enc_j(0x6f, 0, joff));
        return 1;
    }
    rv_reloc(c, RK_RV_BRANCH, v, (i64)c->out->code.n);
    w32(c, enc_b(0x63u | ((u32)f3 << 12), rs1, rs2, 0));
    return 1;
}

/* Emits jal with range check or relocation. */
static int emit_jal(RC *c, int rd, const Val *v)
{
    i64 pc = rv_cur(c);
    int known = same_sec(c, v);
    int unk = v->unk;
    i64 off = known ? v->n - pc : 0;
    int cand = c->cmp && (rd == 0 || (rd == 1 && !c->rv64));
    if (known || unk) {
        if (known && (off & 1)) { err("%s: jump target is not 2-byte aligned", c->mn); return 0; }
        if (cand && c->floor < 4 && (unk || (off >= -2048 && off <= 2046))) {
            w16(c, (rd == 0 ? 0xa001u : 0x2001u) | cj_bits(off));
            return 1;
        }
        if (c->floor < 8 && (unk || fits_s(off, 21))) {
            w32(c, enc_j(0x6f, rd, off));
            if (cand) c->mut = 1;
            return 1;
        }
        int t = rd ? rd : 6;
        i64 hi = (off + 0x800) >> 12;
        i64 lo = ((off & 0xfff) ^ 0x800) - 0x800;
        if (!fits_s(hi, 20)) { err("%s: jump target is out of range (more than 2GB away)", c->mn); return 0; }
        w32(c, 0x17u | ((u32)t << 7) | (((u32)hi & 0xfffff) << 12));
        w32(c, enc_i(0x67, rd, t, lo));
        return 1;
    }
    rv_reloc(c, RK_RV_JAL, v, (i64)c->out->code.n);
    w32(c, enc_j(0x6f, rd, 0));
    return 1;
}

/* Emits call/tail as auipc+jalr, or a single jal when the target is near. */
static int emit_call(RC *c, int link, const Val *v)
{
    i64 pc = rv_cur(c);
    int known = same_sec(c, v);
    int t = link ? 1 : 6;
    int rdj = link ? 1 : 0;
    if (c->opt && c->floor < 8 && (v->unk || known)) {
        i64 off = known ? v->n - pc : 0;
        if (v->unk || fits_s(off, 21)) {
            if (c->cmp && !c->rv64 && link && !v->unk && off >= -2048 && off <= 2046 && !(off & 1) && c->floor < 4) {
                w16(c, 0x2001u | cj_bits(off));
                return 1;
            }
            if (c->cmp && !link && !v->unk && off >= -2048 && off <= 2046 && !(off & 1) && c->floor < 4) {
                w16(c, 0xa001u | cj_bits(off));
                return 1;
            }
            w32(c, enc_j(0x6f, rdj, off));
            c->mut = 1;
            return 1;
        }
    }
    rv_reloc(c, RK_RV_CALL, v, (i64)c->out->code.n);
    w32(c, 0x17u | ((u32)t << 7));
    w32(c, enc_i(0x67, rdj, t, 0));
    return 1;
}

/* fence with optional predecessor and successor sets. */
static int h_fence(RC *c)
{
    if (c->n == 0) { w32(c, 0x0ff0000fu); return 1; }
    if (c->n != 2) return usage(c, "[pred, succ]");
    u32 v[2];
    for (int k = 0; k < 2; k++) {
        const char *s = c->tok[k];
        u32 b = 0;
        if (isdigit((unsigned char)s[0])) {
            i64 x;
            int unk;
            if (!const_of(c, s, &x, &unk)) return 0;
            if (x < 0 || x > 15) { err("fence: value out of range (0 to 15)"); return 0; }
            b = (u32)x;
        } else {
            for (const char *q = s; *q; q++) {
                switch (tolower((unsigned char)*q)) {
                case 'i': b |= 8; break; case 'o': b |= 4; break; case 'r': b |= 2; break; case 'w': b |= 1; break;
                default: err("fence: bad set `%s' (use letters from i, o, r, w)", s); return 0;
                }
            }
        }
        v[k] = b;
    }
    w32(c, 0x0000000fu | (v[0] << 24) | (v[1] << 20));
    return 1;
}

/* csrr, csrw, csrs, csrc and the immediate forms. */
static int do_csr_pseudo(RC *c)
{
    const char *m = c->mn;
    int num, r;
    if (!strcmp(m, "csrr")) {
        if (c->n != 2) return usage(c, "rd, csr");
        if (!get_x(c, 0, &r) || !csr_num(c, c->tok[1], &num)) return 0;
        emit4(c, 0x2073u | ((u32)r << 7) | ((u32)num << 20));
        return 1;
    }
    if (!strcmp(m, "csrw") || !strcmp(m, "csrs") || !strcmp(m, "csrc")) {
        if (c->n != 2) return usage(c, "csr, rs");
        if (!csr_num(c, c->tok[0], &num) || !get_x(c, 1, &r)) return 0;
        u32 f3 = m[3] == 'w' ? 1 : m[3] == 's' ? 2 : 3;
        emit4(c, 0x73u | (f3 << 12) | ((u32)r << 15) | ((u32)num << 20));
        return 1;
    }
    if (!strcmp(m, "csrwi") || !strcmp(m, "csrsi") || !strcmp(m, "csrci")) {
        if (c->n != 2) return usage(c, "csr, uimm5");
        i64 v;
        int unk;
        if (!csr_num(c, c->tok[0], &num) || !const_of(c, c->tok[1], &v, &unk)) return 0;
        if (!unk && (v < 0 || v > 31)) { err("%s: immediate out of range (0 to 31)", m); return 0; }
        u32 f3 = m[3] == 'w' ? 5 : m[3] == 's' ? 6 : 7;
        emit4(c, 0x73u | (f3 << 12) | (((u32)v & 31) << 15) | ((u32)num << 20));
        return 1;
    }
    static const struct { const char *n; int csr; } rd[] = {
        { "rdcycle", 0xc00 }, { "rdtime", 0xc01 }, { "rdinstret", 0xc02 }, { "rdcycleh", 0xc80 }, { "rdtimeh", 0xc81 }, { "rdinstreth", 0xc82 },
        { "frcsr", 3 }, { "frrm", 2 }, { "frflags", 1 }, { NULL, 0 }
    };
    for (int i = 0; rd[i].n; i++) {
        if (!strcmp(m, rd[i].n)) {
            if (c->n != 1) return usage(c, "rd");
            if (!get_x(c, 0, &r)) return 0;
            emit4(c, 0x2073u | ((u32)r << 7) | ((u32)rd[i].csr << 20));
            return 1;
        }
    }
    static const struct { const char *n; int csr; } sw[] = { { "fscsr", 3 }, { "fsrm", 2 }, { "fsflags", 1 }, { NULL, 0 } };
    for (int i = 0; sw[i].n; i++) {
        if (!strcmp(m, sw[i].n)) {
            int rd2 = 0, rs;
            if (c->n == 1) { if (!get_x(c, 0, &rs)) return 0; }
            else if (c->n == 2) { if (!get_x(c, 0, &rd2) || !get_x(c, 1, &rs)) return 0; }
            else return usage(c, "[rd,] rs");
            emit4(c, 0x1073u | ((u32)rd2 << 7) | ((u32)rs << 15) | ((u32)sw[i].csr << 20));
            return 1;
        }
    }
    return -1;
}

/* All simple pseudo instructions: nop, mv, not, neg, seqz, snez, j, jr, ret, beqz, bgt and so on. */
static int do_pseudo(RC *c, int *handled)
{
    const char *m = c->mn;
    *handled = 1;
    int a, b;
    Val v;
    if (!strcmp(m, "nop")) { if (c->n) return usage(c, ""); emit(c, RV_NOP); return 1; }
    if (!strcmp(m, "unimp")) { if (c->cmp && c->floor < 4) w16(c, 0); else w32(c, 0xc0001073u); return 1; }
    if (!strcmp(m, "li")) return h_li(c);
    if (!strcmp(m, "la") || !strcmp(m, "lla") || !strcmp(m, "lga")) return h_la(c);
    if (!strcmp(m, "mv")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        emit(c, enc_i(0x13, a, b, 0));
        return 1;
    }
    if (!strcmp(m, "not")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        emit(c, enc_i(0x4013, a, b, -1));
        return 1;
    }
    if (!strcmp(m, "neg") || !strcmp(m, "negw")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        if (m[3] == 'w' && !c->rv64) { err("negw needs rv64"); return 0; }
        emit(c, (m[3] == 'w' ? 0x4000003bu : 0x40000033u) | ((u32)a << 7) | ((u32)b << 20));
        return 1;
    }
    if (!strcmp(m, "sext.w")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        if (!c->rv64) { err("sext.w needs rv64"); return 0; }
        emit(c, enc_i(0x1b, a, b, 0));
        return 1;
    }
    if (!strcmp(m, "zext.b")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        emit(c, enc_i(0x7013, a, b, 255));
        return 1;
    }
    if (!strcmp(m, "zext.w")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        if (!c->rv64) { err("zext.w needs rv64"); return 0; }
        if (!(c->ext & RVX_ZBA)) { w32(c, enc_i(0x1013, a, b, 32)); w32(c, enc_i(0x5013, a, a, 32)); return 1; }
        emit4(c, 0x0800003bu | ((u32)a << 7) | ((u32)b << 15));
        return 1;
    }
    if (!strcmp(m, "zext.h")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        if (c->ext & RVX_ZBB) { emit4(c, (c->rv64 ? 0x0800403bu : 0x08004033u) | ((u32)a << 7) | ((u32)b << 15)); return 1; }
        int sh = c->rv64 ? 48 : 16;
        w32(c, enc_i(0x1013, a, b, sh));
        w32(c, enc_i(0x5013, a, a, sh));
        return 1;
    }
    if (!strcmp(m, "rev8")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        if (!(c->ext & RVX_ZBB)) { err("rev8 needs the Zbb extension"); return 0; }
        emit4(c, enc_i(0x5013, a, b, c->rv64 ? 0x6b8 : 0x698));
        return 1;
    }
    if (!strcmp(m, "seqz") || !strcmp(m, "snez") || !strcmp(m, "sltz") || !strcmp(m, "sgtz")) {
        if (c->n != 2) return usage(c, "rd, rs");
        if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
        u32 w;
        if (m[1] == 'e') w = enc_i(0x3013, a, b, 1);
        else if (m[1] == 'n') w = F(0, 3, 0x33) | ((u32)a << 7) | ((u32)b << 20);
        else if (m[1] == 'l') w = F(0, 2, 0x33) | ((u32)a << 7) | ((u32)b << 15);
        else w = F(0, 2, 0x33) | ((u32)a << 7) | ((u32)b << 20);
        emit4(c, w);
        return 1;
    }
    if (!strcmp(m, "fgt.s") || !strcmp(m, "fge.s") || !strcmp(m, "fgt.d") || !strcmp(m, "fge.d")) {
        if (c->n != 3) return usage(c, "rd, frs1, frs2");
        int x, y;
        if (!get_x(c, 0, &a) || !get_f(c, 1, &x) || !get_f(c, 2, &y)) return 0;
        u32 base = 0xa0000053u | (m[4] == 'd' ? 1u << 25 : 0) | (m[2] == 't' ? 1u << 12 : 0);
        emit4(c, base | ((u32)a << 7) | ((u32)y << 15) | ((u32)x << 20));
        return 1;
    }
    {
        int f3, swap;
        if (branch_names(m, &f3, &swap)) {
            if (c->n != 3) return usage(c, "rs1, rs2, target");
            if (!get_x(c, 0, &a) || !get_x(c, 1, &b) || !ev(c, c->tok[2], &v)) return 0;
            if (swap) { int t = a; a = b; b = t; }
            if (c->cmp && f3 <= 1 && a == 0 && cr3(b)) { a = b; b = 0; }
            return emit_branch(c, f3, a, b, &v);
        }
    }
    {
        static const struct { const char *n; int f3, mode; } bz[] = {
            { "beqz", 0, 0 }, { "bnez", 1, 0 }, { "blez", 5, 1 }, { "bgez", 5, 0 }, { "bltz", 4, 0 }, { "bgtz", 4, 1 }, { NULL, 0, 0 }
        };
        for (int i = 0; bz[i].n; i++) {
            if (!strcmp(m, bz[i].n)) {
                if (c->n != 2) return usage(c, "rs, target");
                if (!get_x(c, 0, &a) || !ev(c, c->tok[1], &v)) return 0;
                if (bz[i].mode) return emit_branch(c, bz[i].f3, 0, a, &v);
                return emit_branch(c, bz[i].f3, a, 0, &v);
            }
        }
    }
    if (!strcmp(m, "j")) {
        if (c->n != 1) return usage(c, "target");
        if (!ev(c, c->tok[0], &v)) return 0;
        return emit_jal(c, 0, &v);
    }
    if (!strcmp(m, "jal")) {
        if (c->n == 1) {
            if (!ev(c, c->tok[0], &v)) return 0;
            return emit_jal(c, 1, &v);
        }
        if (c->n != 2) return usage(c, "[rd,] target");
        if (!get_x(c, 0, &a) || !ev(c, c->tok[1], &v)) return 0;
        return emit_jal(c, a, &v);
    }
    if (!strcmp(m, "jr")) {
        if (c->n != 1) return usage(c, "rs");
        if (!get_x(c, 0, &a)) return 0;
        emit(c, enc_i(0x67, 0, a, 0));
        return 1;
    }
    if (!strcmp(m, "jalr")) {
        if (c->n == 1) {
            if (!get_x(c, 0, &a)) return 0;
            emit(c, enc_i(0x67, 1, a, 0));
            return 1;
        }
        if (c->n == 2) {
            int base, rk, plain;
            Val off;
            if (!get_x(c, 0, &a)) return 0;
            if (!parse_mem(c, c->tok[1], &base, &off, &rk, &plain)) return 0;
            if (plain == 1) { err("jalr: expected `rs' or `offset(rs)', found `%s'", c->tok[1]); return 0; }
            if (plain == 2) {
                emit(c, enc_i(0x67, a, base, 0));
                return 1;
            }
            if (!val_isconst(&off) && !off.unk) {
                put_lo_reloc(c, rk == RM_NONE ? RM_LO : rk, &off, 0, (i64)c->out->code.n);
                emit4(c, enc_i(0x67, a, base, 0));
                return 1;
            }
            if (rk != RM_NONE) { put_lo_reloc(c, rk, &off, 0, (i64)c->out->code.n); emit4(c, enc_i(0x67, a, base, 0)); return 1; }
            if (!off.unk && !fits_s(off.n, 12)) { err("jalr: offset does not fit in 12 bits"); return 0; }
            emit(c, enc_i(0x67, a, base, off.n));
            return 1;
        }
        if (c->n == 3) {
            i64 imm;
            int rk;
            Val sym;
            if (!get_x(c, 0, &a) || !get_x(c, 1, &b)) return 0;
            if (!simm12_of(c, c->tok[2], &imm, &rk, &sym, 0)) return 0;
            if (rk != RM_NONE) { put_lo_reloc(c, rk, &sym, 0, (i64)c->out->code.n); emit4(c, enc_i(0x67, a, b, 0)); return 1; }
            if (!fits_s(imm, 12)) { err("jalr: offset does not fit in 12 bits"); return 0; }
            emit(c, enc_i(0x67, a, b, imm));
            return 1;
        }
        return usage(c, "rd, rs, offset");
    }
    if (!strcmp(m, "ret")) { if (c->n) return usage(c, ""); emit(c, enc_i(0x67, 0, 1, 0)); return 1; }
    if (!strcmp(m, "call") || !strcmp(m, "tail")) {
        if (c->n != 1) return usage(c, "target");
        if (!ev(c, c->tok[0], &v)) return 0;
        return emit_call(c, m[0] == 'c', &v);
    }
    if (!strcmp(m, "fence")) return h_fence(c);
    if (!strcmp(m, "fence.i") && c->n == 0) { emit4(c, 0x100f); return 1; }
    {
        int r = do_csr_pseudo(c);
        if (r >= 0) return r;
    }
    if (!strcmp(m, "fmv.s") || !strcmp(m, "fmv.d") || !strcmp(m, "fabs.s") || !strcmp(m, "fabs.d") || !strcmp(m, "fneg.s") || !strcmp(m, "fneg.d") || !strcmp(m, "fmv.h") || !strcmp(m, "fabs.h") || !strcmp(m, "fneg.h")) {
        int d = m[strlen(m) - 1] == 'd';
        int hf = m[strlen(m) - 1] == 'h';
        if (c->n != 2) return usage(c, "frd, frs");
        if (!get_f(c, 0, &a) || !get_f(c, 1, &b)) return 0;
        if ((hf && !(c->ext & RVX_ZFH)) || (d && !(c->ext & RVX_D)) || (!d && !hf && !(c->ext & RVX_F))) { err("%s needs the %s extension", m, hf ? "Zfh" : d ? "D" : "F"); return 0; }
        u32 f3 = m[1] == 'm' ? 0 : m[1] == 'n' ? 1 : 2;
        if (m[1] == 'a') f3 = 2;
        if (m[1] == 'n') f3 = 1;
        u32 w = 0x20000053u | (d ? 1u << 25 : 0) | (hf ? 2u << 25 : 0) | (f3 << 12) | ((u32)a << 7) | ((u32)b << 15) | ((u32)b << 20);
        emit4(c, w);
        return 1;
    }
    if (!strcmp(m, "sfence.vma")) {
        int x = 0, y = 0;
        if (c->n >= 1 && !get_x(c, 0, &x)) return 0;
        if (c->n >= 2 && !get_x(c, 1, &y)) return 0;
        if (c->n > 2) return usage(c, "[rs1, rs2]");
        emit4(c, 0x12000073u | ((u32)x << 15) | ((u32)y << 20));
        return 1;
    }
    *handled = 0;
    return 0;
}

typedef struct { const char *n; const char *m; const char *t; int nops; int only; int ext; } CAl;

static const CAl calias[] = {
    { "c.addi", "addi", "$1,$1,$2", 2, 0, 0 }, { "c.addiw", "addiw", "$1,$1,$2", 2, 2, 0 }, { "c.li", "addi", "$1,zero,$2", 2, 0, 0 },
    { "c.lui", "lui", "$1,$2", 2, 0, 0 }, { "c.mv", "add", "$1,zero,$2", 2, 0, 0 }, { "c.add", "add", "$1,$1,$2", 2, 0, 0 },
    { "c.and", "and", "$1,$1,$2", 2, 0, 0 }, { "c.or", "or", "$1,$1,$2", 2, 0, 0 }, { "c.xor", "xor", "$1,$1,$2", 2, 0, 0 },
    { "c.sub", "sub", "$1,$1,$2", 2, 0, 0 }, { "c.addw", "addw", "$1,$1,$2", 2, 2, 0 }, { "c.subw", "subw", "$1,$1,$2", 2, 2, 0 },
    { "c.andi", "andi", "$1,$1,$2", 2, 0, 0 }, { "c.slli", "slli", "$1,$1,$2", 2, 0, 0 }, { "c.srli", "srli", "$1,$1,$2", 2, 0, 0 }, { "c.srai", "srai", "$1,$1,$2", 2, 0, 0 },
    { "c.jr", "jalr", "zero,0($1)", 1, 0, 0 }, { "c.jalr", "jalr", "ra,0($1)", 1, 0, 0 },
    { "c.j", "jal", "zero,$1", 1, 0, 0 }, { "c.jal", "jal", "ra,$1", 1, 1, 0 },
    { "c.beqz", "beq", "$1,zero,$2", 2, 0, 0 }, { "c.bnez", "bne", "$1,zero,$2", 2, 0, 0 },
    { "c.lw", "lw", "$1,$2", 2, 0, 0 }, { "c.sw", "sw", "$1,$2", 2, 0, 0 }, { "c.ld", "ld", "$1,$2", 2, 2, 0 }, { "c.sd", "sd", "$1,$2", 2, 2, 0 },
    { "c.lwsp", "lw", "$1,$2", 2, 0, 0 }, { "c.swsp", "sw", "$1,$2", 2, 0, 0 }, { "c.ldsp", "ld", "$1,$2", 2, 2, 0 }, { "c.sdsp", "sd", "$1,$2", 2, 2, 0 },
    { "c.fld", "fld", "$1,$2", 2, 0, RVX_D }, { "c.fsd", "fsd", "$1,$2", 2, 0, RVX_D }, { "c.fldsp", "fld", "$1,$2", 2, 0, RVX_D }, { "c.fsdsp", "fsd", "$1,$2", 2, 0, RVX_D },
    { "c.flw", "flw", "$1,$2", 2, 1, RVX_F }, { "c.fsw", "fsw", "$1,$2", 2, 1, RVX_F }, { "c.flwsp", "flw", "$1,$2", 2, 1, RVX_F }, { "c.fswsp", "fsw", "$1,$2", 2, 1, RVX_F },
    { "c.addi4spn", "addi", "$1,$2,$3", 3, 0, 0 }, { "c.addi16sp", "addi", "$1,$1,$2", 2, 0, 0 },
    { "c.nop", "nop", "", 0, 0, 0 }, { "c.ebreak", "ebreak", "", 0, 0, 0 },
    { NULL, NULL, NULL, 0, 0, 0 }
};

/* True if the name is a compressed instruction alias. */
static int calias_known(const char *n)
{
    for (const CAl *k = calias; k->n; k++) if (!strcmp(k->n, n)) return 1;
    static const char *const zcb[] = { "c.zext.b", "c.sext.b", "c.zext.h", "c.sext.h", "c.zext.w", "c.not", "c.mul", "c.lbu", "c.lhu", "c.lh", "c.sb", "c.sh", NULL };
    for (int i = 0; zcb[i]; i++) if (!strcmp(zcb[i], n)) return 1;
    return 0;
}

/* Explicit compressed instructions (c.addi, c.lw, c.j ...). */
static int c_alias(RC *c)
{
    const CAl *a = NULL;
    for (const CAl *k = calias; k->n; k++) if (!strcmp(k->n, c->mn)) a = k;
    if (!a) {
        if (!strncmp(c->mn, "c.", 2)) {
            static const char *const zcb[] = { "c.zext.b", "c.sext.b", "c.zext.h", "c.sext.h", "c.zext.w", "c.not", "c.mul", "c.lbu", "c.lhu", "c.lh", "c.sb", "c.sh", NULL };
            for (int i = 0; zcb[i]; i++) if (!strcmp(zcb[i], c->mn)) { err("`%s' is a Zcb instruction and is not supported; write the full instruction and it is compressed for you", c->mn); return 0; }
            err("unknown RISC-V instruction `%s'", c->mn);
            return 0;
        }
        return 1;
    }
    if (!(c->ext & RVX_C)) { err("`%s' needs the C extension (it is off for this cpu)", c->mn); return 0; }
    if (a->only == 2 && !c->rv64) { err("`%s' is only available on rv64", c->mn); return 0; }
    if (a->only == 1 && c->rv64) { err("`%s' is only available on rv32", c->mn); return 0; }
    if (a->ext && !(c->ext & a->ext)) { err("`%s' needs an extension that is not enabled", c->mn); return 0; }
    if (c->n != a->nops) { err("%s: expected %d operand%s, found %d", c->mn, a->nops, a->nops == 1 ? "" : "s", c->n); return 0; }
    if (!strcmp(a->n, "c.addi16sp")) {
        int r;
        i64 v;
        int unk;
        if (!get_x(c, 0, &r)) return 0;
        if (r != 2) { err("c.addi16sp: the register must be sp"); return 0; }
        if (!const_of(c, c->tok[1], &v, &unk)) return 0;
        if (unk) v = 16;
        if (v == 0 || (v & 15) || v < -512 || v > 496) { err("c.addi16sp: the immediate must be a nonzero multiple of 16 from -512 to 496"); return 0; }
        u32 m = (u32)v;
        w16(c, 0x6101u | (((m >> 9) & 1) << 12) | (((m >> 4) & 1) << 6) | (((m >> 6) & 1) << 5) | (((m >> 7) & 3) << 3) | (((m >> 5) & 1) << 2));
        return 2;
    }
    {
        int bi = (!strcmp(a->n, "c.j") || !strcmp(a->n, "c.jal")) ? 0 : (!strcmp(a->n, "c.beqz") || !strcmp(a->n, "c.bnez")) ? 1 : -1;
        if (bi >= 0 && bi < c->n) {
            const char *q = c->tok[bi];
            while (*q == ' ') q++;
            if (*q == '+' || *q == '-') q++;
            if (isdigit((unsigned char)*q)) {
                size_t l = strlen(c->tok[bi]) + 8;
                char *nb = xmalloc(l);
                snprintf(nb, l, "$+(%s)", c->tok[bi]);
                free(c->tok[bi]);
                c->tok[bi] = nb;
            }
        }
    }
    char *nt[3];
    int nn = 0;
    const char *p = a->t;
    while (*p) {
        char buf[160];
        size_t l = 0;
        while (*p && *p != ',') {
            if (*p == '$' && p[1] >= '1' && p[1] <= '3') {
                const char *src = c->tok[p[1] - '1'];
                size_t sl = strlen(src);
                if (l + sl + 1 >= sizeof buf) { err("%s: operand too long", c->mn); return 0; }
                memcpy(buf + l, src, sl);
                l += sl;
                p += 2;
            } else buf[l++] = *p++;
        }
        buf[l] = 0;
        if (nn >= 3) break;
        nt[nn++] = xstrdup(buf);
        if (*p == ',') p++;
    }
    for (int i = 0; i < c->n; i++) free(c->tok[i]);
    for (int i = 0; i < nn; i++) c->tok[i] = nt[i];
    c->n = nn;
    snprintf(c->mn, sizeof c->mn, "%s", a->m);
    c->must16 = 1;
    c->cmp = 1;
    return 1;
}

/* Turns add/and/or/xor/slt with an immediate into the addi style form. */
static void promote_imm(RC *c)
{
    static const struct { const char *r, *i; int neg; } P[] = {
        { "add", "addi", 0 }, { "and", "andi", 0 }, { "or", "ori", 0 }, { "xor", "xori", 0 }, { "slt", "slti", 0 }, { "sltu", "sltiu", 0 },
        { "sll", "slli", 0 }, { "srl", "srli", 0 }, { "sra", "srai", 0 }, { "addw", "addiw", 0 }, { "sllw", "slliw", 0 }, { "srlw", "srliw", 0 }, { "sraw", "sraiw", 0 },
        { "sub", "addi", 1 }, { "subw", "addiw", 1 }, { "ror", "rori", 0 }, { "rorw", "roriw", 0 }, { "bset", "bseti", 0 }, { "bclr", "bclri", 0 }, { "binv", "binvi", 0 }, { "bext", "bexti", 0 },
        { NULL, NULL, 0 }
    };
    if (c->n != 3 || rv_xreg(c->tok[0]) < 0 || rv_xreg(c->tok[1]) < 0 || rv_xreg(c->tok[2]) >= 0) return;
    for (int k = 0; P[k].r; k++) {
        if (strcmp(P[k].r, c->mn)) continue;
        snprintf(c->mn, sizeof c->mn, "%s", P[k].i);
        if (P[k].neg) {
            size_t l = strlen(c->tok[2]) + 4;
            char *n = xmalloc(l);
            snprintf(n, l, "-(%s)", c->tok[2]);
            free(c->tok[2]);
            c->tok[2] = n;
        }
        return;
    }
}

/* Entry point. Parses the mnemonic and operands and encodes the instruction or pseudo instruction. */
int rv_insn(const char *mn, const char *rest, IC *ic, InsnOut *out)
{
    RC c;
    memset(&c, 0, sizeof c);
    size_t i = 0;
    for (; mn[i] && i < sizeof c.mn - 1; i++) c.mn[i] = (char)tolower((unsigned char)mn[i]);
    c.mn[i] = 0;
    c.ic = ic;
    c.out = out;
    c.floor = ic->wide;
    c.rv64 = ic->arch == ARCH_RV64;
    c.ext = ic->rvext;
    c.opt = ic->optimize;
    c.cmp = (c.ext & RVX_C) && ic->optimize;
    int ok = 0;
    if (!tokenize(rest, &c)) goto done;
    {
        int ca = c_alias(&c);
        if (!ca) goto done;
        if (ca == 2) { ok = 1; finish(&c); goto done; }
    }
    promote_imm(&c);
    {
        int handled = 0;
        const RvEnt *e = rv_find(c.mn, &c.aq, &c.rl);
        int is_ldst = e && strlen(e->pat) >= 3 && (e->pat[2] == 'l' || e->pat[2] == 'm') && e->pat[1] == ',';
        if (is_ldst && (c.n == 2 || c.n == 3)) {
            int base, rk, plain;
            Val off;
            int save = c.n;
            c.n = 2;
            Val tmp;
            int okp = parse_mem(&c, c.tok[1], &base, &tmp, &rk, &plain);
            c.n = save;
            (void)off;
            if (okp && plain == 1 && e->ext) {
                if ((e->only == 2 && !c.rv64)) { err("`%s' needs rv64", c.mn); goto done; }
                if (!(c.ext & e->ext)) { err("`%s' needs an extension that is not enabled", c.mn); goto done; }
                ok = h_ldst_sym(&c, e, e->pat[0] == 't' || e->pat[0] == 'T');
                if (ok) finish(&c);
                goto done;
            }
            if (!okp) goto done;
        }
        ok = do_pseudo(&c, &handled);
        if (handled) {
            if (ok) finish(&c);
            goto done;
        }
        if (!e) {
            err("unknown RISC-V instruction `%s'", mn);
            goto done;
        }
        if (e->only == 2 && !c.rv64) { err("`%s' is only available on rv64", c.mn); goto done; }
        if (e->only == 1 && c.rv64) { err("`%s' is only available on rv32", c.mn); goto done; }
        if (!(c.ext & e->ext)) {
            const char *en = e->ext == RVX_ZFH ? "Zfh" : e->ext == RVX_ZFHMIN ? "Zfhmin" : e->ext == RVX_ZBKB ? "Zbkb" : e->ext == RVX_ZBKX ? "Zbkx" : e->ext == RVX_ZKND ? "Zknd" : e->ext == RVX_ZKNE ? "Zkne" : e->ext == RVX_ZKNH ? "Zknh" : e->ext == RVX_ZKSED ? "Zksed" : e->ext == RVX_ZKSH ? "Zksh" : e->ext == RVX_ZICBOM ? "Zicbom" : e->ext == RVX_ZICBOZ ? "Zicboz" : e->ext == RVX_ZICBOP ? "Zicbop" : e->ext == RVX_ZAWRS ? "Zawrs" : e->ext == RVX_SVINVAL ? "Svinval" : e->ext == RVX_H ? "H" : e->ext == RVX_M ? "M" : e->ext == RVX_A ? "A" : e->ext == RVX_F ? "F" : e->ext == RVX_D ? "D" : e->ext == RVX_ZICSR ? "Zicsr" : e->ext == RVX_ZIFENCEI ? "Zifencei" : e->ext == RVX_ZBA ? "Zba" : e->ext == RVX_ZBB ? "Zbb" : e->ext == RVX_ZBS ? "Zbs" : e->ext == RVX_ZBC ? "Zbc" : e->ext == RVX_ZICOND ? "Zicond" : "base";
            err("`%s' needs the %s extension (it is off for this cpu, try `cpu rv64gc' or add _%c%s)", c.mn, en, tolower((unsigned char)en[0]), en + 1);
            goto done;
        }
        if ((c.aq || c.rl) && e->ext != RVX_A) { err("`%s': aq/rl suffixes only apply to atomics", c.mn); goto done; }
        ok = do_pattern(&c, e);
        if (ok) finish(&c);
    }
    if (ok && c.must16 && c.out->code.n != 2) {
        err("%s: these operands cannot be encoded in 16 bits", mn);
        ok = 0;
    }
done:
    for (int k = 0; k < c.n; k++) free(c.tok[k]);
    return ok;
}
