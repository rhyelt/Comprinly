#include "cly.h"

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }
static void put16(Bytes *b, u16 v) { badle(b, v, 2); }

static void put_name(Bytes *st, Bytes *str, const char *name)
{
    size_t n = strlen(name);
    if (n <= 8) {
        u8 b[8] = {0};
        memcpy(b, name, n);
        badd(st, b, 8);
    } else {
        put32(st, 0);
        put32(st, (u32)(str->n + 4));
        badd(str, name, n + 1);
    }
}

static void put_sym(Bytes *st, Bytes *str, const char *name, u32 value, int sect, int cls)
{
    put_name(st, str, name);
    put32(st, value);
    put16(st, (u16)sect);
    put16(st, 0);
    bad1(st, (u8)cls);
    bad1(st, 0);
}

static int alog(i64 a)
{
    int n = 0;
    while (a > 1 && n < 13) { a >>= 1; n++; }
    return n;
}

int fmt_coff(Obj *o, Bytes *out)
{
    int ns = o->nsecs;
    Bytes symtab = {0}, strtab = {0};
    int nsym = 0;
    for (int i = 0; i < ns; i++) {
        put_sym(&symtab, &strtab, o->secs[i]->name, 0, i + 1, 3);
        nsym++;
    }
    int *symidx = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        int glob = s->global || s->ext || s->common || (!s->defined && s->referenced);
        if (!s->name[0]) continue;
        if (s->isequ && !s->isabs && !glob) continue;
        if (!s->defined && !s->ext && !s->common && !s->referenced && !s->import) continue;
        if (s->defined && !s->common && !s->ext) {
            if (s->isequ || s->sec < 0) put_sym(&symtab, &strtab, s->name, (u32)(s->isequ ? s->eqv.n : s->off), -1, glob ? 2 : 3);
            else put_sym(&symtab, &strtab, s->name, (u32)s->off, s->sec + 1, glob ? 2 : 3);
        } else if (s->common) put_sym(&symtab, &strtab, s->name, (u32)s->commonsize, 0, 2);
        else put_sym(&symtab, &strtab, s->name, 0, 0, 2);
        symidx[i] = nsym++;
    }
    Bytes *rel = xcalloc((size_t)ns + 1, sizeof(Bytes));
    int *nrel = xcalloc((size_t)ns + 1, sizeof(int));
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        Val *v = &r->v;
        if (v->neg >= 0 || (v->pos >= 0 && v->ext)) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("expression cannot be expressed as a COFF relocation");
            continue;
        }
        int symi;
        if (v->ext) {
            int k = -1;
            for (int j = 0; j < o->nsyms; j++) if (o->syms[j] == v->ext) k = j;
            if (k < 0 || !symidx[k]) { err("symbol `%s' has no COFF symbol", v->ext->name); continue; }
            symi = symidx[k];
        } else if (v->pos >= 0) symi = v->pos;
        else continue;
        if (r->size != 4 && !(o->is64 && r->size == 8 && r->kind != RK_REL)) { g_pos.file = "<link>"; g_pos.line = r->line; err("COFF supports only 32-bit relocations%s", o->is64 ? " and absolute 64-bit addresses" : ""); continue; }
        i64 add = v->n;
        int type = o->is64 ? (r->size == 8 ? 1 : 2) : 6;
        if (r->kind == RK_REL) {
            add = v->n + r->off - r->relbase + 4;
            type = o->is64 ? 4 : 0x14;
        }
        Sec *s = o->secs[r->sec];
        if (!s->nobits && (size_t)(r->off + r->size) <= s->data.n) bputle(&s->data, (size_t)r->off, (u64)add, r->size);
        put32(&rel[r->sec], (u32)r->off);
        put32(&rel[r->sec], (u32)symi);
        put16(&rel[r->sec], (u16)type);
        nrel[r->sec]++;
    }
    if (g_errors) return 0;
    Bytes shstr = {0};
    Bytes f = {0};
    size_t hdr = 20 + 40 * (size_t)ns;
    size_t pos = hdr;
    size_t *rawoff = xcalloc((size_t)ns + 1, sizeof(size_t));
    size_t *reloff = xcalloc((size_t)ns + 1, sizeof(size_t));
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        if (!s->nobits && s->data.n) { rawoff[i] = pos; pos += s->data.n; }
        if (nrel[i]) { reloff[i] = pos; pos += rel[i].n; }
    }
    size_t symoff = pos;
    put16(&f, o->is64 ? 0x8664 : 0x14C);
    put16(&f, (u16)ns);
    put32(&f, 0);
    put32(&f, (u32)symoff);
    put32(&f, (u32)nsym);
    put16(&f, 0);
    put16(&f, 0);
    Bytes secstr = {0};
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        const char *nm = s->name;
        if (strlen(nm) <= 8) {
            u8 b[8] = {0};
            memcpy(b, nm, strlen(nm));
            badd(&f, b, 8);
        } else {
            char b[16];
            snprintf(b, sizeof b, "/%u", (unsigned)(strtab.n + 4 + secstr.n));
            u8 nb[8] = {0};
            memcpy(nb, b, strlen(b) > 8 ? 8 : strlen(b));
            badd(&f, nb, 8);
            badd(&secstr, nm, strlen(nm) + 1);
        }
        put32(&f, 0);
        put32(&f, 0);
        put32(&f, (u32)s->size);
        put32(&f, (u32)rawoff[i]);
        put32(&f, (u32)reloff[i]);
        put32(&f, 0);
        put16(&f, (u16)nrel[i]);
        put16(&f, 0);
        u32 ch = 0;
        if (s->exec) ch |= 0x20 | 0x20000000;
        else if (s->nobits) ch |= 0x80;
        else ch |= 0x40;
        if (s->alloc) ch |= 0x40000000;
        if (s->write) ch |= 0x80000000u;
        int al = alog(s->align > 0 ? s->align : 1) + 1;
        ch |= (u32)al << 20;
        put32(&f, ch);
    }
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        if (!s->nobits && s->data.n) badd(&f, s->data.p, s->data.n);
        if (nrel[i]) badd(&f, rel[i].p, rel[i].n);
    }
    badd(&f, symtab.p, symtab.n);
    put32(&f, (u32)(4 + strtab.n + secstr.n));
    badd(&f, strtab.p, strtab.n);
    badd(&f, secstr.p, secstr.n);
    badd(out, f.p, f.n);
    free(rawoff); free(reloff); free(rel); free(nrel); free(symidx);
    (void)shstr;
    return 1;
}
