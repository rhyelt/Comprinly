/* Writer for ELF executables (x86, x86-64, ARM64, RISC-V). With the optimizer on, x86 files use a compact header layout. */
#include "cly.h"

#define ELF_BASE 0x08048000
#define ELF_HDR 148

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }
static void put16(Bytes *b, u16 v) { badle(b, v, 2); }

/* Adds a string to a string table and returns its offset. */
static int add_str(Bytes *tab, const char *s)
{
    int off = (int)tab->n;
    badd(tab, s, strlen(s) + 1);
    return off;
}

static void put64(Bytes *b, u64 v) { badle(b, v, 8); }

#define ELF64_BASE 0x400000
#define ELF64_HDR 232

/* ELF machine number for the CPU. */
static int elf_machine(Obj *o)
{
    if (o->arch == ARCH_A64) return 183;
    if (o->arch == ARCH_RV64 || o->arch == ARCH_RV32) return 243;
    return o->is64 ? 62 : 3;
}

/* ELF flags field (RISC-V compressed and float ABI bits). */
static u32 elf_flags(Obj *o)
{
    if (o->arch != ARCH_RV64 && o->arch != ARCH_RV32) return 0;
    u32 f = 0;
    if (o->rvext & RVX_C) f |= 1;
    if (o->rvext & RVX_D) f |= 4;
    else if (o->rvext & RVX_F) f |= 2;
    return f;
}


/* Smallest x86-64 program file (-strip): the program header sits inside the ELF header from byte 24. The entry address is the header's own p_type and p_flags, so it points at byte 1 of the file, where a jump to the real entry is stored. The file is 80 bytes plus the program. Returns -1 when the program does not fit this layout (it has bss, or an address that needs more than 32 bits). */
static int tiny64_run(Obj *o, Bytes *out);

static int tiny64(Obj *o, Bytes *out)
{
    int r = tiny64_run(o, out);
    for (int i = 0; i < o->nsecs; i++) {
        Sec *s = o->secs[i];
        if (s->conv) { s->conv = 0; s->nobits = 1; s->data.n = 0; }
    }
    return r;
}

static int tiny64_run(Obj *o, Bytes *out)
{
    int anyw = 0;
    i64 bss = 0;
    for (int i = 0; i < o->nsecs; i++) if (o->secs[i]->alloc && o->secs[i]->nobits) bss += o->secs[i]->size;
    if (bss > 96) return -1;
    for (int i = 0; i < o->nsecs; i++) {
        Sec *s = o->secs[i];
        if (s->alloc && s->nobits && s->size) { s->nobits = 0; bfill(&s->data, 0, (size_t)s->size - s->data.n); s->conv = 1; }
    }
    for (int i = 0; i < o->nsecs; i++) if (!g_ro_prog && o->secs[i]->alloc && o->secs[i]->write && (o->secs[i]->size || o->secs[i]->data.n)) anyw = 1;
    u32 H = anyw ? 7 : 5;
    i64 page = (i64)H << 32;
    g_compact = 1;
    link_layout(o, page + 80, 0, 0);
    if (g_lay.has_bss || g_lay.end != g_lay.file_end || !o->entry) return -1;
    if (!link_fits(o)) return -1;
    if (!link_apply(o)) return 0;
    i64 eo = link_entry(o) - page;
    Bytes img = {0};
    link_range(o, &img, g_lay.base, g_lay.file_end);
    size_t total = 80 + img.n;
    u8 h[80];
    memset(h, 0, sizeof h);
    h[0] = 0x7F; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
    i64 d8 = eo - 6;
    if (d8 >= -128 && d8 <= 127) { h[4] = 0xEB; h[5] = (u8)d8; }
    else { h[4] = 0xE9; u32 d32 = (u32)(eo - 9); memcpy(h + 5, &d32, 4); }
    h[16] = 2; h[18] = 0x3E;
    h[24] = 1;
    memcpy(h + 28, &H, 4);
    u64 v;
    v = 24; memcpy(h + 32, &v, 8);
    v = (u64)(page + 0x18); memcpy(h + 40, &v, 8);
    h[54] = 56;
    u64 need = total > 25 ? (u64)total - 24 : 1;
    u64 fsz = 1;
    if (need > 1) fsz = 1 + 65536 * ((need - 1 + 65535) / 65536);
    memcpy(h + 56, &fsz, 8);
    memcpy(h + 64, &fsz, 8);
    badd(out, h, 80);
    badd(out, img.p, img.n);
    return 1;
}

