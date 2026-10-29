/* Relocation patching for ARM64 and RISC-V instruction encodings. */
#include "cly.h"

/* Reads a 32-bit little endian word from section data. */
static int rd32(Sec *s, i64 off, u32 *v)
{
    if (off < 0 || (size_t)(off + 4) > s->data.n) return 0;
    *v = (u32)s->data.p[off] | ((u32)s->data.p[off + 1] << 8) | ((u32)s->data.p[off + 2] << 16) | ((u32)s->data.p[off + 3] << 24);
    return 1;
}

/* Writes a 32-bit little endian word. */
static void wr32(Sec *s, i64 off, u32 v)
{
    for (int i = 0; i < 4; i++) s->data.p[off + i] = (u8)(v >> (8 * i));
}

/* True if v fits a signed field of the given width. */
static int fits(i64 v, int bits)
{
    i64 lim = (i64)1 << (bits - 1);
    return v >= -lim && v < lim;
}

/* Applies one ARM64 relocation (branch, adrp, page offset, movz/movk, literal ...). */
static int a64_apply(Obj *o, Reloc *r, i64 S, i64 P)
{
    Sec *s = o->secs[r->sec];
    u32 w;
    if (!rd32(s, r->off, &w)) return 1;
    i64 d = S - P;
    switch (r->kind) {
    case RK_A64_B26:
        if (d & 3) { err("branch target is not 4-byte aligned"); return 0; }
        if (!fits(d >> 2, 26)) { err("branch target is out of range (+-128MB)"); return 0; }
        w = (w & 0xfc000000u) | (u32)((d >> 2) & 0x3ffffff);
        break;
    case RK_A64_B19:
        if (d & 3) { err("branch target is not 4-byte aligned"); return 0; }
        if (!fits(d >> 2, 19)) { err("target is out of range (+-1MB)"); return 0; }
        w = (w & ~(0x7ffffu << 5)) | ((u32)((d >> 2) & 0x7ffff) << 5);
        break;
    case RK_A64_B14:
        if (d & 3) { err("branch target is not 4-byte aligned"); return 0; }
        if (!fits(d >> 2, 14)) { err("test-and-branch target is out of range (+-32KB)"); return 0; }
        w = (w & ~(0x3fffu << 5)) | ((u32)((d >> 2) & 0x3fff) << 5);
        break;
    case RK_A64_ADR:
        if (!fits(d, 21)) { err("adr target is out of range (+-1MB); use adrp"); return 0; }
        w = (w & ~((3u << 29) | (0x7ffffu << 5))) | ((u32)(d & 3) << 29) | ((u32)((d >> 2) & 0x7ffff) << 5);
        break;
    case RK_A64_ADRP: {
        if (r->v.wk == 7) { err("GOT references (:got:) need an object file or a shared library output"); return 0; }
        i64 pg = (S >> 12) - (P >> 12);
        if (!fits(pg, 21)) { err("adrp target is out of range (+-4GB)"); return 0; }
        w = (w & ~((3u << 29) | (0x7ffffu << 5))) | ((u32)(pg & 3) << 29) | ((u32)((pg >> 2) & 0x7ffff) << 5);
        break;
    }
    case RK_A64_LO12: {
        if (r->v.wk == 7) { err("GOT references (:got_lo12:) need an object file or a shared library output"); return 0; }
        int sh = r->size;
        if (sh && (S & (((i64)1 << sh) - 1))) { err("address is not aligned to %d bytes for this access", 1 << sh); return 0; }
        w = (w & ~(0xfffu << 10)) | ((u32)((S & 0xfff) >> sh) << 10);
        break;
    }
    case RK_A64_MOVW: {
        int g = r->size & 3, nc = r->size >> 2;
        if (!nc && g < 3 && (S < 0 || (S >> (16 * (g + 1))) != 0)) { err("value does not fit in this 16-bit group (use :abs_g%d_nc:)", g); return 0; }
        w = (w & ~(0xffffu << 5)) | ((u32)((S >> (16 * g)) & 0xffff) << 5);
        break;
    }
    default:
        return -1;
    }
    wr32(s, r->off, w);
    return 1;
}

/* Patches the 12-bit immediate of an I-type RISC-V instruction. */
static void rv_patch_i(Sec *s, i64 off, i64 lo)
{
    u32 w;
    rd32(s, off, &w);
    w = (w & 0x000fffffu) | (((u32)lo & 0xfff) << 20);
    wr32(s, off, w);
}

/* Patches the split 12-bit immediate of an S-type instruction. */
static void rv_patch_s(Sec *s, i64 off, i64 lo)
{
    u32 w;
    rd32(s, off, &w);
    w = (w & 0x01fff07fu) | (((u32)lo & 0x1f) << 7) | ((((u32)lo >> 5) & 0x7f) << 25);
    wr32(s, off, w);
}

