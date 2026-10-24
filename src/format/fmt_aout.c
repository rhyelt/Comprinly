/* Writer for 32-bit a.out relocatable objects (little endian "aout" and NetBSD style "aoutb"), laid out like NASM's. */
#include "cly.h"

static void p32(Bytes *b, u32 v) { badle(b, v, 4); }

/* Segment of a section: 0 text, 1 data, 2 bss. */
static int seg_of(Sec *s)
{
    if (s->nobits) return 2;
    if (s->exec) return 0;
    return 1;
}

int fmt_aout(Obj *o, Bytes *out)
{
    int ns = o->nsecs;
    if (o->is64 || o->arch != ARCH_X86 || o->bits == 64) {
        fprintf(stderr, "cly: a.out output is only supported for 32-bit x86\n");
        return 0;
    }
    i64 *base = xcalloc((size_t)ns + 1, sizeof(i64));
    int *seg = xcalloc((size_t)ns + 1, sizeof(int));
    i64 segsz[3] = {0, 0, 0};
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        int g = seg_of(s);
        seg[i] = g;
        i64 al = s->align > 0 ? s->align : 1;
        segsz[g] = (segsz[g] + al - 1) / al * al;
        base[i] = segsz[g];
        segsz[g] += s->size;
    }
    for (int g = 0; g < 3; g++) segsz[g] = (segsz[g] + 3) & ~3;
    i64 segbase[3] = {0, segsz[0], segsz[0] + segsz[1]};
    Bytes img[2] = {{0}, {0}};
    for (int g = 0; g < 2; g++) {
        for (i64 k = 0; k < segsz[g]; k++) bad1(&img[g], 0x90);
        for (int i = 0; i < ns; i++) {
            Sec *s = o->secs[i];
            if (seg[i] != g || !s->data.n) continue;
            memcpy(img[g].p + base[i], s->data.p, s->data.n);
        }
    }
    Bytes str = {0}, syms = {0};
    p32(&str, 0);
    int nsym = 0;
    int *symidx = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    int *ord = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    for (int i = 0; i < o->nsyms; i++) ord[i] = i;
    for (int i = 1; i < o->nsyms; i++) {
        int k = ord[i], j = i - 1;
        while (j >= 0 && o->syms[ord[j]]->defline > o->syms[k]->defline) { ord[j + 1] = ord[j]; j--; }
        ord[j + 1] = k;
    }
    for (int i = 0; i < o->nsyms; i++) symidx[i] = -1;
    {
        for (int oi = 0; oi < o->nsyms; oi++) {
            int i = ord[oi];
            Sym *s = o->syms[i];
            int glob = s->global || s->ext || s->common || (!s->defined && s->referenced);
            int undef = s->ext || s->common || !s->defined;
            if (!s->name[0] || s->import) continue;
            if (s->isequ && !s->isabs && !glob) continue;
            if (!s->defined && !s->ext && !s->common && !s->referenced) continue;
            const char *nm = sym_mangle(s, glob);
            u32 strx = (u32)str.n;
            badd(&str, nm, strlen(nm) + 1);
            int type;
            i64 val = 0;
            if (s->common) { type = 1; val = s->commonsize; }
            else if (undef) type = 1;
            else if (s->isequ || s->sec < 0) { type = 2 | (glob ? 1 : 0); val = s->isequ ? s->eqv.n : s->off; }
            else {
                type = (seg[s->sec] == 0 ? 4 : seg[s->sec] == 1 ? 6 : 8) | (glob ? 1 : 0);
                val = segbase[seg[s->sec]] + base[s->sec] + s->off;
            }
            p32(&syms, strx);
            bad1(&syms, (u8)type);
            bad1(&syms, 0);
            badle(&syms, 0, 2);
            p32(&syms, (u32)val);
            symidx[i] = nsym++;
        }
    }
    Bytes rel[2] = {{0}, {0}};
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        Val *v = &r->v;
        int g = seg[r->sec];
        if (g == 2) continue;
        if (r->kind > RK_REL || r->size != 1 && r->size != 2 && r->size != 4) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("this kind of reference cannot be written to an a.out object file");
            continue;
        }
        if (v->neg >= 0 || (v->pos >= 0 && v->ext)) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("expression cannot be expressed as an a.out relocation");
            continue;
        }
        i64 fo = base[r->sec] + r->off;
        i64 add = v->n;
        u32 symnum;
        int ext = 0;
        if (v->ext) {
            int k = -1;
            for (int j = 0; j < o->nsyms; j++) if (o->syms[j] == v->ext) k = j;
            if (k < 0 || symidx[k] < 0) { err("symbol `%s' has no a.out symbol", v->ext->name); continue; }
            symnum = (u32)symidx[k];
            ext = 1;
        } else if (v->pos >= 0) {
            symnum = seg[v->pos] == 0 ? 4 : seg[v->pos] == 1 ? 6 : 8;
            add += segbase[seg[v->pos]] + base[v->pos];
        } else continue;
        int pcrel = r->kind == RK_REL;
        if (pcrel) {
            add -= segbase[g] + base[r->sec] + (r->relbase ? r->relbase : r->off + r->size);
            if (!ext && seg[v->pos] == g) {
                bputle(&img[g], (size_t)fo, (u64)(v->n + (base[v->pos] - base[r->sec]) - (r->relbase ? r->relbase : r->off + r->size)), r->size);
                continue;
            }
        }
        bputle(&img[g], (size_t)fo, (u64)add, r->size);
        p32(&rel[g], (u32)fo);
        u32 info = symnum & 0xffffff;
        info |= (u32)((pcrel ? 1 : 0) | ((r->size == 4 ? 2 : r->size == 2 ? 1 : 0) << 1) | (ext ? 8 : 0)) << 24;
        p32(&rel[g], info);
    }
    if (g_errors) return 0;
    if (o->otype == T_AOUTB) { bad1(out, 0); bad1(out, 0x86); bad1(out, 1); bad1(out, 7); }
    else { bad1(out, 7); bad1(out, 1); bad1(out, 0x64); bad1(out, 0); }
    p32(out, (u32)segsz[0]);
    p32(out, (u32)segsz[1]);
    p32(out, (u32)segsz[2]);
    p32(out, (u32)syms.n);
    p32(out, 0);
    p32(out, (u32)rel[0].n);
    p32(out, (u32)rel[1].n);
    badd(out, img[0].p, img[0].n);
    badd(out, img[1].p, img[1].n);
    badd(out, rel[0].p, rel[0].n);
    badd(out, rel[1].p, rel[1].n);
    badd(out, syms.p, syms.n);
    bputle(&str, 0, (u64)str.n, 4);
    badd(out, str.p, str.n);
    free(base); free(seg); free(symidx);
    return 1;
}
