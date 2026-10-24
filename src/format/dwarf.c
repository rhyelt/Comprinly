/* DWARF 4 debug information for ELF output (-g): a line table that maps code addresses to source lines, and one compile unit. Object files get the three sections with relocations, executables get final addresses. */
#include "cly.h"
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

enum { D_ABBREV, D_INFO, D_LINE, D_RANGES, D_N };
static const char *dnames[D_N] = { ".debug_abbrev", ".debug_info", ".debug_line", ".debug_ranges" };

typedef struct { size_t off; int w, tsec; i64 add; } DR;

typedef struct {
    Bytes b[D_N];
    DR *r[D_N];
    int nr[D_N];
    int resolved, asz;
} DW;

static void uleb(Bytes *b, u64 v)
{
    do {
        u8 c = v & 0x7f;
        v >>= 7;
        if (v) c |= 0x80;
        bad1(b, c);
    } while (v);
}

static void sleb(Bytes *b, i64 v)
{
    int more = 1;
    while (more) {
        u8 c = v & 0x7f;
        v >>= 7;
        if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) more = 0;
        else c |= 0x80;
        bad1(b, c);
    }
}

static void radd(DW *d, int which, size_t off, int w, int tsec, i64 add)
{
    d->r[which] = xrealloc(d->r[which], sizeof(DR) * (size_t)(d->nr[which] + 1));
    DR *r = &d->r[which][d->nr[which]++];
    r->off = off; r->w = w; r->tsec = tsec; r->add = add;
}

/* An address inside a code section. Executables get the final value, objects get a relocation. */
static void put_addr(DW *d, Obj *o, int which, int sec, i64 off)
{
    Bytes *b = &d->b[which];
    if (d->resolved) { badle(b, (u64)(o->secs[sec]->base + off), d->asz); return; }
    radd(d, which, b->n, d->asz, sec, off);
    badle(b, 0, d->asz);
}

/* An offset into another debug section. The target is a D_ number, stored as -1 - D_x so it cannot be mistaken for a code section. */
static void put_ref(DW *d, int which, int target, i64 off)
{
    Bytes *b = &d->b[which];
    if (d->resolved) { badle(b, (u64)off, 4); return; }
    radd(d, which, b->n, 4, -1 - target, off);
    badle(b, 0, 4);
}

static const Obj *g_sort_o;
static int dl_cmp(const void *pa, const void *pb)
{
    const DbgLine *a = pa, *b = pb;
    if (a->sec != b->sec) return a->sec < b->sec ? -1 : 1;
    if (a->off != b->off) return a->off < b->off ? -1 : 1;
    return a->seq < b->seq ? -1 : a->seq > b->seq;
}