/* Smallest i386 program file (-strip): the program header starts at byte 4 and shares its fields with the ELF header (the entry address doubles as the memory size). The file is 46 bytes plus the program, and the page is mapped read and execute by the kernel's rule for programs without a stack header. Returns -1 when the program writes to memory. */
static int tiny32(Obj *o, Bytes *out)
{
    for (int i = 0; i < o->nsecs; i++) if (o->secs[i]->alloc && (o->secs[i]->nobits || (o->secs[i]->write && !g_ro_prog)) && (o->secs[i]->size || o->secs[i]->data.n)) return -1;
    i64 V = 0x20000;
    g_compact = 1;
    for (int tries = 0; tries < 2; tries++) {
        link_layout(o, V + 46, 0, 0);
        if (g_lay.has_bss || g_lay.end != g_lay.file_end || !o->entry) return -1;
        if ((g_lay.file_end - V) > V) { V = (g_lay.file_end - V + 0xFFFF) / 0x10000 * 0x10000; if (V < 0x20000) V = 0x20000; continue; }
        break;
    }
    if (g_lay.file_end - V > V) return -1;
    if (!link_fits(o)) return -1;
    if (!link_apply(o)) return 0;
    i64 ent = link_entry(o);
    Bytes img = {0};
    link_range(o, &img, g_lay.base, g_lay.file_end);
    u8 h[46];
    memset(h, 0, sizeof h);
    h[0] = 0x7F; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
    h[4] = 1;
    u32 t;
    t = (u32)V; memcpy(h + 12, &t, 4);
    h[16] = 2; h[18] = 3;
    t = (u32)ent; memcpy(h + 20, &t, 4);
    memcpy(h + 24, &t, 4);
    h[28] = 4;
    h[42] = 32;
    h[44] = 1;
    badd(out, h, 46);
    badd(out, img.p, img.n);
    return 1;
}


/* -strip for ARM64 and RISC-V: one read, write and execute segment, one program header, no padding between sections. A small bss is written out as zeros; a larger one makes this return -1 and the usual layout is used. */
static int single_seg(Obj *o, Bytes *out, int is64)
{
    i64 bss = 0;
    for (int i = 0; i < o->nsecs; i++) if (o->secs[i]->alloc && o->secs[i]->nobits) bss += o->secs[i]->size;
    if (bss > 96) return -1;
    for (int i = 0; i < o->nsecs; i++) {
        Sec *s = o->secs[i];
        if (s->alloc && s->nobits && s->size) { s->nobits = 0; bfill(&s->data, 0, (size_t)s->size - s->data.n); s->conv = 1; }
    }
    i64 base = is64 ? ELF64_BASE : ELF_BASE;
    int hdr = is64 ? 120 : 84;
    g_compact = 1;
    g_compact_min = is64 ? 8 : 4;
    link_layout(o, base + hdr, 0, 0);
    g_compact_min = 0;
    int r = -1;
    if (!g_lay.has_bss && g_lay.end == g_lay.file_end && o->entry) {
        if (!link_apply(o)) r = 0;
        else {
            i64 entry = link_entry(o);
            Bytes img = {0};
            link_range(o, &img, g_lay.base, g_lay.file_end);
            Bytes f = {0};
            u8 ident[16] = { 0x7F, 'E', 'L', 'F', (u8)(is64 ? 2 : 1), 1, 1, 0 };
            badd(&f, ident, 16);
            put16(&f, 2);
            put16(&f, (u16)elf_machine(o));
            put32(&f, 1);
            i64 total = hdr + (i64)img.n;
            if (is64) {
                put64(&f, (u64)entry); put64(&f, 64); put64(&f, 0);
                put32(&f, elf_flags(o)); put16(&f, 64); put16(&f, 56); put16(&f, 1); put16(&f, 64); put16(&f, 0); put16(&f, 0);
                put32(&f, 1); put32(&f, 7); put64(&f, 0); put64(&f, (u64)base); put64(&f, (u64)base); put64(&f, (u64)total); put64(&f, (u64)total); put64(&f, 0x1000);
            } else {
                put32(&f, (u32)entry); put32(&f, 52); put32(&f, 0);
                put32(&f, elf_flags(o)); put16(&f, 52); put16(&f, 32); put16(&f, 1); put16(&f, 40); put16(&f, 0); put16(&f, 0);
                put32(&f, 1); put32(&f, 0); put32(&f, (u32)base); put32(&f, (u32)base); put32(&f, (u32)total); put32(&f, (u32)total); put32(&f, 7); put32(&f, 0x1000);
            }
            while ((int)f.n < hdr) bad1(&f, 0);
            badd(&f, img.p, img.n);
            badd(out, f.p, f.n);
            r = 1;
        }
    }
    for (int i = 0; i < o->nsecs; i++) {
        Sec *s = o->secs[i];
        if (s->conv) { s->conv = 0; s->nobits = 1; s->data.n = 0; }
    }
    return r;
}

