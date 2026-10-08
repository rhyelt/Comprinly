#include "insn.h"

static int cur_bits = 32;

static int fits_sb(i64 n, int w)
{
    if (w == 16) return (n >= -128 && n <= 127) || (n >= 0xFF80 && n <= 0xFFFF);
    return (n >= -128 && n <= 127) || (n >= 0xFFFFFF80LL && n <= 0xFFFFFFFFLL);
}

static int memsz_ok(const Op *o, int sz)
{
    return o->size == 0 || sz == 0 || o->size == sz;
}

static int cls_is(const Op *o, int c)
{
    if (o->kind != OK_REG || REGCLS(o->reg) != c) return 0;
    if ((c == RC_XMM || c == RC_YMM) && cur_bits != 64) return REGNUM(o->reg) < 8;
    return 1;
}

static int vreg_ok(const Op *o, int rc)
{
    if (o->kind != OK_REG || REGCLS(o->reg) != rc) return 0;
    int n = REGNUM(o->reg);
    return rc == RC_K ? n < 8 : (cur_bits == 64 || !(n & 8));
}

static const int sv_rc[4] = { RC_XMM, RC_YMM, RC_ZMM, RC_K };

static int sv_msize(int s)
{
    int c = SV_MEM(s);
    return c ? 1 << (c - 1) : 0;
}

static int spec_bytes(int s)
{
    return IS_SV(s) ? sv_msize(s) : spec_msize(s);
}

static int match_sv(int s, const Op *o)
{
    int rc = sv_rc[SV_CLS(s)];
    if (SV_VSIB(s)) return o->kind == OK_MEM && o->index && REGCLS(o->index) == rc && !(o->flags & OF_FAR);
    if (vreg_ok(o, rc)) return 1;
    int ms = sv_msize(s);
    if (!ms || o->kind != OK_MEM || (o->flags & OF_FAR)) return 0;
    if (o->index && REGCLS(o->index) >= RC_XMM) return 0;
    return 1;
}

static int has_near_alt(const Ent *head)
{
    for (const Ent *e = head; e; e = e->next)
        for (int i = 0; i < e->nops; i++) if (e->sp[i] == S_REL) return 1;
    return 0;
}

