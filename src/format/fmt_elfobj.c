/* Writer for ELF relocatable object files (.o) for x86, x86-64, ARM64 and RISC-V, matching NASM's section and symbol layout. */
#include "cly.h"

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }
static void put16(Bytes *b, u16 v) { badle(b, v, 2); }

/* Adds a string to a string table and returns its offset. */
static int add_str(Bytes *tab, const char *s)
{
    int off = (int)tab->n;
    badd(tab, s, strlen(s) + 1);
    return off;
}

static int add_str_tail(Bytes *tab, const char *s)
{
    size_t n = strlen(s) + 1;
    for (size_t i = 0; i + n <= tab->n; i++) {
        if (!memcmp((char *)tab->p + i, s, n) && (i + n == tab->n || ((char *)tab->p)[i + n - 1] == 0)) return (int)i;
    }
    return add_str(tab, s);
}

static int W64;
static int X32;

static void put_rela(Bytes *b, u64 off, u64 sym, u64 type, u64 add)
{
    if (W64) {
        badle(b, off, 8);
        badle(b, (sym << 32) | type, 8);
        badle(b, add, 8);
    } else {
        put32(b, (u32)off);
        put32(b, (u32)((sym << 8) | (type & 255)));
        put32(b, (u32)add);
    }
}

static void putwd(Bytes *b, u64 v) { badle(b, v, W64 ? 8 : 4); }

/* Writes one section header. */
static void shdr(Bytes *f, u32 name, u32 type, u64 flags, u64 off, u64 size, u32 link, u32 info, u64 align, u64 ent)
{
    put32(f, name);
    put32(f, type);
    putwd(f, flags);
    putwd(f, 0);
    putwd(f, off);
    putwd(f, size);
    put32(f, link);
    put32(f, info);
    putwd(f, align);
    putwd(f, ent);
}

static int g_other;
typedef struct { size_t at; char *name; } NameRef;
static NameRef *g_nr;
static int g_nnr, g_cnr;

static void name_ref(Bytes *st, const char *name)
{
    if (!name || !*name) { put32(st, 0); return; }
    if (g_nnr == g_cnr) { g_cnr = g_cnr ? g_cnr * 2 : 64; g_nr = xrealloc(g_nr, sizeof(NameRef) * (size_t)g_cnr); }
    g_nr[g_nnr].at = st->n;
    g_nr[g_nnr].name = xstrdup(name);
    g_nnr++;
    put32(st, 0);
}

static int nr_cmp(const void *a, const void *b)
{
    size_t la = strlen(((const NameRef *)a)->name), lb = strlen(((const NameRef *)b)->name);
    return la < lb ? 1 : la > lb ? -1 : 0;
}

static void name_flush(Bytes *st, Bytes *str)
{
    if (!g_nnr) return;
    qsort(g_nr, (size_t)g_nnr, sizeof(NameRef), nr_cmp);
    for (int i = 0; i < g_nnr; i++) {
        int off = add_str_tail(str, g_nr[i].name);
        bputle(st, g_nr[i].at, (u64)off, 4);
        free(g_nr[i].name);
    }
    g_nnr = 0;
}

/* Writes one symbol table entry (32 or 64-bit layout). */
static void put_sym(Bytes *st, Bytes *str, const char *name, u64 value, u64 size, int info, int shn)
{
    if (W64) {
        (void)str;
        name_ref(st, name);
        bad1(st, (u8)info);
        bad1(st, (u8)g_other);
        put16(st, (u16)shn);
        badle(st, value, 8);
        badle(st, size, 8);
        return;
    }
    name_ref(st, name);
    put32(st, value);
    put32(st, size);
    bad1(st, (u8)info);
    bad1(st, (u8)g_other);
    put16(st, (u16)shn);
}

