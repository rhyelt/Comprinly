/* Writer for as86 object files (bin86 and dev86 toolchain), laid out like NASM's -f as86 output. */
#include "cly.h"

static void w16(Bytes *b, u32 v) { badle(b, v, 2); }
static void w32(Bytes *b, u32 v) { badle(b, v, 4); }

static int seg_of(Sec *s)
{
    if (s->nobits) return 2;
    if (s->exec) return 0;
    return 1;
}

typedef struct { i64 off; int size, rel, type, number; i64 add; } Piece;

static int g_rsize;

static void set_rsize(Bytes *b, int size)
{
    if (g_rsize == size) return;
    g_rsize = size;
    bad1(b, size == 1 ? 1 : size == 2 ? 2 : 3);
}

static int cmp_piece(const void *a, const void *b)
{
    const Piece *x = a, *y = b;
    return x->off < y->off ? -1 : x->off > y->off;
}

int fmt_as86(Obj *o, Bytes *out)
{
    int ns = o->nsecs;
    if (o->is64 || o->arch != ARCH_X86 || o->bits == 64) {
        fprintf(stderr, "cly: as86 output is only supported for 16 and 32-bit x86\n");
        return 0;
    }
    i64 *base = xcalloc((size_t)ns + 1, sizeof(i64));
    int *seg = xcalloc((size_t)ns + 1, sizeof(int));
    i64 len[3] = {0, 0, 0};
    for (int i = 0; i < ns; i++) {
        seg[i] = seg_of(o->secs[i]);
        base[i] = len[seg[i]];
        len[seg[i]] += o->secs[i]->size;
    }
    Bytes img[2] = {{0}, {0}};
    for (int g = 0; g < 2; g++) {
        for (i64 k = 0; k < len[g]; k++) bad1(&img[g], 0);
        for (int i = 0; i < ns; i++) {
            Sec *s = o->secs[i];
            if (seg[i] == g && s->data.n) memcpy(img[g].p + base[i], s->data.p, s->data.n);
        }
    }
    Bytes str = {0}, syms = {0};
    {
        const char *mod = o->src ? o->src : "cly";
        const char *sl = strrchr(mod, '/');
        const char *dot = strrchr(mod, '.');
        size_t n = dot && (!sl || dot > sl + 1) ? (size_t)(dot - mod) : strlen(mod);
        badd(&str, mod, n);
        bad1(&str, 0);
    }
    int *ord = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    int *symidx = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    for (int i = 0; i < o->nsyms; i++) { ord[i] = i; symidx[i] = -1; }
    for (int i = 1; i < o->nsyms; i++) {
        int k = ord[i], j = i - 1;
        while (j >= 0 && o->syms[ord[j]]->defline > o->syms[k]->defline) { ord[j + 1] = ord[j]; j--; }
        ord[j + 1] = k;
    }
    u32 nsyms = 0;
    i64 symlen = 0;
    for (int oi = 0; oi < o->nsyms; oi++) {
        int i = ord[oi];
        Sym *s = o->syms[i];
        int glob = s->global || s->ext || s->common || (!s->defined && s->referenced);
        int undef = s->ext || s->common || !s->defined;
        if (!s->name[0] || s->import) continue;
        if (!s->defined && !s->ext && !s->common && !s->referenced) continue;
        const char *nm = sym_mangle(s, glob);
        u32 strpos = (u32)str.n;
        badd(&str, nm, strlen(nm) + 1);
        u32 flags;
        i64 val;
        if (s->common) { flags = 3 | 0x40; val = s->commonsize; }
        else if (undef) { flags = 15 | 0x40; val = 0; }
        else if (s->isequ || s->sec < 0) { flags = 0x10; val = s->isequ ? s->eqv.n : s->off; if (glob) flags |= 0x80; }
        else {
            int g = seg[s->sec];
            flags = g == 0 ? 0 : 3;
            val = base[s->sec] + s->off;
            if (g == 2) val += len[1];
            if (glob) flags |= 0x80;
            if (!strcmp(s->name, "..start")) flags |= 0x100;
        }
        if (val == 0) { symlen += 4; }
        else if (val >= 0 && val <= 255) { flags |= 1 << 14; symlen += 5; }
        else if (val >= 0 && val <= 65535) { flags |= 2 << 14; symlen += 6; }
        else { flags |= 3 << 14; symlen += 8; }
        w16(&syms, strpos);
        w16(&syms, flags);
        switch (flags & (3 << 14)) {
        case 1 << 14: bad1(&syms, (u8)val); break;
        case 2 << 14: w16(&syms, (u32)val); break;
        case 3 << 14: w32(&syms, (u32)val); break;
        }
        symidx[i] = (int)nsyms++;
    }
    Piece *pc[2] = {NULL, NULL};
    int np[2] = {0, 0};
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        Val *v = &r->v;
        int g = seg[r->sec];
        if (g == 2) continue;
        if (r->kind > RK_REL || (r->size != 1 && r->size != 2 && r->size != 4)) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("this kind of reference cannot be written to an as86 object file");
            continue;
        }
        if (v->neg >= 0 || (v->pos >= 0 && v->ext)) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("expression cannot be expressed as an as86 relocation");
            continue;
        }
        Piece p;
        memset(&p, 0, sizeof p);
        p.off = base[r->sec] + r->off;
        p.size = r->size;
        p.rel = r->kind == RK_REL;
        p.add = v->n;
        if (p.rel && r->relbase) p.add -= r->relbase - (r->off + r->size);
        if (v->ext) {
            int k = -1;
            for (int j = 0; j < o->nsyms; j++) if (o->syms[j] == v->ext) k = j;
            if (k < 0 || symidx[k] < 0) { err("symbol `%s' has no as86 symbol", v->ext->name); continue; }
            p.type = 2;
            p.number = symidx[k];
        } else if (v->pos >= 0) {
            int tg = seg[v->pos];
            p.type = 1;
            p.add += base[v->pos];
            if (tg == 2) { p.number = 3; p.add += len[1]; }
            else p.number = tg == 0 ? 0 : 3;
        } else continue;
        pc[g] = xrealloc(pc[g], sizeof(Piece) * (size_t)(np[g] + 1));
        pc[g][np[g]++] = p;
    }
    if (g_errors) return 0;
    for (int g = 0; g < 2; g++) if (np[g] > 1) qsort(pc[g], (size_t)np[g], sizeof(Piece), cmp_piece);
    i64 bsslen = len[2];
    i64 datalen = len[1];
    u32 segsize = 0;
    int seglen = 0;
    if (len[0] > 65535) { segsize |= 0x03000000u; seglen += 4; } else { segsize |= 0x02000000u; seglen += 2; }
    if (datalen > 65535) { segsize |= 0xC0000000u; seglen += 4; } else { segsize |= 0x80000000u; seglen += 2; }
    w32(out, 0x000186A3);
    bad1(out, 0x2A);
    w32(out, (u32)(27 + symlen + seglen + (i64)str.n));
    w32(out, (u32)(len[0] + datalen + bsslen));
    w16(out, (u32)str.n);
    w16(out, 0);
    w32(out, 0x55555555);
    w32(out, segsize);
    if (segsize & 0x01000000u) w32(out, (u32)len[0]); else w16(out, (u32)len[0]);
    if (segsize & 0x40000000u) w32(out, (u32)(datalen + bsslen)); else w16(out, (u32)(datalen + bsslen));
    w16(out, nsyms);
    badd(out, syms.p, syms.n);
    badd(out, str.p, str.n);
    g_rsize = -1;
    for (int g = 0; g < 2; g++) {
        bad1(out, 0x20 + (g == 0 ? 0 : 3));
        i64 pos = 0;
        for (int k = 0; k <= np[g]; k++) {
            i64 end = k < np[g] ? pc[g][k].off : len[g];
            while (pos < end) {
                i64 n = end - pos > 64 ? 64 : end - pos;
                bad1(out, (u8)(0x40 | (n & 0x3F)));
                badd(out, img[g].p + pos, (size_t)n);
                pos += n;
            }
            if (k == np[g]) break;
            Piece *p = &pc[g][k];
            set_rsize(out, p->size);
            if (p->type == 1) {
                bad1(out, (u8)(0x80 | (p->rel ? 0x20 : 0) | p->number));
                if (g_rsize == 2) w16(out, (u32)p->add); else w32(out, (u32)p->add);
            } else {
                u32 s = (u32)p->add;
                u32 c = s > 65535 ? 3 : s > 255 ? 2 : s > 0 ? 1 : 0;
                bad1(out, (u8)(0xC0 | (p->rel ? 0x20 : 0) | (p->number > 255 ? 4 : 0) | c));
                if (p->number > 255) w16(out, (u32)p->number); else bad1(out, (u8)p->number);
                if (c == 1) bad1(out, (u8)p->add);
                else if (c == 2) w16(out, (u32)p->add);
                else if (c == 3) w32(out, (u32)p->add);
            }
            pos = p->off + p->size;
        }
    }
    if (bsslen > 65535) { bad1(out, 0x13); w32(out, (u32)bsslen); }
    else if (bsslen > 255) { bad1(out, 0x12); w16(out, (u32)bsslen); }
    else if (bsslen) { bad1(out, 0x11); bad1(out, (u8)bsslen); }
    bad1(out, 0);
    return 1;
}