static int match_spec(int s, const Op *o, IC *ic, const Ent *e, const Ent *head)
{
    int k = o->kind;
    if (IS_SV(s)) return match_sv(s, o);
    switch (s) {
    case S_M512: return k == OK_MEM && memsz_ok(o, 64) && !(o->flags & OF_FAR) && !(o->index && REGCLS(o->index) >= RC_XMM);
    case S_BND: case S_BNDR: return o->kind == OK_REG && REGCLS(o->reg) == RC_BND;
    case S_R8: case S_RR8: return cls_is(o, RC_R8);
    case S_R16: case S_RR16: return cls_is(o, RC_R16);
    case S_R32: case S_RR32: return cls_is(o, RC_R32);
    case S_R64: case S_RR64: return cls_is(o, RC_R64);
    case S_RM64: return cls_is(o, RC_R64) || (k == OK_MEM && memsz_ok(o, 8) && !(o->flags & OF_FAR));
    case S_IBS: return k == OK_IMM && (o->size == 1 || (val_isconst(&o->val) && (o->size == 0) && o->val.n >= -128 && o->val.n <= 127));
    case S_IBU: return k == OK_IMM && (o->size == 1 || (val_isconst(&o->val) && (o->size == 0) && o->val.n >= -128 && o->val.n <= 255));
    case S_ECX: return o->kind == OK_REG && o->reg == REGID(RC_R32, 1);
    case S_EDX: return o->kind == OK_REG && o->reg == REGID(RC_R32, 2);
    case S_CX: return o->kind == OK_REG && o->reg == REGID(RC_R16, 1);
    case S_SB64: return k == OK_IMM && val_isconst(&o->val) && (!(o->flags & OF_STRICT) || o->size == 1) && (o->size == 0 || o->size == 1 || o->size == 4 || o->size == 8) && o->val.n >= -128 && o->val.n <= 127;
    case S_RAX: return o->kind == OK_REG && o->reg == REGID(RC_R64, 0);
    case S_RCX: return o->kind == OK_REG && o->reg == REGID(RC_R64, 1);
    case S_IMM64: return k == OK_IMM && (o->size == 0 || o->size == 8);
    case S_SD: return k == OK_IMM && val_isconst(&o->val) && (o->size == 0 || o->size == 4) && o->val.n >= -2147483648LL && o->val.n <= 2147483647LL;
    case S_UD: return k == OK_IMM && val_isconst(&o->val) && (o->size == 0 || o->size == 4) && o->val.n >= 0 && o->val.n <= 4294967295LL;
    case S_MO64: {
        if (k != OK_MEM || o->base || o->index || (o->flags & OF_FAR) || (o->flags & OF_NOSPLIT)) return 0;
        if (o->dispsize == 1) return 0;
        if (o->asize != 64) return 0;
        return memsz_ok(o, 8);
    }
    case S_RM8: return cls_is(o, RC_R8) || (k == OK_MEM && memsz_ok(o, 1) && !(o->flags & OF_FAR));
    case S_RM16: return cls_is(o, RC_R16) || (k == OK_MEM && memsz_ok(o, 2) && !(o->flags & OF_FAR));
    case S_RM32: return cls_is(o, RC_R32) || (k == OK_MEM && memsz_ok(o, 4) && !(o->flags & OF_FAR));
    case S_R32M16: return cls_is(o, RC_R32) || (k == OK_MEM && memsz_ok(o, 2) && !(o->flags & OF_FAR));
    case S_R32M8: return cls_is(o, RC_R32) || (k == OK_MEM && memsz_ok(o, 1) && !(o->flags & OF_FAR));
    case S_M: return k == OK_MEM && !(o->flags & OF_FAR) && !(o->index && REGCLS(o->index) >= RC_XMM);
    case S_M8: return k == OK_MEM && memsz_ok(o, 1) && !(o->flags & OF_FAR);
    case S_M16: return k == OK_MEM && memsz_ok(o, 2) && !(o->flags & OF_FAR);
    case S_M32: return k == OK_MEM && memsz_ok(o, 4) && !(o->flags & OF_FAR);
    case S_M64: return k == OK_MEM && memsz_ok(o, 8) && !(o->flags & OF_FAR);
    case S_M80: return k == OK_MEM && memsz_ok(o, 10) && !(o->flags & OF_FAR);
    case S_M128: return k == OK_MEM && memsz_ok(o, 16) && !(o->flags & OF_FAR);
    case S_M256: return k == OK_MEM && memsz_ok(o, 32) && !(o->flags & OF_FAR);
    case S_MFAR: return k == OK_MEM && (o->flags & OF_FAR);
    case S_MO8: case S_MO16: case S_MO32: {
        if (k != OK_MEM || o->base || o->index || (o->flags & OF_FAR) || (o->flags & OF_NOSPLIT)) return 0;
        if (o->dispsize == 1) return 0;
        if (cur_bits == 64 && o->asize != 64) return 0;
        int sz = s == S_MO8 ? 1 : (s == S_MO16 ? 2 : 4);
        if (!memsz_ok(o, sz)) return 0;
        return 1;
    }
    case S_IMM8: return k == OK_IMM && (o->size == 0 || o->size == 1);
    case S_IMM16: return k == OK_IMM && (o->size == 0 || o->size == 2);
    case S_IMM32: return k == OK_IMM && (o->size == 0 || o->size == 4);
    case S_SB16: return k == OK_IMM && val_isconst(&o->val) && (!(o->flags & OF_STRICT) || o->size == 1) && (o->size == 0 || o->size == 1 || o->size == 2) && fits_sb(o->val.n, 16);
    case S_SB32: return k == OK_IMM && val_isconst(&o->val) && (!(o->flags & OF_STRICT) || o->size == 1) && (o->size == 0 || o->size == 1 || o->size == 4) && fits_sb(o->val.n, 32);
    case S_ONE: return k == OK_IMM && val_isconst(&o->val) && o->val.n == 1 && !(o->flags & OF_STRICT) && (o->size == 0 || o->size == 1);
    case S_AL: return o->kind == OK_REG && o->reg == REGID(RC_R8, 0);
    case S_CL: return o->kind == OK_REG && o->reg == REGID(RC_R8, 1);
    case S_DX: return o->kind == OK_REG && o->reg == REGID(RC_R16, 2);
    case S_AX: return o->kind == OK_REG && o->reg == REGID(RC_R16, 0);
    case S_EAX: return o->kind == OK_REG && o->reg == REGID(RC_R32, 0);
    case S_ES: case S_CS: case S_SS: case S_DS: case S_FS: case S_GS:
        return o->kind == OK_REG && o->reg == REGID(RC_SEG, s - S_ES);
    case S_SREG: return cls_is(o, RC_SEG);
    case S_CR: return cls_is(o, RC_CR);
    case S_DR: return cls_is(o, RC_DR);
    case S_TR: return cls_is(o, RC_TR);
    case S_MM: case S_MMR: return cls_is(o, RC_MMX);
    case S_MM32: return cls_is(o, RC_MMX) || (k == OK_MEM && memsz_ok(o, 4));
    case S_MM64: return cls_is(o, RC_MMX) || (k == OK_MEM && memsz_ok(o, 8));
    case S_XMM: case S_XMMR: return cls_is(o, RC_XMM);
    case S_XM16: return cls_is(o, RC_XMM) || (k == OK_MEM && memsz_ok(o, 2));
    case S_XM32: return cls_is(o, RC_XMM) || (k == OK_MEM && memsz_ok(o, 4));
    case S_XM64: return cls_is(o, RC_XMM) || (k == OK_MEM && memsz_ok(o, 8));
    case S_XM128: return cls_is(o, RC_XMM) || (k == OK_MEM && memsz_ok(o, 16));
    case S_YMM: case S_YMMR: return cls_is(o, RC_YMM);
    case S_YM256: return cls_is(o, RC_YMM) || (k == OK_MEM && memsz_ok(o, 32));
    case S_ST0: return o->kind == OK_REG && o->reg == REGID(RC_ST, 0);
    case S_STI: return cls_is(o, RC_ST);
    case S_XMM0: return o->kind == OK_REG && o->reg == REGID(RC_XMM, 0);
    case S_REL8: {
        if (k != OK_IMM || (o->flags & (OF_NEAR | OF_FAR))) return 0;
        if (o->size && o->size != 1) return 0;
        int near_alt = has_near_alt(head);
        if (near_alt && !(o->flags & OF_SHORT) && ic->wide) return 0;
        const Val *v = &o->val;
        if (v->unk) return 1;
        if (!(o->flags & OF_SHORT) && near_alt) {
            if (v->ext || v->neg >= 0 || v->pos != ic->cur_sec || v->hasreg) { return 0; }
        }
        if (v->pos == ic->cur_sec && v->neg < 0 && !v->ext) {
            int extra = ((strstr(e->enc, "a16") && ic->bits == 32) || (strstr(e->enc, "a32") && ic->bits == 16)) + (ic->bnd ? 1 : 0);
            i64 d = v->n - (ic->start_off + e->fixedlen + extra);
            if (d < -128 || d > 127) {
                if (near_alt && !(o->flags & OF_SHORT)) { ic->need_wide = 1; return 0; }
                err("short jump is out of range");
                return 0;
            }
        }
        return 1;
    }
    case S_REL: return k == OK_IMM && !(o->flags & (OF_SHORT | OF_FAR)) && (o->size == 0 || o->size == 2 || o->size == 4);
    case S_FARPTR: return k == OK_FAR;
    }
    return 0;
}

static int o_sizeok(int s, const Op *o)
{
    if (o->kind != OK_MEM || SV_VSIB(s)) return 1;
    int ms = sv_msize(s);
    return o->size == 0 || o->size == ms || o->bcst;
}

static int deco_ok(const Ent *e, int i, const Op *o, IC *ic)
{
    int dc = e->dc[i];
    if (o->kreg && !(dc & DC_MASK)) return 0;
    if (o->zero && !(dc & DC_Z)) return 0;
    if (o->rc == 5 && !(dc & DC_SAE)) return 0;
    if (o->rc >= 1 && o->rc <= 4 && !(dc & DC_ER)) return 0;
    if (o->rc && o->kind != OK_REG) return 0;
    if (o->bcst) {
        if (!(dc & DC_BC) || o->kind != OK_MEM) return 0;
        int eb = (dc >> 6) & 3;
        int esz = eb == 1 ? 2 : (eb == 2 ? 4 : 8);
        int tot = spec_bytes(e->sp[i]);
        if (o->bcst * esz != tot) { ic->why = "mismatch in the number of broadcasting elements"; return 0; }
        if (o->size && o->size != esz && o->size != tot) return 0;
    }
    return 1;
}