/* Maps a relocation kind to its ARM64 ELF relocation number. */
static int a64_elf_type(Reloc *r, int *ok)
{
    *ok = 1;
    switch (r->kind) {
    case RK_ABS: return r->size == 8 ? 257 : r->size == 4 ? 258 : r->size == 2 ? 259 : (*ok = 0);
    case RK_REL: return r->size == 8 ? 260 : r->size == 4 ? 261 : r->size == 2 ? 262 : (*ok = 0);
    case RK_A64_B26: return r->size == 1 ? 283 : 282;
    case RK_A64_B19: return r->size == 1 ? 273 : 280;
    case RK_A64_B14: return 279;
    case RK_A64_ADR: return 274;
    case RK_A64_ADRP: return 275;
    case RK_A64_LO12: {
        static const int t[] = { 277, 284, 285, 286, 299 };
        return r->size >= 0 && r->size <= 4 ? t[r->size] : (*ok = 0);
    }
    case RK_A64_MOVW: {
        int g = r->size & 3, nc = r->size >> 2;
        static const int t[] = { 263, 265, 267, 269 };
        return t[g] + (nc ? 1 : 0) - (g == 3 ? (nc ? 1 : 0) : 0);
    }
    }
    *ok = 0;
    return 0;
}

/* Maps a relocation kind to its RISC-V ELF relocation number. */
static int rv_elf_type(Reloc *r, int *ok)
{
    *ok = 1;
    switch (r->kind) {
    case RK_ABS: return r->size == 8 ? 2 : r->size == 4 ? 1 : (*ok = 0);
    case RK_RV_JAL: return 17;
    case RK_RV_BRANCH: return 16;
    case RK_RV_HI20: return 26;
    case RK_RV_LO12I: return 27;
    case RK_RV_LO12S: return 28;
    case RK_RV_PCHI20: return 23;
    case RK_RV_PCLO12I: return 24;
    case RK_RV_PCLO12S: return 25;
    case RK_RV_CALL: return 19;
    }
    *ok = 0;
    return 0;
}

void obj_live_secs(Obj *o, int *keep, int *secsym)
{
    int narch = o->arch != ARCH_X86;
    for (int i = 0; i < o->nsecs; i++) {
        Sec *s = o->secs[i];
        keep[i] = s->size > 0 || s->data.n > 0 || i == 0;
        secsym[i] = 0;
        if (g_nasm) keep[i] = secsym[i] = 1;
    }
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        if (r->sec >= 0 && r->sec < o->nsecs) keep[r->sec] = 1;
        if (!r->v.ext && r->v.pos >= 0 && r->v.pos < o->nsecs) {
            keep[r->v.pos] = 1;
            if (!(narch && (r->kind == RK_RV_PCLO12I || r->kind == RK_RV_PCLO12S))) secsym[r->v.pos] = 1;
        }
        if (r->v.neg >= 0 && r->v.neg < o->nsecs) { keep[r->v.neg] = 1; secsym[r->v.neg] = 1; }
    }
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        if (s->defined && !s->isequ && !s->common && !s->ext && s->sec >= 0 && s->sec < o->nsecs) keep[s->sec] = 1;
    }
}

