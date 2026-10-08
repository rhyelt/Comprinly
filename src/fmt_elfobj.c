#include "cly.h"

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }
static void put16(Bytes *b, u16 v) { badle(b, v, 2); }

static int add_str(Bytes *tab, const char *s)
{
    int off = (int)tab->n;
    badd(tab, s, strlen(s) + 1);
    return off;
}

static int W64;

static void putwd(Bytes *b, u64 v) { badle(b, v, W64 ? 8 : 4); }

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

static void put_sym(Bytes *st, Bytes *str, const char *name, u64 value, u64 size, int info, int shn)
{
    if (W64) {
        put32(st, name && *name ? (u32)add_str(str, name) : 0);
        bad1(st, (u8)info);
        bad1(st, 0);
        put16(st, (u16)shn);
        badle(st, value, 8);
        badle(st, size, 8);
        return;
    }
    put32(st, name && *name ? (u32)add_str(str, name) : 0);
    put32(st, value);
    put32(st, size);
    bad1(st, (u8)info);
    bad1(st, 0);
    put16(st, (u16)shn);
}

int fmt_elfobj(Obj *o, Bytes *out)
{
    int ns = o->nsecs;
    W64 = o->is64;
    Bytes symtab = {0}, strtab = {0}, shstr = {0};
    bad1(&strtab, 0);
    bad1(&shstr, 0);
    put_sym(&symtab, &strtab, NULL, 0, 0, 0, 0);
    for (int i = 0; i < ns; i++) put_sym(&symtab, &strtab, NULL, 0, 0, 3, i + 1);
    int nsym = 1 + ns;
    int nlocal = nsym;
    int *symidx = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < o->nsyms; i++) {
            Sym *s = o->syms[i];
            int glob = s->global || s->ext || s->common || (!s->defined && s->referenced);
            if ((pass == 0) == glob) continue;
            if (!s->name[0]) continue;
            if (s->import) continue;
            if (s->isequ && !s->isabs && !glob) continue;
            if (!s->defined && !s->ext && !s->common && !s->referenced) continue;
            if (s->defined && !s->common && !s->ext) {
                if (s->isequ || s->sec < 0) put_sym(&symtab, &strtab, s->name, (u64)(s->isequ ? s->eqv.n : s->off), 0, glob ? 0x10 : 0, 0xFFF1);
                else put_sym(&symtab, &strtab, s->name, (u64)s->off, 0, glob ? 0x10 : 0, s->sec + 1);
            } else if (s->common) {
                put_sym(&symtab, &strtab, s->name, 4, (u32)s->commonsize, 0x10, 0xFFF2);
            } else put_sym(&symtab, &strtab, s->name, 0, 0, 0x10, 0);
            symidx[i] = nsym++;
            if (pass == 0) nlocal = nsym;
        }
    }
    Bytes *rel = xcalloc((size_t)ns + 1, sizeof(Bytes));
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        Val *v = &r->v;
        if (v->neg >= 0 || (v->pos >= 0 && v->ext)) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("expression cannot be expressed as an ELF relocation");
            continue;
        }
        int symi;
        if (v->ext) {
            int k = -1;
            for (int j = 0; j < o->nsyms; j++) if (o->syms[j] == v->ext) k = j;
            if (k < 0 || !symidx[k]) { err("symbol `%s' has no ELF symbol", v->ext->name); continue; }
            symi = symidx[k];
        } else if (v->pos >= 0) symi = 1 + v->pos;
        else continue;
        int type;
        i64 add = v->n;
        Sec *s = o->secs[r->sec];
        if (W64) {
            if (r->kind == RK_REL) {
                add = v->n + r->off - r->relbase;
                type = r->size == 8 ? 24 : r->size == 4 ? 2 : r->size == 2 ? 13 : 15;
            } else type = r->size == 8 ? 1 : r->size == 4 ? 10 : r->size == 2 ? 12 : 14;
            if (!s->nobits && (size_t)(r->off + r->size) <= s->data.n) bputle(&s->data, (size_t)r->off, 0, r->size);
            badle(&rel[r->sec], (u64)r->off, 8);
            badle(&rel[r->sec], ((u64)symi << 32) | (u64)type, 8);
            badle(&rel[r->sec], (u64)add, 8);
            continue;
        }
        if (r->kind == RK_REL) {
            add = v->n + r->off - r->relbase;
            type = r->size == 4 ? 2 : r->size == 2 ? 21 : 23;
        } else type = r->size == 4 ? 1 : r->size == 2 ? 20 : 22;
        if (!s->nobits && (size_t)(r->off + r->size) <= s->data.n) bputle(&s->data, (size_t)r->off, (u64)add, r->size);
        put32(&rel[r->sec], (u32)r->off);
        put32(&rel[r->sec], ((u32)symi << 8) | (u32)type);
    }
    if (g_errors) return 0;
    int *sname = xcalloc((size_t)ns + 1, sizeof(int));
    int *rname = xcalloc((size_t)ns + 1, sizeof(int));
    for (int i = 0; i < ns; i++) sname[i] = add_str(&shstr, o->secs[i]->name);
    int nrels = 0;
    for (int i = 0; i < ns; i++) {
        if (rel[i].n) {
            char buf[300];
            snprintf(buf, sizeof buf, W64 ? ".rela%s" : ".rel%s", o->secs[i]->name);
            rname[i] = add_str(&shstr, buf);
            nrels++;
        }
    }
    int n_symtab = add_str(&shstr, ".symtab");
    int n_strtab = add_str(&shstr, ".strtab");
    int n_shstr = add_str(&shstr, ".shstrtab");
    Bytes f = {0};
    u8 ident[16] = { 0x7F, 'E', 'L', 'F', (u8)(W64 ? 2 : 1), 1, 1, 0 };
    badd(&f, ident, 16);
    put16(&f, 1); put16(&f, W64 ? 62 : 3); put32(&f, 1); putwd(&f, 0); putwd(&f, 0);
    size_t shoff_pos = f.n;
    putwd(&f, 0); put32(&f, 0);
    put16(&f, W64 ? 64 : 52); put16(&f, 0); put16(&f, 0); put16(&f, W64 ? 64 : 40);
    int total = 1 + ns + 3 + nrels;
    put16(&f, (u16)total);
    put16(&f, (u16)(ns + 3));
    size_t *soff = xcalloc((size_t)ns + 1, sizeof(size_t));
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        if (s->nobits) continue;
        while (f.n & 15) bad1(&f, 0);
        soff[i] = f.n;
        badd(&f, s->data.p, s->data.n);
    }
    size_t *roff = xcalloc((size_t)ns + 1, sizeof(size_t));
    for (int i = 0; i < ns; i++) {
        if (!rel[i].n) continue;
        while (f.n & 7) bad1(&f, 0);
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
        shdr(&f, (u32)sname[i], s->nobits ? 8 : 1, (s->alloc ? 2u : 0) | (s->write ? 1u : 0) | (s->exec ? 4u : 0), soff[i], (u64)s->size, 0, 0, (u64)(s->align > 0 ? s->align : 1), 0);
    }
    int symtab_idx = 1 + ns;
    shdr(&f, (u32)n_symtab, 2, 0, sym_off, symtab.n, (u32)(symtab_idx + 1), (u32)nlocal, W64 ? 8 : 4, W64 ? 24 : 16);
    shdr(&f, (u32)n_strtab, 3, 0, str_off, strtab.n, 0, 0, 1, 0);
    shdr(&f, (u32)n_shstr, 3, 0, shs_off, shstr.n, 0, 0, 1, 0);
    for (int i = 0; i < ns; i++) {
        if (!rel[i].n) continue;
        shdr(&f, (u32)rname[i], W64 ? 4 : 9, 0x40, roff[i], rel[i].n, (u32)symtab_idx, (u32)(i + 1), W64 ? 8 : 4, W64 ? 24 : 8);
    }
    bputle(&f, shoff_pos, (u64)sh_off, W64 ? 8 : 4);
    badd(out, f.p, f.n);
    free(sname); free(rname); free(soff); free(roff); free(rel); free(symidx);
    return 1;
}