static int entry_matches(const Ent *e, const Op *ops, int nops, IC *ic, const Ent *head)
{
    if (e->nops != nops) return 0;
    if (e->mode == 1 && cur_bits != 64) return 0;
    if (cur_bits == 64 && nops == 2 && ops[0].kind == OK_REG && ops[1].kind == OK_REG && ops[0].reg == ops[1].reg && REGCLS(ops[0].reg) == RC_R32 && !strcmp(e->mn, "xchg") && strstr(e->enc, "90+r")) return 0;
    if (e->mode == 2 && cur_bits == 64) return 0;
    for (int i = 0; i < nops; i++) {
        if (!match_spec(e->sp[i], &ops[i], ic, e, head)) return 0;
        if (!deco_ok(e, i, &ops[i], ic)) return 0;
        if (IS_SV(e->sp[i]) && !o_sizeok(e->sp[i], &ops[i])) return 0;
        if (e->vkind && ops[i].kind == OK_REG && REGCLS(ops[i].reg) == RC_R8 && REGNUM(ops[i].reg) >= 4 && REGNUM(ops[i].reg) < 8) { ic->why = "cannot use high register in AVX instruction"; return 0; }
        if ((e->vkind == VK_VEX || e->vkind == VK_XOP) && cur_bits != 64) {
            if (ops[i].kind == OK_REG && REGCLS(ops[i].reg) >= RC_XMM && REGCLS(ops[i].reg) != RC_K && REGNUM(ops[i].reg) >= 8) return 0;
            if (ops[i].kind == OK_MEM && ops[i].index && REGCLS(ops[i].index) >= RC_XMM && REGNUM(ops[i].index) >= 8) return 0;
        }
        if (e->vkind && cur_bits != 64 && e->rl[i] == 'v' && ops[i].kind == OK_REG && REGCLS(ops[i].reg) >= RC_XMM && REGNUM(ops[i].reg) >= 8) return 0;
    }
    if (!e->vkind) for (int i = 0; i < nops; i++) {
        const Op *o = &ops[i];
        if (o->kreg || o->zero || o->bcst || o->rc) return 0;
    }
    return 1;
}

static int is_reg_role(int s)
{
    switch (s) {
    case S_R8: case S_R16: case S_R32: case S_R64: case S_SREG: case S_CR: case S_DR: case S_TR: case S_MM: case S_XMM: case S_YMM: case S_BND: return 1;
    }
    return 0;
}

static int is_rm_role(int s)
{
    switch (s) {
    case S_RR8: case S_RR16: case S_RR32: case S_RR64: case S_RM64: case S_RM8: case S_RM16: case S_RM32:
    case S_M: case S_M8: case S_M16: case S_M32: case S_M64: case S_M80: case S_M128: case S_M256: case S_MFAR:
    case S_R32M16: case S_R32M8: case S_MMR: case S_XMMR: case S_YMMR: case S_MM32: case S_MM64: case S_BNDR: case S_M512:
    case S_XM16: case S_XM32: case S_XM64: case S_XM128: case S_YM256:
        return 1;
    }
    return 0;
}

static int is_imm_spec(int s)
{
    return s == S_IMM8 || s == S_IMM16 || s == S_IMM32 || s == S_SB16 || s == S_SB32 || s == S_IMM64 || s == S_SD || s == S_UD || s == S_SB64 || s == S_IBS || s == S_IBU;
}

typedef struct {
    Bytes b;
    Reloc r[8];
    int nr;
    int rw, rr, rx, rb, needrex, high8;
} Buf;

static int rfield(Buf *bf, int reg, int *bit)
{
    int n = REGNUM(reg);
    int c = REGCLS(reg);
    if (c == RC_R8) {
        if (n >= 20) { bf->needrex = 1; return 4 + (n - 20); }
        if (n >= 4 && n < 8) bf->high8 = 1;
    }
    if (n >= 8 && n < 16 && (c == RC_R8 || c == RC_R16 || c == RC_R32 || c == RC_R64 || c == RC_CR || c == RC_DR || c == RC_XMM || c == RC_MMX || c == RC_YMM || c == RC_ZMM)) {
        if (c == RC_MMX) return n & 7;
        *bit = 1;
    }
    return n & 7;
}

static void put_val(Buf *bf, const Val *v, int size, int kind, IC *ic)
{
    if (v->unk || (val_isconst(v) && kind == RK_ABS)) {
        i64 n = v->n;
        if (kind == RK_ABS && !v->unk) {
            int ok;
            if (size == 1) ok = n >= -128 && n <= 255;
            else if (size == 2) ok = n >= -32768 && n <= 65535;
            else if (size == 4) ok = n >= -2147483648LL && n <= 4294967295LL;
            else ok = 1;
            if (!ok) warn("%s data exceeds bounds", size == 1 ? "byte" : (size == 2 ? "word" : "dword"));
        }
        badle(&bf->b, (u64)n, size);
        return;
    }
    (void)ic;
    if (bf->nr >= 8) { err("internal: too many relocations"); return; }
    Reloc *r = &bf->r[bf->nr++];
    memset(r, 0, sizeof *r);
    r->off = (i64)bf->b.n;
    r->size = size;
    r->kind = kind;
    r->v = *v;
    badle(&bf->b, 0, size);
}

static int log2sc(int s) { return s == 1 ? 0 : (s == 2 ? 1 : (s == 4 ? 2 : 3)); }

