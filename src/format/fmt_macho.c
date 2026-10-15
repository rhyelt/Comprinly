#include "cly.h"

#define MB 0x1000
#define MHDR 0x200

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }

static i64 rup(i64 v, i64 a) { return (v + a - 1) / a * a; }

static void segname(Bytes *b, const char *n)
{
    char buf[16];
    memset(buf, 0, 16);
    strncpy(buf, n, 15);
    badd(b, buf, 16);
}

static void section(Bytes *b, const char *sect, const char *seg, u32 addr, u32 size, u32 off, u32 align, u32 flags)
{
    segname(b, sect);
    segname(b, seg);
    put32(b, addr);
    put32(b, size);
    put32(b, off);
    put32(b, align);
    put32(b, 0);
    put32(b, 0);
    put32(b, flags);
    put32(b, 0);
    put32(b, 0);
}

#define MB64 0x100000000LL
#define MHDR64 0x400

static void put64(Bytes *b, u64 v) { badle(b, v, 8); }

static void section64(Bytes *b, const char *sect, const char *seg, u64 addr, u64 size, u32 off, u32 align, u32 flags)
{
    segname(b, sect);
    segname(b, seg);
    put64(b, addr);
    put64(b, size);
    put32(b, off);
    put32(b, align);
    put32(b, 0);
    put32(b, 0);
    put32(b, flags);
    put32(b, 0);
    put32(b, 0);
    put32(b, 0);
}

static int fmt_macho64(Obj *o, Bytes *out)
{
    link_layout(o, MB64 + MHDR64, 0x1000, 1);
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
    Bytes img = {0};
    link_image(o, &img);
    int rw = g_lay.has_rw;
    i64 text_end = g_lay.a_end;
    i64 text_vm = rup(text_end - MB64, MB);
    i64 data_fsz = g_lay.file_end - g_lay.rw_start;
    if (data_fsz < 0) data_fsz = 0;
    i64 data_fsz_r = rup(data_fsz, MB);
    i64 data_vm = rup(g_lay.end - g_lay.rw_start, MB);
    int ncmds = rw ? 4 : 3;
    u32 cmdsize = 72 + (72 + 80) + (rw ? 72 + 80 * 2 : 0) + 184;
    Bytes f = {0};
    put32(&f, 0xFEEDFACF);
    put32(&f, 0x01000007);
    put32(&f, 3);
    put32(&f, 2);
    put32(&f, (u32)ncmds);
    put32(&f, cmdsize);
    put32(&f, 1);
    put32(&f, 0);
    put32(&f, 0x19); put32(&f, 72); segname(&f, "__PAGEZERO"); put64(&f, 0); put64(&f, MB64); put64(&f, 0); put64(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0);
    put32(&f, 0x19); put32(&f, 72 + 80); segname(&f, "__TEXT"); put64(&f, MB64); put64(&f, (u64)text_vm); put64(&f, 0); put64(&f, (u64)text_vm); put32(&f, 7); put32(&f, 5); put32(&f, 1); put32(&f, 0);
    section64(&f, "__text", "__TEXT", (u64)g_lay.base, (u64)(text_end - g_lay.base), (u32)(g_lay.base - MB64), 4, 0x80000400);
    if (rw) {
        put32(&f, 0x19); put32(&f, 72 + 80 * 2); segname(&f, "__DATA"); put64(&f, (u64)g_lay.rw_start); put64(&f, (u64)data_vm);
        put64(&f, (u64)(g_lay.rw_start - MB64)); put64(&f, (u64)data_fsz_r); put32(&f, 7); put32(&f, 3); put32(&f, 2); put32(&f, 0);
        section64(&f, "__data", "__DATA", (u64)g_lay.rw_start, (u64)data_fsz, (u32)(g_lay.rw_start - MB64), 4, 0);
        section64(&f, "__bss", "__DATA", (u64)(g_lay.rw_start + data_fsz), (u64)(g_lay.end - g_lay.rw_start - data_fsz), 0, 4, 1);
    }
    put32(&f, 5); put32(&f, 184); put32(&f, 4); put32(&f, 42);
    for (int i = 0; i < 21; i++) put64(&f, i == 16 ? (u64)entry : 0);
    while (f.n < MHDR64) bad1(&f, 0);
    badd(&f, img.p, g_lay.a_end > g_lay.base ? (size_t)(g_lay.a_end - g_lay.base) : 0);
    if (rw) {
        while ((i64)f.n < g_lay.rw_start - MB64) bad1(&f, 0);
        if (data_fsz > 0) badd(&f, img.p + (g_lay.rw_start - g_lay.base), (size_t)data_fsz);
        while ((i64)f.n < g_lay.rw_start - MB64 + data_fsz_r) bad1(&f, 0);
    } else {
        while ((i64)f.n < text_vm) bad1(&f, 0);
    }
    badd(out, f.p, f.n);
    return 1;
}