/* 64-bit ELF: header, program headers for the code and data segments, section headers and the section name table. */
static int fmt_elf64_norm(Obj *o, Bytes *out);

static int fmt_elf64(Obj *o, Bytes *out)
{
    if (g_xstrip && o->arch != ARCH_X86) {
        Bytes tb = {0};
        int tr = single_seg(o, &tb, 1);
        if (tr == 0) return 0;
        if (tr == 1) {
            Bytes nb = {0};
            int r = fmt_elf64_norm(o, &nb);
            if (r && nb.n < tb.n) badd(out, nb.p, nb.n);
            else badd(out, tb.p, tb.n);
            return 1;
        }
    }
    if (g_xstrip && o->arch == ARCH_X86 && !g_ldo.dynamic) {
        Bytes tb = {0};
        int tr = tiny64(o, &tb);
        if (tr == 0) return 0;
        if (tr == 1) {
            Bytes nb = {0};
            int r = fmt_elf64_norm(o, &nb);
            if (r && nb.n < tb.n) badd(out, nb.p, nb.n);
            else badd(out, tb.p, tb.n);
            return 1;
        }
    }
    return fmt_elf64_norm(o, out);
}

static int fmt_elf64_norm(Obj *o, Bytes *out)
{
    int hdr = ELF64_HDR;
    g_compact = g_strip && o->arch == ARCH_X86;
    link_layout(o, ELF64_BASE + hdr, 0x1000, 3);
    if (g_compact && !g_lay.has_rw) {
        hdr = 64 + 56 * (g_ro_prog ? 1 : 2);
        link_layout(o, ELF64_BASE + hdr, 0x1000, 3);
    }
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
    Bytes img = {0};
    link_range(o, &img, g_lay.base, g_lay.a_end);
    int rw = g_lay.has_rw;
    int nph = (rw ? 3 : 2) - (g_ro_prog ? 1 : 0);
    Bytes f = {0};
    u8 ident[16] = { 0x7F, 'E', 'L', 'F', 2, 1, 1, 0 };
    badd(&f, ident, 16);
    put16(&f, 2);
    put16(&f, (u16)elf_machine(o));
    put32(&f, 1);
    put64(&f, (u64)entry);
    put64(&f, 64);
    size_t shoff_pos = f.n;
    put64(&f, 0);
    put32(&f, elf_flags(o));
    put16(&f, 64);
    put16(&f, 56);
    put16(&f, (u16)nph);
    put16(&f, 64);
    size_t shnum_pos = f.n;
    put16(&f, 0);
    size_t shstr_pos = f.n;
    put16(&f, 0);
    i64 a_filesz = g_lay.a_end - ELF64_BASE;
    put32(&f, 1); put32(&f, 5); put64(&f, 0); put64(&f, ELF64_BASE); put64(&f, ELF64_BASE);
    put64(&f, (u64)a_filesz); put64(&f, (u64)a_filesz); put64(&f, 0x1000);
    if (rw) {
        i64 fsz = g_lay.file_end - g_lay.rw_start;
        if (fsz < 0) fsz = 0;
        put32(&f, 1); put32(&f, 6); put64(&f, (u64)(g_lay.rw_lma - ELF64_BASE)); put64(&f, (u64)g_lay.rw_start); put64(&f, (u64)g_lay.rw_start);
        put64(&f, (u64)fsz); put64(&f, (u64)(g_lay.end - g_lay.rw_start)); put64(&f, 0x1000);
    }
    if (!g_ro_prog) { put32(&f, 0x6474E551); put32(&f, 6); put64(&f, 0); put64(&f, 0); put64(&f, 0); put64(&f, 0); put64(&f, 0); put64(&f, 16); }
    while ((int)f.n < hdr) bad1(&f, 0);
    badd(&f, img.p, img.n);
    if (rw) {
        while ((i64)f.n < g_lay.rw_lma - ELF64_BASE) bad1(&f, 0);
        link_range(o, &f, g_lay.rw_start, g_lay.file_end > g_lay.rw_start ? g_lay.file_end : g_lay.rw_start);
    }
    if (g_strip) {
        badd(out, f.p, f.n);
        return 1;
    }
    Bytes shstr = {0}, symtab = {0}, strtab = {0};
    bad1(&shstr, 0);
    bad1(&strtab, 0);
    int ns = g_lay.n;
    int *shidx = xcalloc((size_t)o->nsecs + 1, sizeof(int));
    for (int i = 0; i < ns; i++) shidx[g_lay.order[i]->idx] = i + 1;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) bfill(&symtab, 0, 24);
        for (int i = 0; i < o->nsyms; i++) {
            Sym *s = o->syms[i];
            if (!s->defined || !s->name[0]) continue;
            if (s->isequ && !s->isabs) continue;
            int isglob = s->global;
            if ((pass == 0) == isglob) continue;
            int shn;
            if (s->isequ || s->sec == SEC_ABS || s->sec < 0) shn = 0xFFF1;
            else if (!shidx[s->sec]) continue;
            else shn = shidx[s->sec];
            put32(&symtab, (u32)add_str(&strtab, s->name));
            bad1(&symtab, (u8)(isglob ? 0x10 : 0));
            bad1(&symtab, 0);
            put16(&symtab, (u16)shn);
            put64(&symtab, (u64)sym_addr(o, s));
            put64(&symtab, 0);
        }
    }
    int nlocal = 1;
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        if (!s->defined || !s->name[0]) continue;
        if (s->isequ && !s->isabs) continue;
        if (s->global) continue;
        if (!(s->isequ || s->sec == SEC_ABS || s->sec < 0) && !shidx[s->sec]) continue;
        nlocal++;
    }
    int *secname = xcalloc((size_t)ns + 1, sizeof(int));
    for (int i = 0; i < ns; i++) secname[i] = add_str(&shstr, g_lay.order[i]->name);
    int n_symtab = add_str(&shstr, ".symtab");
    int n_strtab = add_str(&shstr, ".strtab");
    int n_shstr = add_str(&shstr, ".shstrtab");
    void *dw = dwarf_exe_begin(o, &shstr);
    while (f.n & 7) bad1(&f, 0);
    size_t sym_off = f.n;
    badd(&f, symtab.p, symtab.n);
    size_t str_off = f.n;
    badd(&f, strtab.p, strtab.n);
    size_t shs_off = f.n;
    badd(&f, shstr.p, shstr.n);
    dwarf_exe_data(dw, &f);
    while (f.n & 7) bad1(&f, 0);
    size_t sh_off = f.n;
    for (int i = 0; i < 16; i++) put32(&f, 0);
    for (int i = 0; i < ns; i++) {
        Sec *s = g_lay.order[i];
        u64 flags = 2 | (s->write ? 1 : 0) | (s->exec ? 4 : 0);
        put32(&f, (u32)secname[i]);
        put32(&f, s->nobits ? 8 : 1);
        put64(&f, flags);
        put64(&f, (u64)s->base);
        put64(&f, (u64)(s->nobits || s->base < g_lay.rw_start ? s->base - ELF64_BASE : s->base - g_lay.rw_start + g_lay.rw_lma - ELF64_BASE));
        put64(&f, (u64)s->size);
        put32(&f, 0);
        put32(&f, 0);
        put64(&f, (u64)(s->align > 0 ? s->align : 1));
        put64(&f, 0);
    }
    put32(&f, (u32)n_symtab); put32(&f, 2); put64(&f, 0); put64(&f, 0);
    put64(&f, (u64)sym_off); put64(&f, (u64)symtab.n); put32(&f, (u32)(ns + 2)); put32(&f, (u32)nlocal); put64(&f, 8); put64(&f, 24);
    put32(&f, (u32)n_strtab); put32(&f, 3); put64(&f, 0); put64(&f, 0);
    put64(&f, (u64)str_off); put64(&f, (u64)strtab.n); put32(&f, 0); put32(&f, 0); put64(&f, 1); put64(&f, 0);
    put32(&f, (u32)n_shstr); put32(&f, 3); put64(&f, 0); put64(&f, 0);
    put64(&f, (u64)shs_off); put64(&f, (u64)shstr.n); put32(&f, 0); put32(&f, 0); put64(&f, 1); put64(&f, 0);
    dwarf_exe_headers(dw, &f, 1);
    bputle(&f, shoff_pos, (u64)sh_off, 8);
    bputle(&f, shnum_pos, (u64)(ns + 4 + dwarf_exe_count(dw)), 2);
    bputle(&f, shstr_pos, (u64)(ns + 3), 2);
    badd(out, f.p, f.n);
    dwarf_exe_end(dw);
    free(shidx);
    free(secname);
    return 1;
}