static void build(Obj *o, DW *d)
{
    d->asz = (o->is64 && o->otype != T_X32) ? 8 : 4;
    int n = 0;
    DbgLine *e = xmalloc(sizeof(DbgLine) * (size_t)(o->ndbg + 1));
    for (int i = 0; i < o->ndbg; i++) {
        DbgLine *x = &o->dbg[i];
        if (x->sec < 0 || x->sec >= o->nsecs) continue;
        Sec *s = o->secs[x->sec];
        if (!s->exec || s->nobits) continue;
        e[n++] = *x;
    }
    g_sort_o = o;
    qsort(e, (size_t)n, sizeof *e, dl_cmp);
    if (!n) { free(e); return; }
    int nsec = 0, first = -1;
    for (int i = 0; i < n; i++) if (i == 0 || e[i].sec != e[i - 1].sec) { nsec++; if (first < 0) first = e[i].sec; }

    Bytes *ab = &d->b[D_ABBREV];
    for (int k = 1; k <= 2; k++) {
        uleb(ab, (u64)k); uleb(ab, 0x11); bad1(ab, 0);
        uleb(ab, 0x25); uleb(ab, 0x08);
        uleb(ab, 0x13); uleb(ab, 0x05);
        uleb(ab, 0x03); uleb(ab, 0x08);
        uleb(ab, 0x1b); uleb(ab, 0x08);
        uleb(ab, 0x10); uleb(ab, 0x17);
        uleb(ab, 0x11); uleb(ab, 0x01);
        if (k == 1) { uleb(ab, 0x12); uleb(ab, 0x07); }
        else { uleb(ab, 0x55); uleb(ab, 0x17); }
        uleb(ab, 0); uleb(ab, 0);
    }
    bad1(ab, 0);

    const char *fname = o->dbgnfiles ? o->dbgfiles[0] : "";
    char cwd[1024];
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, ".");
    Bytes *in = &d->b[D_INFO];
    badle(in, 0, 4);
    badle(in, 4, 2);
    put_ref(d, D_INFO, D_ABBREV, 0);
    bad1(in, (u8)d->asz);
    uleb(in, nsec == 1 ? 1 : 2);
    { char pr[64]; snprintf(pr, sizeof pr, "Comprinly %s", CLY_VERSION); badd(in, pr, strlen(pr) + 1); }
    badle(in, 0x8001, 2);
    badd(in, fname, strlen(fname) + 1);
    badd(in, cwd, strlen(cwd) + 1);
    put_ref(d, D_INFO, D_LINE, 0);
    if (nsec == 1) {
        put_addr(d, o, D_INFO, first, 0);
        badle(in, (u64)o->secs[first]->size, 8);
    } else {
        badle(in, 0, d->asz);
        put_ref(d, D_INFO, D_RANGES, 0);
    }
    bad1(in, 0);
    bputle(in, 0, in->n - 4, 4);

    Bytes *rg = &d->b[D_RANGES];
    if (nsec > 1) {
        for (int i = 0; i < n; i++) {
            if (i && e[i].sec == e[i - 1].sec) continue;
            put_addr(d, o, D_RANGES, e[i].sec, 0);
            put_addr(d, o, D_RANGES, e[i].sec, o->secs[e[i].sec]->size);
        }
        badle(rg, 0, d->asz); badle(rg, 0, d->asz);
    }

    Bytes *ln = &d->b[D_LINE];
    badle(ln, 0, 4);
    badle(ln, 4, 2);
    size_t hl_pos = ln->n;
    badle(ln, 0, 4);
    size_t hstart = ln->n;
    bad1(ln, 1); bad1(ln, 1); bad1(ln, 1);
    bad1(ln, (u8)-5); bad1(ln, 14); bad1(ln, 13);
    static const u8 oplen[12] = { 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
    badd(ln, oplen, 12);
    bad1(ln, 0);
    for (int i = 0; i < o->dbgnfiles; i++) {
        badd(ln, o->dbgfiles[i], strlen(o->dbgfiles[i]) + 1);
        uleb(ln, 0); uleb(ln, 0); uleb(ln, 0);
    }
    bad1(ln, 0);
    bputle(ln, hl_pos, ln->n - hstart, 4);
    int i = 0;
    while (i < n) {
        int sec = e[i].sec;
        int j = i;
        while (j < n && e[j].sec == sec) j++;
        bad1(ln, 0);
        uleb(ln, (u64)d->asz + 1);
        bad1(ln, 2);
        put_addr(d, o, D_LINE, sec, 0);
        i64 addr = 0;
        int line = 1, file = 1;
        for (int k = i; k < j; k++) {
            if (k > i && e[k].off == e[k - 1].off) continue;
            int f = e[k].file + 1;
            if (f != file) { bad1(ln, 4); uleb(ln, (u64)f); file = f; }
            i64 dl = e[k].line - line;
            if (dl) { bad1(ln, 3); sleb(ln, dl); line = e[k].line; }
            i64 da = e[k].off - addr;
            if (da) { bad1(ln, 2); uleb(ln, (u64)da); addr = e[k].off; }
            bad1(ln, 1);
        }
        i64 da = o->secs[sec]->size - addr;
        if (da > 0) { bad1(ln, 2); uleb(ln, (u64)da); }
        bad1(ln, 0); uleb(ln, 1); bad1(ln, 1);
        i = j;
    }
    bputle(ln, 0, ln->n - 4, 4);
    free(e);
}

static void dw_free(DW *d)
{
    for (int i = 0; i < D_N; i++) { free(d->r[i]); free(d->b[i].p); }
}