/* Builds the object file: sections, symbol table with FILE symbol and section symbols, relocations and string tables. */
int fmt_elfobj(Obj *o, Bytes *out)
{
    int ns = o->nsecs;
    g_nnr = 0;
    int narch = o->arch != ARCH_X86;
    X32 = o->otype == T_X32;
    W64 = o->is64 && !X32;
    Bytes symtab = {0}, strtab = {0}, shstr = {0};
    bad1(&strtab, 0);
    bad1(&shstr, 0);
    int *keep = xcalloc((size_t)ns + 1, sizeof(int)), *need = xcalloc((size_t)ns + 1, sizeof(int));
    int *secnew = xcalloc((size_t)ns + 1, sizeof(int)), *secsym = xcalloc((size_t)ns + 1, sizeof(int));
    obj_live_secs(o, keep, need);
    int nk = 0;
    for (int i = 0; i < ns; i++) if (keep[i]) secnew[i] = ++nk;
    put_sym(&symtab, &strtab, NULL, 0, 0, 0, 0);
    int nsym = 1;
    for (int i = 0; i < ns; i++) {
        if (!keep[i] || !need[i]) continue;
        put_sym(&symtab, &strtab, NULL, 0, 0, 3, secnew[i]);
        secsym[i] = nsym++;
    }
    if (o->src && g_nasm) {
        const char *fn = o->src;
        put_sym(&symtab, &strtab, fn, 0, 0, 4, 0xfff1);
        nsym++;
    }
    int nlocal = nsym;
    int *symidx = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    int nsyn = 0;
    int *syn_sec = NULL;
    i64 *syn_off = NULL;
    int *syn_idx = NULL;
    if (narch) {
        for (int i = 0; i < o->nrelocs; i++) {
            Reloc *r = &o->relocs[i];
            if (r->kind != RK_RV_PCLO12I && r->kind != RK_RV_PCLO12S) continue;
            int dup = 0;
            for (int k = 0; k < nsyn; k++) if (syn_sec[k] == r->v.pos && syn_off[k] == r->v.n) dup = 1;
            if (dup) continue;
            syn_sec = xrealloc(syn_sec, sizeof(int) * (size_t)(nsyn + 1));
            syn_off = xrealloc(syn_off, sizeof(i64) * (size_t)(nsyn + 1));
            syn_idx = xrealloc(syn_idx, sizeof(int) * (size_t)(nsyn + 1));
            syn_sec[nsyn] = r->v.pos;
            syn_off[nsyn] = r->v.n;
            nsyn++;
        }
        for (int k = 0; k < nsyn; k++) {
            char nm[40];
            snprintf(nm, sizeof nm, ".Lpcrel_hi%d", k);
            put_sym(&symtab, &strtab, nm, (u64)syn_off[k], 0, 0, secnew[syn_sec[k]]);
            syn_idx[k] = nsym++;
        }
        nlocal = nsym;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < o->nsyms; i++) {
            Sym *s = o->syms[i];
            int glob = s->global || s->ext || s->common || (!s->defined && s->referenced);
            if ((pass == 0) == glob) continue;
            if (!s->name[0]) continue;
            if (s->import) continue;
            if (s->isequ && !s->isabs && !glob) continue;
            if (!s->defined && !s->ext && !s->common && !s->referenced) continue;
            if (g_nasm && s->ext && !s->defined && !s->common && !s->referenced) continue;
            g_other = s->svis;
            int ty = s->stype;
            if (s->defined && !s->common && !s->ext) {
                if (s->isequ || s->sec < 0) put_sym(&symtab, &strtab, sym_mangle(s, glob), (u64)(s->isequ ? s->eqv.n : s->off), (u64)s->ssize, (glob ? 0x10 : 0) | ty, 0xFFF1);
                else put_sym(&symtab, &strtab, sym_mangle(s, glob), (u64)s->off, (u64)s->ssize, (glob ? 0x10 : 0) | ty, secnew[s->sec]);
            } else if (s->common) {
                put_sym(&symtab, &strtab, sym_mangle(s, 1), (u64)s->commonalign, (u32)s->commonsize, 0x10 | ty, 0xFFF2);
            } else put_sym(&symtab, &strtab, sym_mangle(s, 1), 0, 0, 0x10, 0);
            g_other = 0;
            symidx[i] = nsym++;
            if (pass == 0) nlocal = nsym;
        }
    }
    Bytes *rel = xcalloc((size_t)ns + 1, sizeof(Bytes));
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        Val *v = &r->v;
        Reloc diffrel;
        /* `sym - here_section_label`: the subtracted label sits in the section being written, so the difference is a PC relative relocation with the distance back to that label folded into the addend (this is what NASM does). */
        if (!narch && v->neg == r->sec && v->neg >= 0 && r->kind == RK_ABS && !(v->pos >= 0 && v->ext) && (v->pos >= 0 || v->ext)) {
            diffrel = *r;
            diffrel.kind = RK_REL;
            diffrel.relbase = 0;
            diffrel.v.neg = -1;
            r = &diffrel;
            v = &r->v;
        }
        if (v->neg >= 0 || (v->pos >= 0 && v->ext)) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("expression cannot be expressed as an ELF relocation");
            continue;
        }
        int symi;
        if (narch && (r->kind == RK_RV_PCLO12I || r->kind == RK_RV_PCLO12S)) {
            symi = 0;
            for (int k = 0; k < nsyn; k++) if (syn_sec[k] == v->pos && syn_off[k] == v->n) symi = syn_idx[k];
            int ok2;
            int ty = rv_elf_type(r, &ok2);
            Sec *s2 = o->secs[r->sec];
            if (!s2->nobits && (size_t)(r->off + 4) <= s2->data.n) {}
            put_rela(&rel[r->sec], (u64)r->off, (u64)symi, (u64)ty, 0);
            continue;
        }
        if (v->ext) {
            int k = -1;
            for (int j = 0; j < o->nsyms; j++) if (o->syms[j] == v->ext) k = j;
            if (k < 0 || !symidx[k]) { err("symbol `%s' has no ELF symbol", v->ext->name); continue; }
            symi = symidx[k];
        } else if (v->pos >= 0) symi = secsym[v->pos];
        else continue;
        int type;
        i64 add = v->n;
        Sec *s = o->secs[r->sec];
        if (narch) {
            int ok2;
            type = o->arch == ARCH_A64 ? a64_elf_type(r, &ok2) : rv_elf_type(r, &ok2);
            if (!ok2) {
                g_pos.file = "<link>";
                g_pos.line = r->line;
                err("this kind of reference cannot be written to an %s object file", arch_name(o->arch));
                continue;
            }
            if (!s->nobits && (size_t)(r->off + r->size) <= s->data.n && r->size > 0 && r->size <= 8 && (r->kind == RK_ABS || r->kind == RK_REL)) bputle(&s->data, (size_t)r->off, 0, r->size);
            put_rela(&rel[r->sec], (u64)r->off, (u64)symi, (u64)type, (u64)add);
            continue;
        }
        if (o->is64) {
            if (r->kind == RK_REL) {
                add = v->n + r->off - r->relbase;
                type = r->size == 8 ? 24 : r->size == 4 ? 2 : r->size == 2 ? 13 : 15;
                if (r->size == 4) {
                    if (v->wk == 1) type = 4;
                    else if (v->wk == 2) type = 9;
                    else if (v->wk == 5) type = 22;
                    else if (v->wk == 4) type = 9;
                }
            } else {
                type = r->size == 8 ? 1 : r->size == 4 ? (r->sext ? 11 : 10) : r->size == 2 ? 12 : 14;
                if (r->size == 8 && v->wk == 2) type = 27;
                else if (r->size == 8 && v->wk == 3) type = 25;
                else if (r->size == 4 && v->wk == 2) type = 3;
                else if (r->size == 4 && v->wk == 6) type = 23;
            }
            if (!s->nobits && (size_t)(r->off + r->size) <= s->data.n) bputle(&s->data, (size_t)r->off, 0, r->size);
            put_rela(&rel[r->sec], (u64)r->off, (u64)symi, (u64)type, (u64)add);
            continue;
        }
        if (r->kind == RK_REL) {
            add = v->n + r->off - r->relbase;
            type = r->size == 4 ? 2 : r->size == 2 ? 21 : 23;
            if (r->size == 4 && v->wk == 1) type = 4;
        } else {
            type = r->size == 4 ? 1 : r->size == 2 ? 20 : 22;
            if (r->size == 4 && v->wk == 2) type = 3;
            else if (r->size == 4 && v->wk == 3) type = 9;
            else if (r->size == 4 && v->wk == 4) type = 10;
        }
        if (!s->nobits && (size_t)(r->off + r->size) <= s->data.n) bputle(&s->data, (size_t)r->off, (u64)add, r->size);
        put32(&rel[r->sec], (u32)r->off);
        put32(&rel[r->sec], ((u32)symi << 8) | (u32)type);
    }
    if (g_errors) return 0;
    name_flush(&symtab, &strtab);
    int *sname = xcalloc((size_t)ns + 1, sizeof(int));
    int *rname = xcalloc((size_t)ns + 1, sizeof(int));
    int nrels = 0;
    for (int i = 0; i < ns; i++) if (keep[i] && rel[i].n) nrels++;
    int n_symtab = add_str(&shstr, ".symtab");
    int n_strtab = add_str(&shstr, ".strtab");
    int n_shstr = add_str(&shstr, ".shstrtab");
    int n_gnustack = add_str(&shstr, ".note.GNU-stack");
    for (int i = 0; i < ns; i++) {
        if (!keep[i]) continue;
        if (rel[i].n) {
            char buf[300];
            snprintf(buf, sizeof buf, (o->is64 || narch) ? ".rela%s" : ".rel%s", o->secs[i]->name);
            rname[i] = add_str_tail(&shstr, buf);
        }
        sname[i] = add_str_tail(&shstr, o->secs[i]->name);
    }
    Bytes f = {0};
    u8 ident[16] = { 0x7F, 'E', 'L', 'F', (u8)(W64 ? 2 : 1), 1, 1, 0 };
    badd(&f, ident, 16);
    int mach = o->arch == ARCH_A64 ? 183 : (o->arch == ARCH_RV64 || o->arch == ARCH_RV32) ? 243 : (o->is64 ? 62 : 3);
    u32 eflags = 0;
    if (o->arch == ARCH_RV64 || o->arch == ARCH_RV32) {
        if (o->rvext & RVX_C) eflags |= 1;
        if (o->rvext & RVX_D) eflags |= 4;
        else if (o->rvext & RVX_F) eflags |= 2;
    }
    put16(&f, 1); put16(&f, (u16)mach); put32(&f, 1); putwd(&f, 0); putwd(&f, 0);
    size_t shoff_pos = f.n;
    putwd(&f, 0); put32(&f, eflags);
    put16(&f, W64 ? 64 : 52); put16(&f, 0); put16(&f, 0); put16(&f, W64 ? 64 : 40);
    int total = 1 + nk + 3 + nrels + 1;
    put16(&f, (u16)total);
    put16(&f, (u16)(nk + 3));
    size_t *soff = xcalloc((size_t)ns + 1, sizeof(size_t));
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        if (s->nobits || !keep[i]) continue;
        while (f.n & ((size_t)(s->align > 8 ? 8 : s->align > 4 ? 4 : s->align > 2 ? 2 : 1) - 1)) bad1(&f, 0);
        soff[i] = f.n;
        badd(&f, s->data.p, s->data.n);
    }
    size_t *roff = xcalloc((size_t)ns + 1, sizeof(size_t));
    for (int i = 0; i < ns; i++) {
        if (!rel[i].n || !keep[i]) continue;
        while (f.n & (W64 ? 7 : 3)) bad1(&f, 0);
        roff[i] = f.n;
        badd(&f, rel[i].p, rel[i].n);
    }
    while (f.n & 3) bad1(&f, 0);
    size_t sym_off = f.n;
    badd(&f, symtab.p, symtab.n);
    size_t str_off = f.n;
    badd(&f, strtab.p, strtab.n);
    size_t shs_off = f.n;
    badd(&f, shstr.p, shstr.n);
    while (f.n & 3) bad1(&f, 0);
    size_t sh_off = f.n;
    for (int i = 0; i < (W64 ? 16 : 10); i++) put32(&f, 0);
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        if (!keep[i]) continue;
        shdr(&f, (u32)sname[i], s->nobits ? 8 : 1, (s->alloc ? 2u : 0) | (s->write ? 1u : 0) | (s->exec ? 4u : 0), soff[i], (u64)s->size, 0, 0, (u64)(s->align > 0 ? s->align : 1), 0);
    }
    int symtab_idx = 1 + nk;
    shdr(&f, (u32)n_symtab, 2, 0, sym_off, symtab.n, (u32)(symtab_idx + 1), (u32)nlocal, W64 ? 8 : 4, W64 ? 24 : 16);
    shdr(&f, (u32)n_strtab, 3, 0, str_off, strtab.n, 0, 0, 1, 0);
    shdr(&f, (u32)n_shstr, 3, 0, shs_off, shstr.n, 0, 0, 1, 0);
    for (int i = 0; i < ns; i++) {
        if (!rel[i].n || !keep[i]) continue;
        shdr(&f, (u32)rname[i], (o->is64 || narch) ? 4 : 9, 0x40, roff[i], rel[i].n, (u32)symtab_idx, (u32)secnew[i], W64 ? 8 : 4, W64 ? 24 : ((narch || X32) ? 12 : 8));
    }
    shdr(&f, (u32)n_gnustack, 1, 0, 0, 0, 0, 0, 1, 0);
    bputle(&f, shoff_pos, (u64)sh_off, W64 ? 8 : 4);
    badd(out, f.p, f.n);
    free(keep); free(need); free(secnew); free(secsym); free(sname); free(rname); free(soff); free(roff); free(rel); free(symidx);
    return 1;
}