/* Patches the 20-bit immediate of a U-type instruction. */
static void rv_patch_u(Sec *s, i64 off, i64 hi)
{
    u32 w;
    rd32(s, off, &w);
    w = (w & 0xfffu) | (((u32)hi & 0xfffff) << 12);
    wr32(s, off, w);
}

/* Sign extends the low 12 bits. */
static i64 sx12(i64 v)
{
    return ((v & 0xfff) ^ 0x800) - 0x800;
}

/* Applies one RISC-V relocation (jal, branches, auipc pairs, compressed forms ...). */
static int rv_apply(Obj *o, Reloc *r, i64 S, i64 P)
{
    Sec *s = o->secs[r->sec];
    u32 w;
    if (!rd32(s, r->off, &w)) return 1;
    i64 d = S - P;
    switch (r->kind) {
    case RK_RV_JAL: {
        if (d & 1) { err("jump target is not 2-byte aligned"); return 0; }
        if (!fits(d, 21)) { err("jump target is out of range (+-1MB); use call or tail for far targets"); return 0; }
        u32 o2 = (u32)d;
        w = (w & 0xfffu) | (((o2 >> 12) & 0xff) << 12) | (((o2 >> 11) & 1) << 20) | (((o2 >> 1) & 0x3ff) << 21) | (((o2 >> 20) & 1) << 31);
        wr32(s, r->off, w);
        return 1;
    }
    case RK_RV_BRANCH: {
        if (d & 1) { err("branch target is not 2-byte aligned"); return 0; }
        if (!fits(d, 13)) { err("branch target is out of range (+-4KB)"); return 0; }
        u32 o2 = (u32)d;
        w = (w & 0x01fff07fu) | (((o2 >> 11) & 1) << 7) | (((o2 >> 1) & 15) << 8) | (((o2 >> 5) & 0x3f) << 25) | (((o2 >> 12) & 1) << 31);
        wr32(s, r->off, w);
        return 1;
    }
    case RK_RV_HI20:
        rv_patch_u(s, r->off, (S + 0x800) >> 12);
        return 1;
    case RK_RV_LO12I:
        rv_patch_i(s, r->off, sx12(S));
        return 1;
    case RK_RV_LO12S:
        rv_patch_s(s, r->off, sx12(S));
        return 1;
    case RK_RV_PCHI20:
        if (r->v.wk == 7) { err("%%got_pcrel_hi needs an object file or a shared library output"); return 0; }
        if (!fits((d + 0x800) >> 12, 20)) { err("pc-relative address is out of range (+-2GB)"); return 0; }
        rv_patch_u(s, r->off, (d + 0x800) >> 12);
        return 1;
    case RK_RV_PCLO12I:
    case RK_RV_PCLO12S: {
        Reloc *hi = NULL;
        for (int i = 0; i < o->nrelocs; i++) {
            Reloc *q = &o->relocs[i];
            if (q->kind == RK_RV_PCHI20 && sec_addr(o, q->sec) + q->off == S) { hi = q; break; }
        }
        if (!hi) { err("%%pcrel_lo needs a label on an auipc with a matching %%pcrel_hi"); return 0; }
        int ok = 1;
        i64 T = val_addr(o, &hi->v, &ok);
        i64 lo = sx12(T - S);
        if (r->kind == RK_RV_PCLO12I) rv_patch_i(s, r->off, lo);
        else rv_patch_s(s, r->off, lo);
        return 1;
    }
    case RK_RV_CALL: {
        if (!fits((d + 0x800) >> 12, 20)) { err("call target is out of range (+-2GB)"); return 0; }
        rv_patch_u(s, r->off, (d + 0x800) >> 12);
        rv_patch_i(s, r->off + 4, sx12(d));
        return 1;
    }
    default:
        return -1;
    }
}

/* Entry for ARM64 and RISC-V relocations. Sets *handled when the kind belongs to those CPUs. */
int link_arch_reloc(Obj *o, Reloc *r, i64 S, i64 P, int *handled)
{
    *handled = 0;
    if (r->kind >= RK_RV_JAL && r->kind <= RK_RV_CB) {
        *handled = 1;
        g_pos.file = "<link>";
        g_pos.line = r->line;
        return rv_apply(o, r, S, P);
    }
    if (r->kind >= RK_A64_B26 && r->kind <= RK_A64_MOVW) {
        *handled = 1;
        g_pos.file = "<link>";
        g_pos.line = r->line;
        return a64_apply(o, r, S, P);
    }
    return 1;
}
