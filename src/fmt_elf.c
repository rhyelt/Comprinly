#include "cly.h"

#define ELF_BASE 0x08048000
#define ELF_HDR 148

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }
static void put16(Bytes *b, u16 v) { badle(b, v, 2); }

static int add_str(Bytes *tab, const char *s)
{
    int off = (int)tab->n;
    badd(tab, s, strlen(s) + 1);
    return off;
}

static void put64(Bytes *b, u64 v) { badle(b, v, 8); }

#define ELF64_BASE 0x400000
#define ELF64_HDR 232

static int fmt_elf64(Obj *o, Bytes *out)
{
    link_layout(o, ELF64_BASE + ELF64_HDR, 0x1000, 3);
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
    Bytes img = {0};
    link_range(o, &img, g_lay.base, g_lay.a_end);
    int rw = g_lay.has_rw;
    int nph = rw ? 3 : 2;
    Bytes f = {0};
    u8 ident[16] = { 0x7F, 'E', 'L', 'F', 2, 1, 1, 0 };
    badd(&f, ident, 16);
    put16(&f, 2);
    put16(&f, 62);
    put32(&f, 1);
    put64(&f, (u64)entry);
    put64(&f, 64);
    size_t shoff_pos = f.n;
    put64(&f, 0);
    put32(&f, 0);
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
    put32(&f, 0x6474E551); put32(&f, 6); put64(&f, 0); put64(&f, 0); put64(&f, 0); put64(&f, 0); put64(&f, 0); put64(&f, 16);
    while (f.n < ELF64_HDR) bad1(&f, 0);
    badd(&f, img.p, img.n);
    if (rw) {
        while ((i64)f.n < g_lay.rw_lma - ELF64_BASE) bad1(&f, 0);
        link_range(o, &f, g_lay.rw_start, g_lay.file_end > g_lay.rw_start ? g_lay.file_end : g_lay.rw_start);
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
    while (f.n & 7) bad1(&f, 0);
    size_t sym_off = f.n;
    badd(&f, symtab.p, symtab.n);
    size_t str_off = f.n;
    badd(&f, strtab.p, strtab.n);
    size_t shs_off = f.n;
    badd(&f, shstr.p, shstr.n);
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
    bputle(&f, shoff_pos, (u64)sh_off, 8);
    bputle(&f, shnum_pos, (u64)(ns + 4), 2);
    bputle(&f, shstr_pos, (u64)(ns + 3), 2);
    badd(out, f.p, f.n);
    free(shidx);
    free(secname);
    return 1;
}

int fmt_elf(Obj *o, Bytes *out)
{
    if (o->is64) return fmt_elf64(o, out);
    link_layout(o, ELF_BASE + ELF_HDR, 0x1000, 3);
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
    Bytes img = {0};
    link_range(o, &img, g_lay.base, g_lay.a_end);
    int rw = g_lay.has_rw;
    int nph = rw ? 3 : 2;
    Bytes f = {0};
    u8 ident[16] = { 0x7F, 'E', 'L', 'F', 1, 1, 1, 0 };
    badd(&f, ident, 16);
    put16(&f, 2);
    put16(&f, 3);
    put32(&f, 1);
    put32(&f, (u32)entry);
    put32(&f, 52);
    size_t shoff_pos = f.n;
    put32(&f, 0);
    put32(&f, 0);
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
    put32(&f, 0x6474E551); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 0); put32(&f, 6); put32(&f, 16);
    while (f.n < ELF_HDR) bad1(&f, 0);
    size_t base_off = f.n;
    badd(&f, img.p, img.n);
    (void)base_off;
    if (rw) {
        while ((i64)f.n < g_lay.rw_lma - ELF_BASE) bad1(&f, 0);
        link_range(o, &f, g_lay.rw_start, g_lay.file_end > g_lay.rw_start ? g_lay.file_end : g_lay.rw_start);
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
    while (f.n & 3) bad1(&f, 0);
    size_t sym_off = f.n;
    badd(&f, symtab.p, symtab.n);
    size_t str_off = f.n;
    badd(&f, strtab.p, strtab.n);
    size_t shs_off = f.n;
    badd(&f, shstr.p, shstr.n);
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
    bputle(&f, shoff_pos, (u64)sh_off, 4);
    bputle(&f, shnum_pos, (u64)(ns + 4), 2);
    bputle(&f, shstr_pos, (u64)(ns + 3), 2);
    badd(out, f.p, f.n);
    free(shidx);
    free(secname);
    return 1;
}