static int emit_ea(const Op *m, int regf, IC *ic, Buf *bf, int *asz_out, int d8n)
{
    int asz;
    int b16 = m->base && REGCLS(m->base) == RC_R16;
    int i16 = m->index && REGCLS(m->index) == RC_R16;
    int b32 = m->base && REGCLS(m->base) == RC_R32;
    int b64 = m->base && (REGCLS(m->base) == RC_R64 || REGCLS(m->base) == RC_RIP);
    int ivec = m->index && REGCLS(m->index) >= RC_XMM;
    int i32 = m->index && (REGCLS(m->index) == RC_R32 || ivec);
    int i64r = m->index && REGCLS(m->index) == RC_R64;
    int isrip = m->base && REGCLS(m->base) == RC_RIP;
    int n16 = b16 || i16, n32 = b32 || i32, n64 = b64 || i64r;
    if ((n16 && n32) || (n16 && n64) || (n32 && n64 && !ivec) || (b32 && i64r) || (b64 && i32 && !ivec)) { err("invalid mix of address register sizes"); return 0; }
    if (n16) asz = 16;
    else if (n64) asz = 64;
    else if (n32) asz = 32;
    else asz = ic->a16 ? 16 : (ic->a32 ? 32 : (m->asize ? m->asize : ic->bits));
    if (ivec && b64) asz = 64;
    if (cur_bits == 64 && asz == 16) { err("16-bit addressing is not supported in 64-bit mode"); return 0; }
    if (cur_bits != 64 && asz == 64) { err("64-bit addressing is only available in 64-bit mode"); return 0; }
    *asz_out = asz;
    const Val *d = &m->val;
    int dconst = val_isconst(d);
    i64 dv = d->n;
    if (dconst) dv = asz == 16 ? (i64)(int16_t)dv : (asz == 32 ? (i64)(int32_t)dv : dv);
    if (asz >= 32) {
        int ripmode = isrip || (asz == 64 && !m->base && !m->index && !(m->flags & OF_ABS) && (m->flags & OF_REL) && !dconst);
        int base = m->base ? REGNUM(m->base) : -1;
        int idx = m->index ? (ivec ? REGNUM(m->index) : REGNUM(m->index)) : -1;
        int sc = m->scale ? m->scale : 1;
        if (!ivec && idx >= 0 && (idx & 15) == 4) { err("`%s' cannot be used as an index register", asz == 64 ? "rsp" : "esp"); return 0; }
        if (ripmode) {
            if (m->index) { err("invalid effective address"); return 0; }
            bad1(&bf->b, (u8)((regf << 3) | 5));
            size_t o = bf->b.n;
            if (isrip && dconst) badle(&bf->b, (u64)dv, 4);
            else {
                if (bf->nr >= 8) { err("internal: too many relocations"); return 0; }
                Reloc *r = &bf->r[bf->nr++];
                memset(r, 0, sizeof *r);
                r->off = (i64)o;
                r->size = 4;
                r->kind = RK_REL;
                r->relbase = -1;
                r->v = *d;
                badle(&bf->b, 0, 4);
            }
            return 1;
        }
        if (base >= 8) bf->rb = 1;
        if (idx >= 8 && !ivec) bf->rx = 1;
        if (ivec && idx >= 8) bf->rx = 1;
        int lb = base & 7;
        int li = idx >= 0 ? (ivec ? idx & 7 : idx & 7) : -1;
        int dsz;
        if (base < 0 && idx < 0) dsz = 4;
        else if (base < 0) dsz = 4;
        else if (m->dispsize) dsz = m->dispsize == 2 ? 4 : m->dispsize;
        else if (!dconst) dsz = 4;
        else if (dv == 0 && lb != 5) dsz = 0;
        else if (d8n > 1 ? (dv % d8n == 0 && dv / d8n >= -128 && dv / d8n <= 127) : (dv >= -128 && dv <= 127)) dsz = 1;
        else dsz = 4;
        if (dsz == 1 && d8n > 1 && dconst) {
            if (dv % d8n) { err("displacement is not a multiple of %d for a compressed disp8", d8n); return 0; }
            dv /= d8n;
        }
        int mod = dsz == 0 ? 0 : (dsz == 1 ? 1 : 2);
        if (base < 0 && idx < 0) {
            if (cur_bits == 64) {
                bad1(&bf->b, (u8)((regf << 3) | 4));
                bad1(&bf->b, 0x25);
            } else bad1(&bf->b, (u8)((regf << 3) | 5));
        } else if (base < 0) {
            bad1(&bf->b, (u8)((regf << 3) | 4));
            bad1(&bf->b, (u8)((log2sc(sc) << 6) | (li << 3) | 5));
        } else if (idx >= 0 || lb == 4) {
            bad1(&bf->b, (u8)((mod << 6) | (regf << 3) | 4));
            bad1(&bf->b, (u8)((log2sc(sc) << 6) | ((idx < 0 ? 4 : li) << 3) | lb));
        } else {
            bad1(&bf->b, (u8)((mod << 6) | (regf << 3) | lb));
        }
        if (dsz == 1) {
            if (!dconst) { put_val(bf, d, 1, RK_ABS, ic); }
            else bad1(&bf->b, (u8)dv);
        } else if (dsz == 4) put_val(bf, d, 4, RK_ABS, ic);
        return 1;
    }
    int bn = m->base ? REGNUM(m->base) : -1;
    int in = m->index ? REGNUM(m->index) : -1;
    if (m->scale > 1) { err("invalid scale in 16-bit address"); return 0; }
    int rm = -1;
    if (bn < 0 && in < 0) rm = 6;
    else {
        int a = bn, bq = in;
        if (a >= 0 && bq >= 0) {
            if ((a == 6 || a == 7) && (bq == 3 || bq == 5)) { int t = a; a = bq; bq = t; }
            if (a == 3 && bq == 6) rm = 0;
            else if (a == 3 && bq == 7) rm = 1;
            else if (a == 5 && bq == 6) rm = 2;
            else if (a == 5 && bq == 7) rm = 3;
        } else {
            int r = a >= 0 ? a : bq;
            if (r == 6) rm = 4;
            else if (r == 7) rm = 5;
            else if (r == 5) rm = 6;
            else if (r == 3) rm = 7;
        }
        if (rm < 0) { err("invalid 16-bit effective address"); return 0; }
    }
    int dsz;
    if (bn < 0 && in < 0) dsz = 2;
    else if (m->dispsize) dsz = m->dispsize == 4 ? 2 : m->dispsize;
    else if (!dconst) dsz = 2;
    else if (dv == 0 && rm != 6) dsz = 0;
    else if (dv >= -128 && dv <= 127) dsz = 1;
    else dsz = 2;
    int mod = (bn < 0 && in < 0) ? 0 : (dsz == 0 ? 0 : (dsz == 1 ? 1 : 2));
    bad1(&bf->b, (u8)((mod << 6) | (regf << 3) | rm));
    if (dsz == 1) {
        if (!dconst) put_val(bf, d, 1, RK_ABS, ic);
        else bad1(&bf->b, (u8)dv);
    } else if (dsz == 2) put_val(bf, d, 2, RK_ABS, ic);
    return 1;
}

static int seg_prefix_byte(int reg)
{
    static const u8 p[] = { 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65 };
    return p[REGNUM(reg)];
}

static char *nexttok(char **pp)
{
    char *p = *pp;
    while (*p == ' ') p++;
    if (!*p) { *pp = p; return NULL; }
    char *s = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
    *pp = p;
    return s;
}


static int tuple_n(const Ent *e, int rmi, int bc)
{
    int vlb = 16 << e->vL;
    int w = e->vW == 1;
    int sz = spec_bytes(e->sp[rmi]);
    switch (e->tuple) {
    case TU_FV: return bc ? (w ? 8 : 4) : vlb;
    case TU_FVM: return vlb;
    case TU_HV: return bc ? 4 : vlb / 2;
    case TU_HVM: return vlb / 2;
    case TU_QVM: return vlb / 4;
    case TU_OVM: return vlb / 8;
    case TU_T1S: (void)sz; return w ? 8 : 4;
    case TU_T1S8: return 1;
    case TU_T1S16: return 2;
    case TU_T1F32: return 4;
    case TU_T1F64: return 8;
    case TU_T2: return 8 << w;
    case TU_T4: return 16 << w;
    case TU_T8: return 32;
    case TU_M128: return 16;
    case TU_DUP: return e->vL == 0 ? 8 : vlb;
    }
    return 1;
}