int fmt_macho(Obj *o, Bytes *out)
{
    if (o->is64) return fmt_macho64(o, out);
    link_layout(o, MB + MHDR, MB, 1);
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
    Bytes img = {0};
    link_image(o, &img);
    int rw = g_lay.has_rw;
    i64 text_end = g_lay.a_end;
    i64 text_vm = rup(text_end - MB, MB);
    i64 data_fsz = g_lay.file_end - g_lay.rw_start;
    if (data_fsz < 0) data_fsz = 0;
    i64 data_fsz_r = rup(data_fsz, MB);
    i64 data_vm = rup(g_lay.end - g_lay.rw_start, MB);
    int ncmds = rw ? 4 : 3;
    u32 cmdsize = 56 + (56 + 68) + (rw ? 56 + 68 * 2 : 0) + 80;
    Bytes f = {0};
    put32(&f, 0xFEEDFACE);
    put32(&f, 7);
    put32(&f, 3);
    put32(&f, 2);
    put32(&f, (u32)ncmds);
    put32(&f, cmdsize);
    put32(&f, 1);
    put32(&f, 1); put32(&f, 56); segname(&f, "__PAGEZERO"); put32(&f, 0); put32(&f, MB); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0);
    put32(&f, 1); put32(&f, 56 + 68); segname(&f, "__TEXT"); put32(&f, MB); put32(&f, (u32)text_vm); put32(&f, 0); put32(&f, (u32)text_vm); put32(&f, 7); put32(&f, 5); put32(&f, 1); put32(&f, 0);
    section(&f, "__text", "__TEXT", (u32)g_lay.base, (u32)(text_end - g_lay.base), (u32)(g_lay.base - MB), 4, 0x80000400);
    if (rw) {
        put32(&f, 1); put32(&f, 56 + 68 * 2); segname(&f, "__DATA"); put32(&f, (u32)g_lay.rw_start); put32(&f, (u32)data_vm);
        put32(&f, (u32)(g_lay.rw_start - MB)); put32(&f, (u32)data_fsz_r); put32(&f, 7); put32(&f, 3); put32(&f, 2); put32(&f, 0);
        section(&f, "__data", "__DATA", (u32)g_lay.rw_start, (u32)data_fsz, (u32)(g_lay.rw_start - MB), 4, 0);
        section(&f, "__bss", "__DATA", (u32)(g_lay.rw_start + data_fsz), (u32)(g_lay.end - g_lay.rw_start - data_fsz), 0, 4, 1);
    }
    put32(&f, 5); put32(&f, 80); put32(&f, 1); put32(&f, 16);
    for (int i = 0; i < 16; i++) put32(&f, i == 10 ? (u32)entry : 0);
    while (f.n < MHDR) bad1(&f, 0);
    badd(&f, img.p, g_lay.a_end > g_lay.base ? (size_t)(g_lay.a_end - g_lay.base) : 0);
    if (rw) {
        while ((i64)f.n < g_lay.rw_start - MB) bad1(&f, 0);
        if (data_fsz > 0) badd(&f, img.p + (g_lay.rw_start - g_lay.base), (size_t)data_fsz);
        while ((i64)f.n < g_lay.rw_start - MB + data_fsz_r) bad1(&f, 0);
    } else {
        while ((i64)f.n < MB + text_vm - MB) bad1(&f, 0);
    }
    badd(out, f.p, f.n);
    return 1;
}