/* Adds the debug sections to an object file: new sections and the relocations that point into code. */
void dwarf_add_obj(Obj *o)
{
    if (!o->ndbg) return;
    DW d;
    memset(&d, 0, sizeof d);
    build(o, &d);
    int base = o->nsecs, idx[D_N], cnt = 0;
    for (int k = 0; k < D_N; k++) idx[k] = d.b[k].n ? base + cnt++ : -1;
    if (!cnt) { dw_free(&d); return; }
    o->secs = xrealloc(o->secs, sizeof(Sec *) * (size_t)(o->nsecs + cnt + 1));
    for (int k = 0; k < D_N; k++) {
        if (idx[k] < 0) continue;
        Sec *s = xcalloc(1, sizeof(Sec));
        s->name = xstrdup(dnames[k]);
        s->data = d.b[k];
        memset(&d.b[k], 0, sizeof d.b[k]);
        s->size = (i64)s->data.n;
        s->align = 1;
        s->idx = idx[k];
        s->declared = 1;
        s->order = 1000 + k;
        o->secs[idx[k]] = s;
    }
    o->nsecs += cnt;
    for (int k = 0; k < D_N; k++) {
        if (idx[k] < 0) continue;
        o->relocs = xrealloc(o->relocs, sizeof(Reloc) * (size_t)(o->nrelocs + d.nr[k] + 1));
        for (int m = 0; m < d.nr[k]; m++) {
            DR *x = &d.r[k][m];
            Reloc r;
            memset(&r, 0, sizeof r);
            r.sec = idx[k];
            r.off = (i64)x->off;
            r.size = x->w;
            r.kind = RK_ABS;
            r.v.neg = -1;
            r.v.pos = x->tsec >= 0 ? x->tsec : idx[-1 - x->tsec];
            r.v.n = x->add;
            o->relocs[o->nrelocs++] = r;
        }
        o->caprelocs = o->nrelocs;
    }
    dw_free(&d);
}

typedef struct { DW d; int name[D_N], off[D_N]; } DExe;

/* Executables: builds the sections with final addresses and names them in the section name table. */
void *dwarf_exe_begin(Obj *o, Bytes *shstr)
{
    if (!o->ndbg) return NULL;
    DExe *x = xcalloc(1, sizeof *x);
    x->d.resolved = 1;
    build(o, &x->d);
    int any = 0;
    for (int k = 0; k < D_N; k++) {
        x->name[k] = -1;
        if (!x->d.b[k].n) continue;
        x->name[k] = (int)shstr->n;
        badd(shstr, dnames[k], strlen(dnames[k]) + 1);
        any = 1;
    }
    if (!any) { dw_free(&x->d); free(x); return NULL; }
    return x;
}

int dwarf_exe_count(void *h)
{
    DExe *x = h;
    int c = 0;
    if (x) for (int k = 0; k < D_N; k++) if (x->name[k] >= 0) c++;
    return c;
}

void dwarf_exe_data(void *h, Bytes *f)
{
    DExe *x = h;
    if (!x) return;
    for (int k = 0; k < D_N; k++) {
        if (x->name[k] < 0) continue;
        x->off[k] = (int)f->n;
        badd(f, x->d.b[k].p, x->d.b[k].n);
    }
}

void dwarf_exe_headers(void *h, Bytes *f, int is64)
{
    DExe *x = h;
    if (!x) return;
    for (int k = 0; k < D_N; k++) {
        if (x->name[k] < 0) continue;
        badle(f, (u64)x->name[k], 4);
        badle(f, 1, 4);
        if (is64) {
            badle(f, 0, 8); badle(f, 0, 8); badle(f, (u64)x->off[k], 8); badle(f, x->d.b[k].n, 8);
            badle(f, 0, 4); badle(f, 0, 4); badle(f, 1, 8); badle(f, 0, 8);
        } else {
            badle(f, 0, 4); badle(f, 0, 4); badle(f, (u64)x->off[k], 4); badle(f, x->d.b[k].n, 4);
            badle(f, 0, 4); badle(f, 0, 4); badle(f, 1, 4); badle(f, 0, 4);
        }
    }
}

void dwarf_exe_end(void *h)
{
    DExe *x = h;
    if (!x) return;
    dw_free(&x->d);
    free(x);
}