static int vec_body(const Ent *e, Op *ops, int nops, IC *ic, Buf *bf, Bytes *pre, int *need67, int *memseg)
{
    int regop = -1, rmop = -1, vvop = -1, immop = -1, sop = -1;
    for (int i = 0; i < nops; i++) {
        switch (e->rl[i]) {
        case 's': sop = i; break;
        case 'r': regop = i; break;
        case 'm': rmop = i; break;
        case 'v': vvop = i; break;
        case 'i': immop = i; break;
        }
    }
    int kreg = 0, zero = 0, rc = 0, bc = 0;
    for (int i = 0; i < nops; i++) {
        if (ops[i].kreg && !kreg) kreg = ops[i].kreg;
        if (ops[i].zero) zero = 1;
        if (ops[i].rc) rc = ops[i].rc;
        if (ops[i].bcst) bc = 1;
    }
    char *enc = xstrdup(e->enc);
    char *sv = enc;
    int regf = 0, haveR = 0, opc[4], nopc = 0, hasib = 0, post[4], npost = 0, is4 = 0;
    for (char *t = nexttok(&sv); t; t = nexttok(&sv)) {
        if (!strncmp(t, "xv:", 3) || !strncmp(t, "rl:", 3) || !strcmp(t, "dup") || !strcmp(t, "late") || !strcmp(t, "sf")) continue;
        if (t[0] == '/') {
            if (t[1] == 'r') haveR = 1; else { haveR = 2; regf = t[1] - '0'; }
            continue;
        }
        if (!strcmp(t, "ib")) { hasib = 1; continue; }
        if (!strcmp(t, "id")) { hasib = 4; continue; }
        if (!strcmp(t, "is4")) { is4 = 1; continue; }
        if (t[0] == 'p' && isxdigit((unsigned char)t[1]) && npost < 4) { post[npost++] = (int)strtol(t + 1, NULL, 16); continue; }
        if (isxdigit((unsigned char)t[0]) && isxdigit((unsigned char)t[1]) && !t[2] && nopc < 4) { opc[nopc++] = (int)strtol(t, NULL, 16); continue; }
        err("internal: bad vector encoding token `%s'", t);
        free(enc);
        return 0;
    }
    free(enc);
    int regn = 0, rmn = 0, mx = 0;
    int vvn = 0;
    if (haveR == 1) {
        if (regop < 0) { err("internal: no reg operand for %s", e->mn); return 0; }
        regn = REGNUM(ops[regop].reg);
    } else regn = regf;
    const Op *rmo = NULL;
    if (rmop < 0 && haveR == 1) rmop = regop;
    if (rmop >= 0) rmo = &ops[rmop];
    if (vvop >= 0) vvn = REGNUM(ops[vvop].reg);
    int L = e->vL, b = 0, vp = 1;
    int xbit = 1, bbit = 1;
    if (rmo && rmo->kind == OK_REG) {
        rmn = REGNUM(rmo->reg);
        bbit = !((rmn >> 3) & 1);
        xbit = !((rmn >> 4) & 1);
    } else if (rmo) {
        if (rmo->base) bbit = !((REGNUM(rmo->base) >> 3) & 1);
        if (rmo->index) {
            int in = REGNUM(rmo->index);
            if (REGCLS(rmo->index) >= RC_XMM) {
                xbit = !((in >> 3) & 1);
                vp = !((in >> 4) & 1);
            } else xbit = !((in >> 3) & 1);
        }
    }
    if (rc) {
        if (!rmo || rmo->kind != OK_REG) { err("invalid decorator on a memory operand"); return 0; }
        b = 1;
        L = rc == 5 ? 0 : rc - 1;
    }
    if (bc) b = 1;
    if (vvop >= 0 && vvop != regop && REGCLS(ops[vvop].reg) >= RC_XMM) vp = !((vvn >> 4) & 1);
    int nv = (~vvn) & 15;
    int pp = e->vpp, W = e->vW == 1;
    if (e->vkind == VK_EVEX) {
        bad1(pre, 0x62);
        bad1(pre, (u8)((!((regn >> 3) & 1) << 7) | (xbit << 6) | (bbit << 5) | (!((regn >> 4) & 1) << 4) | e->vmap));
        bad1(pre, (u8)((W << 7) | (nv << 3) | 4 | pp));
        bad1(pre, (u8)(((zero ? 1 : 0) << 7) | (L << 5) | (b << 4) | (vp << 3) | kreg));
    } else if (e->vkind == VK_XOP) {
        bad1(pre, 0x8F);
        bad1(pre, (u8)((!((regn >> 3) & 1) << 7) | (xbit << 6) | (bbit << 5) | e->vmap));
        bad1(pre, (u8)((W << 7) | (nv << 3) | (L << 2) | pp));
    } else {
        if (e->vmap == 1 && !W && ic->vexpfx != 3) {
            bad1(pre, 0xC5);
            bad1(pre, (u8)((!((regn >> 3) & 1) << 7) | (nv << 3) | (L << 2) | pp));
        } else {
            if (ic->vexpfx == 2) { err("instruction not encodable with {vex2} prefix"); return 0; }
            bad1(pre, 0xC4);
            bad1(pre, (u8)((!((regn >> 3) & 1) << 7) | (xbit << 6) | (bbit << 5) | e->vmap));
            bad1(pre, (u8)((W << 7) | (nv << 3) | (L << 2) | pp));
        }
    }
    for (int i = 0; i < nopc; i++) bad1(&bf->b, (u8)opc[i]);
    if (haveR) {
        if (!rmo) { err("internal: no rm operand for %s", e->mn); return 0; }
        if (rmo->kind == OK_REG) bad1(&bf->b, (u8)(0xC0 | ((regn & 7) << 3) | (rmn & 7)));
        else {
            int asz;
            int d8n = e->vkind == VK_EVEX ? tuple_n(e, rmop, rmo->bcst != 0) : 0;
            if (!emit_ea(rmo, regn & 7, ic, bf, &asz, d8n)) return 0;
            if (asz != ic->bits) *need67 = 1;
            if (rmo->mseg) *memseg = rmo->mseg;
        }
    }
    (void)mx;
    for (int i = 0; i < npost; i++) bad1(&bf->b, (u8)post[i]);
    if (is4) {
        if (sop < 0) { err("internal: no register operand for %s", e->mn); return 0; }
        bad1(&bf->b, (u8)(REGNUM(ops[sop].reg) << 4));
    }
    if (hasib) {
        if (immop < 0) { err("internal: no immediate operand for %s", e->mn); return 0; }
        put_val(bf, &ops[immop].val, hasib, RK_ABS, ic);
    }
    return 1;
}