/* 32-bit ELF, or hands off to fmt_elf64 for 64-bit files. */
static int fmt_elf32_norm(Obj *o, Bytes *out);

int fmt_elf(Obj *o, Bytes *out)
{
    if (o->is64) return fmt_elf64(o, out);
    if (g_xstrip && o->arch != ARCH_X86) {
        Bytes tb = {0};
        int tr = single_seg(o, &tb, 0);
        if (tr == 0) return 0;
        if (tr == 1) {
            Bytes nb = {0};
            int r = fmt_elf32_norm(o, &nb);
            if (r && nb.n < tb.n) badd(out, nb.p, nb.n);
            else badd(out, tb.p, tb.n);
            return 1;
        }
    }
    if (g_xstrip && o->arch == ARCH_X86 && !g_ldo.dynamic) {
        Bytes tb = {0};
        int tr = tiny32(o, &tb);
        if (tr == 0) return 0;
        if (tr == 1) {
            Bytes nb = {0};
            int r = fmt_elf32_norm(o, &nb);
            if (r && nb.n < tb.n) badd(out, nb.p, nb.n);
            else badd(out, tb.p, tb.n);
            return 1;
        }
    }
    return fmt_elf32_norm(o, out);
}

static int fmt_elf32_norm(Obj *o, Bytes *out)
{
    int hdr = ELF_HDR;
    g_compact = g_strip && o->arch == ARCH_X86;
    link_layout(o, ELF_BASE + hdr, 0x1000, 3);
    if (g_compact && !g_lay.has_rw) {
        hdr = 52 + 32 * (g_ro_prog ? 1 : 2);
        link_layout(o, ELF_BASE + hdr, 0x1000, 3);
    }
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
    Bytes img = {0};
    link_range(o, &img, g_lay.base, g_lay.a_end);
    int rw = g_lay.has_rw;
    int nph = (rw ? 3 : 2) - (g_ro_prog ? 1 : 0);
    Bytes f = {0};
    u8 ident[16] = { 0x7F, 'E', 'L', 'F', 1, 1, 1, 0 };
    badd(&f, ident, 16);
    put16(&f, 2);
    put16(&f, (u16)elf_machine(o));
    put32(&f, 1);
    put32(&f, (u32)entry);
    put32(&f, 52);
    size_t shoff_pos = f.n;
    put32(&f, 0);
    put32(&f, elf_flags(o));
    put16(&f, 52);
    put16(&f, 32);
    put16(&f, (u16)nph);
    put16(&f, 40);
    size_t shnum_pos = f.n;
    put16(&f, 0);
    size_t shstr_pos = f.n;
    put16(&f, 0);
    i64 a_filesz = g_lay.a_end - ELF_BASE;
    put32(&f, 1); put32(&f, 0); put32(&f, ELF_BASE); put32(&f, ELF_BASE);
    put32(&f, (u32)a_filesz); put32(&f, (u32)a_filesz); put32(&f, 5); put32(&f, 0x1000);
    if (rw) {
        i64 fsz = g_lay.file_end - g_lay.rw_start;
        if (fsz < 0) fsz = 0;
        put32(&f, 1); put32(&f, (u32)(g_lay.rw_lma - ELF_BASE)); put32(&f, (u32)g_lay.rw_start); put32(&f, (u32)g_lay.rw_start);
        put32(&f, (u32)fsz); put32(&f, (u32)(g_lay.end - g_lay.rw_start)); put32(&f, 6); put32(&f, 0x1000);
    }
    if (!g_ro_prog) { put32(&f, 0x6474E551); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 6); put32(&f, 16); }
    while ((int)f.n < hdr) bad1(&f, 0);
    size_t base_off = f.n;
    badd(&f, img.p, img.n);
    (void)base_off;
    if (rw) {
        while ((i64)f.n < g_lay.rw_lma - ELF_BASE) bad1(&f, 0);
        link_range(o, &f, g_lay.rw_start, g_lay.file_end > g_lay.rw_start ? g_lay.file_end : g_lay.rw_start);
    }
    if (g_strip) {
        badd(out, f.p, f.n);
        return 1;
    }

    Bytes shstr = {0}, symtab = {0}, strtab = {0};
    bad1(&shstr, 0);
    bad1(&strtab, 0);
    int ns = g_lay.n;
    int *shidx = xcalloc((size_t)o->nsecs + 1, sizeof(int));
    for (int i = 0; i < ns; i++) shidx[g_lay.order[i]->idx] = i + 1;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) bfill(&symtab, 0, 16);
        for (int i = 0; i < o->nsyms; i++) {
            Sym *s = o->syms[i];
            if (!s->defined || !s->name[0]) continue;
            if (s->isequ && !s->isabs) continue;
            int isglob = s->global;
            if ((pass == 0) == isglob) continue;
            int shn;
            if (s->isequ || s->sec == SEC_ABS || s->sec < 0) shn = 0xFFF1;
            else if (!shidx[s->sec]) continue;
            else shn = shidx[s->sec];
            put32(&symtab, (u32)add_str(&strtab, s->name));
            put32(&symtab, (u32)sym_addr(o, s));
            put32(&symtab, 0);
            bad1(&symtab, (u8)(isglob ? 0x10 : 0));
            bad1(&symtab, 0);
            put16(&symtab, (u16)shn);
        }
    }
    int nlocal = 1;
    {
        for (int i = 0; i < o->nsyms; i++) {
            Sym *s = o->syms[i];
            if (!s->defined || !s->name[0]) continue;
            if (s->isequ && !s->isabs) continue;
            if (s->global) continue;
            if (!(s->isequ || s->sec == SEC_ABS || s->sec < 0) && !shidx[s->sec]) continue;
            nlocal++;
        }
    }
    int *secname = xcalloc((size_t)ns + 1, sizeof(int));
    for (int i = 0; i < ns; i++) secname[i] = add_str(&shstr, g_lay.order[i]->name);
    int n_symtab = add_str(&shstr, ".symtab");
    int n_strtab = add_str(&shstr, ".strtab");
    int n_shstr = add_str(&shstr, ".shstrtab");
    void *dw = dwarf_exe_begin(o, &shstr);
    while (f.n & 3) bad1(&f, 0);
    size_t sym_off = f.n;
    badd(&f, symtab.p, symtab.n);
    size_t str_off = f.n;
    badd(&f, strtab.p, strtab.n);
    size_t shs_off = f.n;
    badd(&f, shstr.p, shstr.n);
    dwarf_exe_data(dw, &f);
    while (f.n & 3) bad1(&f, 0);
    size_t sh_off = f.n;
    for (int i = 0; i < 10; i++) put32(&f, 0);
    for (int i = 0; i < ns; i++) {
        Sec *s = g_lay.order[i];
        u32 flags = 2 | (s->write ? 1 : 0) | (s->exec ? 4 : 0);
        put32(&f, (u32)secname[i]);
        put32(&f, s->nobits ? 8 : 1);
        put32(&f, flags);
        put32(&f, (u32)s->base);
        put32(&f, (u32)(s->nobits || s->base < g_lay.rw_start ? s->base - ELF_BASE : s->base - g_lay.rw_start + g_lay.rw_lma - ELF_BASE));
        put32(&f, (u32)s->size);
        put32(&f, 0);
        put32(&f, 0);
        put32(&f, (u32)(s->align > 0 ? s->align : 1));
        put32(&f, 0);
    }
    put32(&f, (u32)n_symtab); put32(&f, 2); put32(&f, 0); put32(&f, 0);
    put32(&f, (u32)sym_off); put32(&f, (u32)symtab.n); put32(&f, (u32)(ns + 2)); put32(&f, (u32)nlocal); put32(&f, 4); put32(&f, 16);
    put32(&f, (u32)n_strtab); put32(&f, 3); put32(&f, 0); put32(&f, 0);
    put32(&f, (u32)str_off); put32(&f, (u32)strtab.n); put32(&f, 0); put32(&f, 0); put32(&f, 1); put32(&f, 0);
    put32(&f, (u32)n_shstr); put32(&f, 3); put32(&f, 0); put32(&f, 0);
    put32(&f, (u32)shs_off); put32(&f, (u32)shstr.n); put32(&f, 0); put32(&f, 0); put32(&f, 1); put32(&f, 0);
    dwarf_exe_headers(dw, &f, 0);
    bputle(&f, shoff_pos, (u64)sh_off, 4);
    bputle(&f, shnum_pos, (u64)(ns + 4 + dwarf_exe_count(dw)), 2);
    bputle(&f, shstr_pos, (u64)(ns + 3), 2);
    badd(out, f.p, f.n);
    dwarf_exe_end(dw);
    free(shidx);
    free(secname);
    return 1;
}