static int emit_entry(const Ent *e, Op *ops, int nops, IC *ic, InsnOut *out)
{
    Buf bf;
    memset(&bf, 0, sizeof bf);
    int bits = ic->bits;
    int obits = bits == 64 ? 32 : bits;
    size_t rexpos = (size_t)-1;
    int need66 = 0, need67 = 0, memseg = 0;
    int plusr = -1, plusi = -1, regop = -1, rmop = -1;
    int hasplusr = strstr(e->enc, "+r") != NULL;
    int immidx[4], nimm = 0, imi = 0;
    int relop = -1, farop = -1, momop = -1;
    for (int i = 0; i < nops; i++) {
        int s = e->sp[i];
        if (hasplusr && plusr < 0 && (s == S_R8 || s == S_R16 || s == S_R32 || s == S_R64)) { plusr = i; continue; }
        if (s == S_STI && plusi < 0) plusi = i;
        if (is_reg_role(s) && regop < 0) regop = i;
        else if (is_rm_role(s) && rmop < 0) rmop = i;
        if (is_imm_spec(s) && nimm < 4) immidx[nimm++] = i;
        if (s == S_REL8 || s == S_REL) relop = i;
        if (s == S_FARPTR) farop = i;
        if (s == S_MO8 || s == S_MO16 || s == S_MO32 || s == S_MO64) momop = i;
        if (s == S_MFAR) {
            if (ops[i].size == 2) need66 = obits == 32;
            else if (ops[i].size == 4) need66 = obits == 16;
            else if (bits == 64 && (ops[i].size == 0 || ops[i].size == 8)) bf.rw = 1;
        }
    }
    int relfield = -1, relsize = 0;
    int osz16 = 0, fwait = 0, tokn = 0;
    char *enc = xstrdup(e->enc);
    char *save = enc;
    Bytes vpre;
    memset(&vpre, 0, sizeof vpre);
    if (e->vkind) {
        if (!vec_body(e, ops, nops, ic, &bf, &vpre, &need67, &memseg)) { free(enc); return 0; }
        bf.rw = bf.rr = bf.rx = bf.rb = bf.needrex = 0;
        save = enc + strlen(enc);
    }
    for (char *t = nexttok(&save); t; t = nexttok(&save)) {
        if (tokn++ == 0 && !strcmp(t, "9b")) { fwait = 1; continue; }
        if (rexpos == (size_t)-1 && strcmp(t, "66") && strcmp(t, "f2") && strcmp(t, "f3") && strcmp(t, "o16") && strcmp(t, "o32") && strcmp(t, "o64") && strcmp(t, "o64nw") && strcmp(t, "a16") && strcmp(t, "a32") && strcmp(t, "a64") && strcmp(t, "only64") && strcmp(t, "no64") && strcmp(t, "norexw") && strcmp(t, "rex.l")) rexpos = bf.b.n;
        if (!strcmp(t, "only64") || !strcmp(t, "no64") || !strcmp(t, "norexw") || !strcmp(t, "rex.l") || !strcmp(t, "o64nw")) continue;
        if (!strcmp(t, "o16")) { osz16 = 16; if (obits == 32) need66 = 1; continue; }
        if (!strcmp(t, "o32")) { osz16 = 32; if (obits == 16) need66 = 1; continue; }
        if (!strcmp(t, "o64")) { bf.rw = 1; continue; }
        if (!strcmp(t, "a16")) { if (bits != 16) need67 = 1; continue; }
        if (!strcmp(t, "a32")) { if (bits != 32) need67 = 1; continue; }
        if (!strcmp(t, "a64")) { continue; }
        if (t[0] == '/') {
            int regf;
            int rm = rmop;
            if (t[1] == 'r') {
                if (regop < 0) { err("internal: no reg operand for %s", e->mn); free(enc); return 0; }
                const Op *ro = &ops[regop];
                regf = rfield(&bf, ro->reg, &bf.rr);
                if (rm < 0) rm = regop;
            } else regf = t[1] - '0';
            if (rm < 0) { err("internal: no rm operand for %s", e->mn); free(enc); return 0; }
            const Op *mo = &ops[rm];
            if (mo->kind == OK_REG) {
                int rmn = rfield(&bf, mo->reg, &bf.rb);
                bad1(&bf.b, (u8)(0xC0 | (regf << 3) | rmn));
            } else {
                int asz;
                if (!emit_ea(mo, regf, ic, &bf, &asz, 0)) { free(enc); return 0; }
                if (asz != bits) need67 = 1;
                if (mo->mseg) memseg = mo->mseg;
            }
            continue;
        }
        if (!strcmp(t, "ib") || !strcmp(t, "iw") || !strcmp(t, "id") || !strcmp(t, "iq")) {
            if (imi >= nimm) { err("internal: missing imm operand for %s", e->mn); free(enc); return 0; }
            Op *io = &ops[immidx[imi++]];
            int sz = t[1] == 'b' ? 1 : (t[1] == 'w' ? 2 : (t[1] == 'q' ? 8 : 4));
            if (val_isconst(&io->val) && io->val.n >= -128 && io->val.n <= 255 && sz == 1) {}
            else if (sz == 1 && val_isconst(&io->val)) warn("byte data exceeds bounds");
            put_val(&bf, &io->val, sz, RK_ABS, ic);
            continue;
        }
        if (!strcmp(t, "cb") || !strcmp(t, "cw") || !strcmp(t, "cd") || !strcmp(t, "cz")) {
            relsize = t[1] == 'b' ? 1 : (t[1] == 'w' ? 2 : (t[1] == 'd' ? 4 : (bits == 16 ? 2 : 4)));
            relfield = (int)bf.b.n;
            badle(&bf.b, 0, relsize);
            continue;
        }
        if (!strcmp(t, "om")) {
            const Op *mo = &ops[momop];
            int asz = ic->a16 ? 16 : (ic->a32 ? 32 : (mo->asize ? mo->asize : bits));
            if (asz != bits) need67 = 1;
            put_val(&bf, &mo->val, asz == 64 ? 8 : (asz == 32 ? 4 : 2), RK_ABS, ic);
            if (mo->mseg) memseg = mo->mseg;
            continue;
        }
        if (!strcmp(t, "fp")) {
            const Op *fo = &ops[farop];
            int osz = fo->size == 2 ? 16 : (fo->size == 4 ? 32 : obits);
            if (osz != obits) need66 = 1;
            put_val(&bf, &fo->val, osz == 32 ? 4 : 2, RK_ABS, ic);
            put_val(&bf, &fo->seg, 2, RK_ABS, ic);
            continue;
        }
        if (t[0] == '=') {
            bad1(&bf.b, (u8)strtol(t + 1, NULL, 16));
            continue;
        }
        char *plus = strchr(t, '+');
        char hx[3] = { t[0], t[1], 0 };
        if (isxdigit((unsigned char)t[0]) && isxdigit((unsigned char)t[1])) {
            u8 v = (u8)strtol(hx, NULL, 16);
            if (plus && plus[1] == 'r') {
                if (plusr < 0) { err("internal: no +r operand for %s", e->mn); free(enc); return 0; }
                v = (u8)(v | rfield(&bf, ops[plusr].reg, &bf.rb));
            } else if (plus && plus[1] == 'i') {
                if (plusi < 0) { err("internal: no +i operand for %s", e->mn); free(enc); return 0; }
                v = (u8)(v | REGNUM(ops[plusi].reg));
            }
            bad1(&bf.b, v);
            continue;
        }
        err("internal: bad encoding token `%s'", t);
        free(enc);
        return 0;
    }
    free(enc);
    if (ic->o16 && obits == 32) need66 = 1;
    if (ic->o32 && obits == 16) need66 = 1;
    if (ic->o16 && obits == 16 && osz16 == 32) need66 = 0;
    if (ic->a16 && bits == 32) need67 = 1;
    if (ic->a32 && bits != 32) need67 = 1;
    if (ic->segpre) memseg = ic->segpre;
    if (bf.rw || bf.rr || bf.rx || bf.rb || bf.needrex) {
        if (bf.high8) { err("cannot use high byte register in rex instruction"); free(bf.b.p); return 0; }
        if (bits != 64) { err("invalid operands in non-64-bit mode"); free(bf.b.p); return 0; }
        u8 rx = (u8)(0x40 | (bf.rw << 3) | (bf.rr << 2) | (bf.rx << 1) | bf.rb);
        size_t at = rexpos == (size_t)-1 ? 0 : rexpos;
        if (at > bf.b.n) at = bf.b.n;
        bad1(&bf.b, 0);
        memmove(bf.b.p + at + 1, bf.b.p + at, bf.b.n - at - 1);
        bf.b.p[at] = rx;
        for (int i = 0; i < bf.nr; i++) if ((size_t)bf.r[i].off >= at) bf.r[i].off++;
        if (relfield >= 0 && (size_t)relfield >= at) relfield++;
    }
    Bytes *code = &out->code;
    size_t start = code->n;
    if (ic->bnd && relsize == 1 && e->enc[0] != '7') { err("bnd prefix is not allowed"); return 0; }
    if (fwait) bad1(code, 0x9B);
    if (ic->rep || ic->xrel) bad1(code, 0xF3);
    else if (ic->repne || ic->xacq || ic->bnd) bad1(code, 0xF2);
    if (ic->lock) bad1(code, 0xF0);
    if (memseg) bad1(code, (u8)seg_prefix_byte(memseg));
    if (need66 && !e->vkind) bad1(code, 0x66);
    if (need67) bad1(code, 0x67);
    if (vpre.n) { badd(code, vpre.p, vpre.n); free(vpre.p); }
    size_t plen = code->n - start;
    badd(code, bf.b.p, bf.b.n);
    free(bf.b.p);
    size_t total = code->n - start;
    for (int i = 0; i < bf.nr; i++) {
        Reloc r = bf.r[i];
        r.off += (i64)(start + plen);
        if (r.kind == RK_REL && r.relbase == -1) r.relbase = (i64)(code->n - start);
        if (out->nrel >= out->caprel) {
            out->caprel = out->caprel ? out->caprel * 2 : 8;
            out->relocs = xrealloc(out->relocs, sizeof(Reloc) * (size_t)out->caprel);
        }
        out->relocs[out->nrel++] = r;
    }
    if (relfield >= 0) {
        const Val *v = &ops[relop].val;
        size_t foff = start + plen + (size_t)relfield;
        i64 end = ic->start_off + (i64)(code->n - start);
        (void)total;
        if (v->unk) {
        } else if (v->pos == ic->cur_sec && v->neg < 0 && !v->ext && !v->hasreg) {
            i64 d = v->n - end;
            if (relsize == 1 && (d < -128 || d > 127)) { err("short jump is out of range"); return 0; }
            if (relsize == 2 && (d < -32768 || d > 65535)) { err("relative displacement out of range"); return 0; }
            bputle(code, foff, (u64)d, relsize);
        } else {
            if (out->nrel >= out->caprel) {
                out->caprel = out->caprel ? out->caprel * 2 : 8;
                out->relocs = xrealloc(out->relocs, sizeof(Reloc) * (size_t)out->caprel);
            }
            Reloc r;
            memset(&r, 0, sizeof r);
            r.off = (i64)foff;
            r.size = relsize;
            r.kind = RK_REL;
            r.relbase = (i64)(code->n - start);
            r.v = *v;
            out->relocs[out->nrel++] = r;
        }
    }
    return 1;
}

static int expand_star(const Ent *e, const Op *ops, int nops, Op *tmp)
{
    int j = -1;
    for (int i = 0; i < e->nops; i++) if (e->dc[i] & DC_STAR) { j = i; break; }
    if (j < 0 || nops < 1 || j < 1) return 0;
    for (int i = 0, k = 0; i < e->nops; i++) {
        if (i == j) {
            tmp[i] = ops[0];
            tmp[i].kreg = tmp[i].zero = tmp[i].bcst = tmp[i].rc = 0;
        } else tmp[i] = ops[k++];
    }
    return 1;
}

static const Ent *choose(const Ent **c, int n, const Op *ops, int nops, IC *ic)
{
    if (n == 1) return c[0];
    int unsized = 0;
    for (int i = 0; i < nops; i++) if (ops[i].kind == OK_MEM && ops[i].size == 0) unsized = 1;
    const Ent *k[64];
    for (int i = 0; i < n; i++) if (!c[i]->vkind) {
        int m = 0;
        for (int j = 0; j < n; j++) if (!c[j]->vkind) k[m++] = c[j];
        c = k;
        n = m;
        if (n == 1) return c[0];
        break;
    }
    int ob = ic->bits == 64 ? 32 : ic->bits;
    int defsz = ob == 32 ? 4 : 2;
    for (int i = 0; i < n; i++) {
        int oz = c[i]->osz == 0 || c[i]->osz == ob;
        int mz = !unsized || c[i]->vkind || c[i]->msize == 0 || c[i]->msize == defsz || (ic->bits == 64 && c[i]->msize == 8);
        if (oz && mz) return c[i];
    }
    for (int i = 0; i < n; i++)
        if (c[i]->osz == 0 || c[i]->osz == ob) return c[i];
    return c[0];
}

int insn_encode(const char *mn, Op *ops, int nops, IC *ic, InsnOut *out)
{
    char key[40];
    size_t n = strlen(mn);
    if (n >= sizeof key) { err("unknown instruction `%s'", mn); return 0; }
    for (size_t i = 0; i <= n; i++) key[i] = (char)tolower((unsigned char)mn[i]);
    cur_bits = ic->bits;
    const Ent *head = ht_get(&g_etab, key);
    if (!head) { err("unknown instruction `%s'", mn); ic->nomatch = 2; return 0; }
    Op mib[2];
    if (nops == 3 && (!strcmp(key, "bndldx") || !strcmp(key, "bndstx"))) {
        int ri = -1, mi = -1, bi = -1;
        for (int i = 0; i < 3; i++) {
            if (ops[i].kind == OK_MEM) mi = i;
            else if (ops[i].kind == OK_REG && REGCLS(ops[i].reg) == RC_BND) bi = i;
            else if (ops[i].kind == OK_REG && REGCLS(ops[i].reg) == RC_R32) ri = i;
        }
        if (ri < 0 || mi < 0 || bi < 0) { err("invalid combination of opcode and operands for `%s'", mn); return 0; }
        if (ops[mi].index) { err("`%s' memory operand already has an index register", mn); return 0; }
        Op m = ops[mi];
        m.index = ops[ri].reg;
        m.scale = 1;
        mib[mi < bi ? 0 : 1] = m;
        mib[mi < bi ? 1 : 0] = ops[bi];
        ops = mib;
        nops = 2;
    }
    const Ent *cands[64];
    int nc = 0;
    int cpu_blocked = 0, pfx_blocked = 0, late_blocked = 0;
    Op tmp[6];
    ic->why = NULL;
    for (const Ent *e = head; e; e = e->next) {
        Op *use = ops;
        int un = nops;
        if (e->nops == nops + 1 && nops < 6 && expand_star(e, ops, nops, tmp)) { use = tmp; un = nops + 1; }
        if (entry_matches(e, use, un, ic, head)) {
            if (e->level > ic->cpu) { cpu_blocked = 1; continue; }
            int ek = e->vkind == VK_EVEX ? 2 : (e->vkind == VK_VEX ? 1 : 0);
            if (ic->vexpfx) {
                if (ic->vexpfx == 4 ? ek != 2 : ek != 1) { pfx_blocked = 1; continue; }
            } else if (e->late) { late_blocked = 1; continue; }
            if (nc < 64) cands[nc++] = e;
        }
    }
    if (!nc) {
        ic->nomatch = 1;
        if (!ic->quiet) {
            if (cpu_blocked) err("instruction `%s' is not supported on the selected cpu", mn);
            else if (ic->why) err("%s", ic->why);
            else if (ic->vexpfx && !pfx_blocked) err("instruction not encodable with {%s} prefix", ic->vexpfx == 1 ? "vex" : ic->vexpfx == 2 ? "vex2" : ic->vexpfx == 3 ? "vex3" : "evex");
            else if (ic->vexpfx) err("instruction not encodable with {%s} prefix", ic->vexpfx == 1 ? "vex" : ic->vexpfx == 2 ? "vex2" : ic->vexpfx == 3 ? "vex3" : "evex");
            else if (late_blocked) err("instruction not encodable without explicit prefix");
            else {
                unsigned have = 0;
                for (const Ent *x = head; x; x = x->next) have |= 1u << x->nops;
                if (!(have & (1u << nops))) {
                    char lst[64] = "";
                    int cnt = 0;
                    for (int k = 0; k < 7; k++) if (have & (1u << k)) {
                        char t[8];
                        snprintf(t, sizeof t, "%s%d", cnt ? " or " : "", k);
                        strcat(lst, t);
                        cnt++;
                    }
                    err("`%s' takes %s operand%s, not %d", mn, lst, cnt == 1 && !strcmp(lst, "1") ? "" : "s", nops);
                } else err("invalid combination of opcode and operands for `%s'", mn);
            }
        }
        return 0;
    }
    const Ent *e = choose(cands, nc, ops, nops, ic);
    Op *use = ops;
    int un = nops;
    if (e->nops == nops + 1 && expand_star(e, ops, nops, tmp)) { use = tmp; un = nops + 1; }
    int anysf = 0;
    for (int i = 0; i < nc; i++) if (cands[i]->sflag) anysf = 1;
    if (e->vkind && nc > 1 && !anysf) {
        int unsized = 0;
        for (int i = 0; i < un; i++) if (use[i].kind == OK_MEM && use[i].size == 0 && !use[i].bcst && !(use[i].index && REGCLS(use[i].index) >= RC_XMM)) unsized = 1;
        if (unsized) {
            for (int i = 0; i < nc; i++) {
                if (!cands[i]->vkind || cands[i]->nops != e->nops) continue;
                for (int k = 0; k < un; k++)
                    if (use[k].kind == OK_MEM && spec_bytes(cands[i]->sp[k]) != spec_bytes(e->sp[k]) && spec_bytes(e->sp[k]) && spec_bytes(cands[i]->sp[k])) {
                        err("operation size not specified");
                        return 0;
                    }
            }
        }
    }
    return emit_entry(e, use, un, ic, out);
}

void insn_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    insn_tab_init();
}

void insn_cpu_set(const char *name, int *lvl)
{
    static const struct { const char *n; int l; } t[] = {
        { "8086", 0 }, { "186", 1 }, { "286", 2 }, { "386", 3 }, { "486", 4 }, { "586", 5 }, { "pentium", 5 },
        { "686", 6 }, { "ppro", 6 }, { "p2", 6 }, { "p3", 7 }, { "katmai", 7 }, { "p4", 8 }, { "willamette", 8 },
        { "prescott", 9 }, { "ia64", 99 }, { "x64", 99 }, { "default", 99 }, { "all", 99 },
        { "pentiumm", 8 }, { "pentium2", 6 }, { "pentium3", 7 }, { "pentium4", 8 }, { "core", 10 }, { "nehalem", 12 },
        { "sandybridge", 13 }, { "haswell", 14 }, { "skylake", 15 }, { "icelake", 15 }, { "sapphirerapids", 15 }, { NULL, 0 }
    };
    for (int i = 0; t[i].n; i++) if (!strcasecmp(t[i].n, name)) { *lvl = t[i].l; return; }
    warn("unknown cpu `%s'", name);
}
