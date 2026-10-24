/* Small static linker: joins ELF relocatable objects (x86-64, x32, i386, ARM64, RISC-V 64) into one static ELF program. */
#include "cly.h"

typedef struct { const u8 *d; size_t n; int is64, mach; const char *name; } EF;

static u16 g16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 g32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u64 g64(const u8 *p) { return (u64)g32(p) | (u64)g32(p + 4) << 32; }

typedef struct { u32 name, type; u64 flags, off, size; u32 link, info; u64 align, entsize; } Sh;
typedef struct { u32 name; int bind, type; u32 shndx; u64 value, size; } Sy;

static Sh get_sh(const EF *f, u64 shoff, int ent, int i)
{
    const u8 *p = f->d + shoff + (u64)i * (u64)ent;
    Sh s;
    s.name = g32(p);
    s.type = g32(p + 4);
    if (f->is64) {
        s.flags = g64(p + 8); s.off = g64(p + 24); s.size = g64(p + 32); s.link = g32(p + 40); s.info = g32(p + 44); s.align = g64(p + 48); s.entsize = g64(p + 56);
    } else {
        s.flags = g32(p + 8); s.off = g32(p + 16); s.size = g32(p + 20); s.link = g32(p + 24); s.info = g32(p + 28); s.align = g32(p + 32); s.entsize = g32(p + 36);
    }
    return s;
}

static Sy get_sy(const EF *f, const u8 *base, u32 i)
{
    Sy y;
    if (f->is64) {
        const u8 *p = base + (u64)i * 24;
        y.name = g32(p); y.bind = p[4] >> 4; y.type = p[4] & 15; y.shndx = g16(p + 6); y.value = g64(p + 8); y.size = g64(p + 16);
    } else {
        const u8 *p = base + (u64)i * 16;
        y.name = g32(p); y.value = g32(p + 4); y.size = g32(p + 8); y.bind = p[12] >> 4; y.type = p[12] & 15; y.shndx = g16(p + 14);
    }
    return y;
}

typedef struct {
    int file, idx, cls;
    char *name;
    u64 size, align, flags;
    const u8 *data;
    int nobits, inc;
    u64 out;
    u64 off;
} ISec;

typedef struct {
    EF f;
    Sh *sh;
    int nsh;
    int symsec;
    const u8 *syms, *strs;
    u32 nsyms;
    int *secmap;
    int *gotslot;
    ISec **isec;
} Obj1;

typedef struct {
    char *name;
    int file, defined, weak, common;
    u32 shndx;
    u64 value, csize, calign, abs;
    int isabs;
    int gslot;
    u64 addr;
    int special;
} GSym;

static GSym *gs;
static int ngs, capgs;
static int *ghash;
static int ghcap;

static u32 hstr(const char *s) { u32 h = 5381; while (*s) h = h * 33 + (u8)*s++; return h; }

static void ghash_build(void)
{
    free(ghash);
    ghcap = 1;
    while (ghcap < ngs * 2 + 16) ghcap <<= 1;
    ghash = xmalloc(sizeof(int) * (size_t)ghcap);
    for (int i = 0; i < ghcap; i++) ghash[i] = -1;
    for (int i = 0; i < ngs; i++) {
        u32 h = hstr(gs[i].name) & (u32)(ghcap - 1);
        while (ghash[h] >= 0) h = (h + 1) & (u32)(ghcap - 1);
        ghash[h] = i;
    }
}

static int gfind(const char *nm)
{
    u32 h = hstr(nm) & (u32)(ghcap - 1);
    while (ghash[h] >= 0) {
        if (!strcmp(gs[ghash[h]].name, nm)) return ghash[h];
        h = (h + 1) & (u32)(ghcap - 1);
    }
    return -1;
}

static int gadd(const char *nm)
{
    int i = ngs < capgs ? -2 : -1;
    (void)i;
    if (ngs == capgs) { capgs = capgs ? capgs * 2 : 256; gs = xrealloc(gs, sizeof(GSym) * (size_t)capgs); }
    memset(&gs[ngs], 0, sizeof(GSym));
    gs[ngs].name = xstrdup(nm);
    gs[ngs].gslot = -1;
    return ngs++;
}

static int lderr;

static void lerr(const char *fmt, ...)
{
    va_list ap;
    fputs("cly: ld: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    lderr++;
}

static u64 al(u64 v, u64 a) { return a > 1 ? (v + a - 1) / a * a : v; }

static void put_le(u8 *p, u64 v, int n) { for (int i = 0; i < n; i++) p[i] = (u8)(v >> (8 * i)); }

static int fits_s(i64 v, int bits) { return v >= -((i64)1 << (bits - 1)) && v < ((i64)1 << (bits - 1)); }
static int fits_u(i64 v, int bits) { return v >= 0 && v < ((i64)1 << bits); }

int ld_link(char **files, int nfiles, const char *outpath)
{
    lderr = 0;
    for (int i = 0; i < nfiles; i++) {
        if (!strcmp(files[i], outpath)) { lerr("the output `%s' is also an input file", outpath); return 0; }
    }
    {
        size_t n0 = 0;
        char *b0 = read_file(files[0], &n0);
        if (b0 && ld_is_coff(b0, n0)) { free(b0); return ld_link_coff(files, nfiles, outpath); }
        free(b0);
    }
    ngs = capgs = 0;
    gs = NULL;
    Obj1 *O = xcalloc((size_t)nfiles + 1, sizeof(Obj1));
    int mach = 0, is64 = -1;
    for (int fi = 0; fi < nfiles; fi++) {
        Obj1 *o = &O[fi];
        size_t n = 0;
        errno = 0;
        char *buf = read_file(files[fi], &n);
        if (!buf) { lerr("cannot open `%s': %s", files[fi], errno ? strerror(errno) : "unreadable"); return 0; }
        o->f.d = (const u8 *)buf;
        o->f.n = n;
        o->f.name = files[fi];
        if (n < 52 || memcmp(buf, "\x7f" "ELF", 4) || (buf[4] == 2 && n < 64)) { lerr("`%s' is not an ELF object file (assemble it with -o first)", files[fi]); return 0; }
        if (buf[4] != 1 && buf[4] != 2) { lerr("`%s' has an unknown ELF class", files[fi]); return 0; }
        o->f.is64 = buf[4] == 2;
        if (buf[5] != 1) { lerr("`%s' is not little endian", files[fi]); return 0; }
        if (g16(o->f.d + 16) != 1) { lerr("`%s' is not a relocatable object", files[fi]); return 0; }
        o->f.mach = g16(o->f.d + 18);
        if (o->f.mach != 62 && o->f.mach != 3 && o->f.mach != 183 && o->f.mach != 243) { lerr("`%s': machine type %d is not supported", files[fi], o->f.mach); return 0; }
        if (o->f.mach == 243 && !o->f.is64) { lerr("`%s': 32-bit RISC-V is not supported by the built-in linker", files[fi]); return 0; }
        if (fi == 0) { mach = o->f.mach; is64 = o->f.is64; }
        else if (mach != o->f.mach || is64 != o->f.is64) { lerr("`%s' is for a different architecture than `%s'", files[fi], files[0]); return 0; }
        u64 shoff = o->f.is64 ? g64(o->f.d + 40) : g32(o->f.d + 32);
        int ent = g16(o->f.d + (o->f.is64 ? 58 : 46));
        int nsh = g16(o->f.d + (o->f.is64 ? 60 : 48));
        if (!shoff || !nsh || ent < (o->f.is64 ? 64 : 40) || shoff > n || (u64)nsh * (u64)ent > n - shoff) { lerr("`%s' has no usable section table", files[fi]); return 0; }
        int shstrndx = g16(o->f.d + (o->f.is64 ? 62 : 50));
        if (shstrndx >= nsh) { lerr("`%s' has a bad section name table", files[fi]); return 0; }
        o->nsh = nsh;
        o->sh = xmalloc(sizeof(Sh) * (size_t)nsh);
        for (int i = 0; i < nsh; i++) {
            o->sh[i] = get_sh(&o->f, shoff, ent, i);
            if (o->sh[i].type != 8 && (o->sh[i].off > n || o->sh[i].size > n - o->sh[i].off)) { lerr("`%s' is truncated", files[fi]); return 0; }
            if (o->sh[i].align > ((u64)1 << 20) || (o->sh[i].size > ((u64)1 << 30))) { lerr("`%s' has a section that is too large", files[fi]); return 0; }
        }
        {
            Sh *st = &o->sh[shstrndx];
            if (st->type == 8 || !st->size || o->f.d[st->off + st->size - 1]) { lerr("`%s' has a bad section name table", files[fi]); return 0; }
            for (int i = 0; i < nsh; i++) if (o->sh[i].name >= st->size) { lerr("`%s' has a bad section name", files[fi]); return 0; }
        }
        o->symsec = -1;
        for (int i = 0; i < nsh; i++) if (o->sh[i].type == 2) o->symsec = i;
        if (o->symsec >= 0) {
            Sh *ss = &o->sh[o->symsec];
            o->syms = o->f.d + ss->off;
            o->nsyms = (u32)(ss->size / (o->f.is64 ? 24 : 16));
            if (ss->link >= (u32)nsh || o->sh[ss->link].type == 8 || !o->sh[ss->link].size || o->f.d[o->sh[ss->link].off + o->sh[ss->link].size - 1]) { lerr("`%s' has a bad symbol name table", files[fi]); return 0; }
            o->strs = o->f.d + o->sh[ss->link].off;
            u64 strsz = o->sh[ss->link].size;
            for (u32 si = 0; si < o->nsyms; si++) {
                Sy y = get_sy(&o->f, o->syms, si);
                if (y.name >= strsz) { lerr("`%s' has a bad symbol name", files[fi]); return 0; }
                if (y.shndx >= (u32)nsh && y.shndx < 0xff00) { lerr("`%s' has a symbol in a missing section", files[fi]); return 0; }
            }
        }
        o->secmap = xcalloc((size_t)nsh + 1, sizeof(int));
        o->isec = xcalloc((size_t)nsh + 1, sizeof(ISec *));
        o->gotslot = xmalloc(sizeof(int) * (size_t)(o->nsyms + 1));
        for (u32 i = 0; i <= o->nsyms; i++) o->gotslot[i] = -1;
    }
    int x32 = mach == 62 && !is64;
    int ptr = is64 ? 8 : 4;
    ISec *secs = NULL;
    int nsecs = 0;
    for (int fi = 0; fi < nfiles; fi++) {
        Obj1 *o = &O[fi];
        const char *shstr = (const char *)o->f.d + o->sh[g16(o->f.d + (o->f.is64 ? 62 : 50))].off;
        for (int i = 1; i < o->nsh; i++) {
            Sh *s = &o->sh[i];
            if (!(s->flags & 2)) continue;
            if (s->type != 1 && s->type != 8 && s->type != 14 && s->type != 15 && s->type != 16 && s->type != 7) continue;
            const char *nm = shstr + s->name;
            if (!strncmp(nm, ".eh_frame", 9) || !strncmp(nm, ".note.GNU-stack", 15) || s->type == 7) continue;
            if (s->flags & 0x400) { lerr("`%s': thread local storage is not supported by the built-in linker", o->f.name); return 0; }
            secs = xrealloc(secs, sizeof(ISec) * (size_t)(nsecs + 1));
            ISec *is = &secs[nsecs];
            memset(is, 0, sizeof *is);
            is->file = fi;
            is->idx = i;
            is->name = xstrdup(nm);
            is->size = s->size;
            is->align = s->align ? s->align : 1;
            is->flags = s->flags;
            is->nobits = s->type == 8;
            is->data = s->type == 8 ? o->f.d : o->f.d + s->off;
            is->cls = (s->flags & 4) ? 0 : (s->flags & 1) ? (is->nobits ? 3 : 2) : 1;
            o->secmap[i] = nsecs + 1;
            nsecs++;
        }
    }
    for (int fi = 0; fi < nfiles; fi++)
        for (int i = 1; i < O[fi].nsh; i++) if (O[fi].secmap[i]) O[fi].isec[i] = &secs[O[fi].secmap[i] - 1];
    for (int fi = 0; fi < nfiles; fi++) {
        Obj1 *o = &O[fi];
        for (u32 si = 1; si < o->nsyms; si++) {
            Sy y = get_sy(&o->f, o->syms, si);
            if (y.bind == 0 || y.type == 3 || y.type == 4) continue;
            const char *nm = (const char *)o->strs + y.name;
            if (!*nm) continue;
            if (!ghash) { ghcap = 16; ghash = xmalloc(sizeof(int) * 16); for (int k = 0; k < 16; k++) ghash[k] = -1; }
            int gi = gfind(nm);
            if (gi < 0) { gi = gadd(nm); ghash_build(); }
            GSym *g = &gs[gi];
            if (y.shndx == 0) continue;
            int weak = y.bind == 2;
            if (y.shndx == 0xfff2) {
                if (!g->defined || g->common) {
                    if (!g->common || y.size > g->csize) { g->csize = y.size; }
                    if (y.value > g->calign) g->calign = y.value;
                    g->common = 1;
                    g->defined = 1;
                    g->file = fi;
                }
                continue;
            }
            if (g->defined && !g->common && !g->weak && !weak) { lerr("multiple definition of `%s' (in `%s' and `%s')", nm, files[g->file], files[fi]); continue; }
            if (g->defined && !g->common && !g->weak && weak) continue;
            if (g->defined && !g->common && g->weak && weak) continue;
            g->defined = 1;
            g->common = 0;
            g->weak = weak;
            g->file = fi;
            g->shndx = y.shndx;
            g->value = y.value;
            g->isabs = y.shndx == 0xfff1;
            if (y.shndx >= 0xff00 && y.shndx != 0xfff1) { lerr("`%s': symbol `%s' has an unsupported section index", files[fi], nm); continue; }
            if (y.shndx != 0xfff1 && y.shndx < (u32)o->nsh && !o->isec[y.shndx]) {
                g->defined = 0;
            }
        }
    }
    if (lderr) return 0;
    static const char *const specials[] = { "__executable_start", "__ehdr_start", "etext", "_etext", "__etext", "edata", "_edata", "__bss_start", "end", "_end", "_GLOBAL_OFFSET_TABLE_", NULL };
    for (int si = 0; specials[si]; si++) {
        int gi = gfind(specials[si]);
        if (gi >= 0 && !gs[gi].defined) { gs[gi].defined = 1; gs[gi].special = si + 1; }
    }
    /* GOT slot discovery and checks on relocation types */
    int ngot = 0, needbase = 0;
    for (int fi = 0; fi < nfiles; fi++) {
        Obj1 *o = &O[fi];
        for (int i = 1; i < o->nsh; i++) {
            Sh *rs = &o->sh[i];
            if (rs->type != 4 && rs->type != 9) continue;
            if (rs->info >= (u32)o->nsh || !o->isec[rs->info]) continue;
            int rela = rs->type == 4;
            int rsz = o->f.is64 ? (rela ? 24 : 16) : (rela ? 12 : 8);
            u64 cnt = rs->size / (u64)rsz;
            for (u64 k = 0; k < cnt; k++) {
                const u8 *p = o->f.d + rs->off + k * (u64)rsz;
                u32 type, sym;
                if (o->f.is64) { u64 info = g64(p + 8); type = (u32)info; sym = (u32)(info >> 32); }
                else { u32 info = g32(p + 4); type = info & 255; sym = info >> 8; }
                int needgot = 0;
                if (mach == 62 && (type == 9 || type == 41 || type == 42 || type == 3 || type == 26)) needgot = 1;
                if (mach == 3 && (type == 3 || type == 10 || type == 9 || type == 43)) needgot = 1;
                if (mach == 183 && (type == 311 || type == 312 || type == 313 || type == 309 || type == 310)) needgot = 1;
                if (mach == 243 && type == 20) needgot = 1;
                if (mach == 62 && type == 25) needbase = 1;
                if (!needgot || sym >= o->nsyms) continue;
                if ((mach == 62 && (type == 26)) || (mach == 3 && (type == 10 || type == 9))) { needbase = 1; continue; }
                Sy y = get_sy(&o->f, o->syms, sym);
                int *slot;
                if (y.bind != 0 && *((const char *)o->strs + y.name)) {
                    int gi = gfind((const char *)o->strs + y.name);
                    slot = &gs[gi].gslot;
                } else slot = &o->gotslot[sym];
                if (*slot < 0) *slot = ngot++;
            }
        }
    }
    int hasgot = ngot > 0 || needbase;
    for (int si = 0; si < ngs; si++) if (gs[si].special == 11) hasgot = 1;
    u64 base = mach == 3 ? 0x08048000u : 0x400000u;
    if (mach == 183 || mach == 243) base = 0x400000u;
    u64 page = 0x1000;
    int ehsz = is64 ? 64 : 52, phsz = is64 ? 56 : 32;
    int nph = 3;
    u64 off = (u64)ehsz + (u64)phsz * (u64)nph;
    u64 end_rx = 0;
    for (int cls = 0; cls < 2; cls++) {
        for (int i = 0; i < nsecs; i++) {
            if (secs[i].cls != cls) continue;
            off = al(off, secs[i].align);
            secs[i].off = off;
            secs[i].out = base + off;
            off += secs[i].size;
        }
        if (cls == 0) end_rx = off;
    }
    u64 seg1end = off;
    u64 doff = al(seg1end, 16);
    u64 daddr_base = al(base + seg1end, page) + (doff & (page - 1));
    if (doff == seg1end && doff == 0) doff = 0;
    u64 cur = doff;
    u64 data_start = doff;
    for (int i = 0; i < nsecs; i++) {
        if (secs[i].cls != 2) continue;
        cur = al(cur, secs[i].align);
        secs[i].off = cur;
        secs[i].out = daddr_base + (cur - doff);
        cur += secs[i].size;
    }
    u64 gotoff = 0, gotaddr = 0;
    if (hasgot) {
        cur = al(cur, (u64)ptr);
        gotoff = cur;
        gotaddr = daddr_base + (cur - doff);
        cur += (u64)(ngot + 3) * (u64)ptr;
    }
    u64 dataend = cur;
    for (int i = 0; i < nsecs; i++) {
        if (secs[i].cls != 3) continue;
        cur = al(cur, secs[i].align);
        secs[i].off = cur;
        secs[i].out = daddr_base + (cur - doff);
        cur += secs[i].size;
    }
    for (int si = 0; si < ngs; si++) {
        if (!gs[si].common) continue;
        cur = al(cur, gs[si].calign ? gs[si].calign : 1);
        gs[si].addr = daddr_base + (cur - doff);
        cur += gs[si].csize;
    }
    u64 memend = cur;
    int hasdata = memend > data_start;
    if (!hasdata) nph = 2;
    for (int si = 0; si < ngs; si++) {
        GSym *g = &gs[si];
        switch (g->special) {
        case 1: case 2: g->addr = base; break;
        case 3: case 4: case 5: g->addr = base + end_rx; break;
        case 6: case 7: g->addr = daddr_base + (dataend - doff); break;
        case 8: g->addr = daddr_base + (dataend - doff); break;
        case 9: case 10: g->addr = daddr_base + (memend - doff); break;
        case 11: g->addr = gotaddr; break;
        }
    }
    if (memend > ((u64)1 << 31)) { lerr("the program would be larger than 2 GB"); return 0; }
    u64 filesz2 = dataend - doff;
    size_t total = (size_t)(hasdata ? doff + filesz2 : seg1end);
    if (hasdata && filesz2 == 0) total = (size_t)doff;
    u8 *img = xcalloc(total + 16, 1);
    for (int i = 0; i < nsecs; i++) {
        if (secs[i].nobits || secs[i].cls == 3 || !secs[i].size) continue;
        memcpy(img + secs[i].off, secs[i].data, (size_t)secs[i].size);
    }
    int *gotfill = NULL;
    (void)gotfill;
    /* resolve every undefined name now */
    for (int fi = 0; fi < nfiles; fi++) {
        Obj1 *o = &O[fi];
        for (u32 si = 1; si < o->nsyms; si++) {
            Sy y = get_sy(&o->f, o->syms, si);
            if (y.shndx != 0 || y.bind == 0) continue;
            const char *nm = (const char *)o->strs + y.name;
            if (!*nm) continue;
            int gi = gfind(nm);
            if (gi >= 0 && gs[gi].defined) continue;
            if (y.bind == 2) continue;
            int seen = 0;
            for (int fj = 0; fj < fi && !seen; fj++) {
                Obj1 *p = &O[fj];
                for (u32 sj = 1; sj < p->nsyms; sj++) { Sy z = get_sy(&p->f, p->syms, sj); if (z.shndx == 0 && z.bind && !strcmp((const char *)p->strs + z.name, nm)) { seen = 1; break; } }
            }
            if (!seen) lerr("undefined reference to `%s' (in `%s')", nm, files[fi]);
        }
    }
    if (lderr) return 0;
    for (int fi = 0; fi < nfiles; fi++) {
        Obj1 *o = &O[fi];
        const char *shstr = (const char *)o->f.d + o->sh[g16(o->f.d + (o->f.is64 ? 62 : 50))].off;
        for (int i = 1; i < o->nsh; i++) {
            Sh *rs = &o->sh[i];
            if (rs->type != 4 && rs->type != 9) continue;
            if (rs->info >= (u32)o->nsh || !o->isec[rs->info]) continue;
            ISec *tsec = o->isec[rs->info];
            if (tsec->nobits) continue;
            int rela = rs->type == 4;
            int rsz = o->f.is64 ? (rela ? 24 : 16) : (rela ? 12 : 8);
            u64 cnt = rs->size / (u64)rsz;
            for (u64 k = 0; k < cnt; k++) {
                const u8 *p = o->f.d + rs->off + k * (u64)rsz;
                u64 roff;
                u32 type, sym;
                i64 addend = 0;
                if (o->f.is64) {
                    roff = g64(p);
                    u64 info = g64(p + 8);
                    type = (u32)info;
                    sym = (u32)(info >> 32);
                    if (rela) addend = (i64)g64(p + 16);
                } else {
                    roff = g32(p);
                    u32 info = g32(p + 4);
                    type = info & 255;
                    sym = info >> 8;
                    if (rela) addend = (int32_t)g32(p + 8);
                }
                if (roff >= tsec->size) { lerr("`%s': relocation outside its section", o->f.name); continue; }
                u8 *loc = img + tsec->off + roff;
                u64 P = tsec->out + roff;
                u64 S = 0;
                int symdef = 1;
                const char *snm = "";
                int gslot = -1;
                if (sym) {
                    if (sym >= o->nsyms) { lerr("`%s': bad symbol index in relocation", o->f.name); continue; }
                    Sy y = get_sy(&o->f, o->syms, sym);
                    snm = (const char *)o->strs + y.name;
                    int gi = -1;
                    if (y.bind != 0 && *snm) gi = gfind(snm);
                    if (gi >= 0) {
                        GSym *g = &gs[gi];
                        gslot = g->gslot;
                        if (!g->defined) { S = 0; symdef = 0; }
                        else if (g->special || g->common) S = g->addr;
                        else if (g->isabs) S = g->value;
                        else {
                            ISec *ds = O[g->file].isec[g->shndx];
                            if (!ds) { lerr("`%s' is defined in a section that was dropped", snm); continue; }
                            S = ds->out + g->value;
                        }
                    } else {
                        gslot = o->gotslot[sym];
                        if (y.shndx == 0xfff1) S = y.value;
                        else if (y.shndx == 0) { S = 0; symdef = 0; }
                        else if (y.shndx < (u32)o->nsh && o->isec[y.shndx]) S = o->isec[y.shndx]->out + y.value;
                        else if (y.shndx < (u32)o->nsh) S = 0;
                        else { lerr("`%s': unsupported symbol section", o->f.name); continue; }
                    }
                }
                (void)symdef;
                u64 G = gslot >= 0 ? gotaddr + (u64)(gslot + 3) * (u64)ptr : 0;
                if (gslot >= 0) {
                    u8 *slotp = img + gotoff + (u64)(gslot + 3) * (u64)ptr;
                    put_le(slotp, S, ptr);
                }
                if (!rela) {
                    int nb = 4;
                    if (mach == 3 && (type == 20 || type == 21)) nb = 2;
                    if (mach == 3 && (type == 22 || type == 23)) nb = 1;
                    u64 v = 0;
                    for (int b = 0; b < nb; b++) v |= (u64)loc[b] << (8 * b);
                    addend = nb == 4 ? (int32_t)v : nb == 2 ? (int16_t)v : (int8_t)v;
                }
                i64 val;
                int nb = 0;
                const char *bad = NULL;
                if (mach == 62) {
                    switch (type) {
                    case 0: continue;
                    case 1: val = (i64)(S + (u64)addend); nb = 8; break;
                    case 2: case 4: val = (i64)(S + (u64)addend - P); nb = 4; if (!fits_s(val, 32)) bad = "PC32"; break;
                    case 3: val = (i64)(G + (u64)addend); nb = 4; break;
                    case 9: case 41: case 42: val = (i64)(G + (u64)addend - P); nb = 4; if (!fits_s(val, 32)) bad = "GOTPCREL"; break;
                    case 10: val = (i64)(S + (u64)addend); nb = 4; if (!fits_u(val, 32)) bad = "32"; break;
                    case 11: val = (i64)(S + (u64)addend); nb = 4; if (!fits_s(val, 32)) bad = "32S"; break;
                    case 12: val = (i64)(S + (u64)addend); nb = 2; break;
                    case 13: val = (i64)(S + (u64)addend - P); nb = 2; break;
                    case 14: val = (i64)(S + (u64)addend); nb = 1; break;
                    case 15: val = (i64)(S + (u64)addend - P); nb = 1; break;
                    case 24: val = (i64)(S + (u64)addend - P); nb = 8; break;
                    case 25: val = (i64)(S + (u64)addend - gotaddr); nb = 8; break;
                    case 26: val = (i64)(gotaddr + (u64)addend - P); nb = 4; break;
                    default: lerr("`%s': unsupported x86-64 relocation type %u", o->f.name, type); continue;
                    }
                    if (x32 && type == 1) nb = 4;
                } else if (mach == 3) {
                    switch (type) {
                    case 0: continue;
                    case 1: val = (i64)(S + (u64)addend); nb = 4; break;
                    case 2: case 4: val = (i64)(S + (u64)addend - P); nb = 4; break;
                    case 3: case 43: val = (i64)(G + (u64)addend - gotaddr); nb = 4; break;
                    case 9: val = (i64)(S + (u64)addend - gotaddr); nb = 4; break;
                    case 10: val = (i64)(gotaddr + (u64)addend - P); nb = 4; break;
                    case 20: val = (i64)(S + (u64)addend); nb = 2; break;
                    case 21: val = (i64)(S + (u64)addend - P); nb = 2; break;
                    case 22: val = (i64)(S + (u64)addend); nb = 1; break;
                    case 23: val = (i64)(S + (u64)addend - P); nb = 1; break;
                    default: lerr("`%s': unsupported i386 relocation type %u", o->f.name, type); continue;
                    }
                } else if (mach == 183) {
                    u32 ins = g32(loc);
                    i64 X = (i64)(S + (u64)addend);
                    i64 PC = X - (i64)P;
                    switch (type) {
                    case 0: continue;
                    case 257: val = X; nb = 8; break;
                    case 258: val = X; nb = 4; break;
                    case 259: val = X; nb = 2; break;
                    case 260: val = PC; nb = 8; break;
                    case 261: val = PC; nb = 4; break;
                    case 262: val = PC; nb = 2; break;
                    case 282: case 283:
                        if (PC & 3 || !fits_s(PC >> 2, 26)) { lerr("`%s': branch to `%s' is out of range or unaligned", o->f.name, snm); continue; }
                        ins = (ins & 0xfc000000u) | ((u32)(PC >> 2) & 0x3ffffffu); put_le(loc, ins, 4); continue;
                    case 280: case 273:
                        if (PC & 3 || !fits_s(PC >> 2, 19)) { lerr("`%s': reference to `%s' is out of range or unaligned", o->f.name, snm); continue; }
                        ins = (ins & 0xff00001fu) | (((u32)(PC >> 2) & 0x7ffffu) << 5); put_le(loc, ins, 4); continue;
                    case 279:
                        if (PC & 3 || !fits_s(PC >> 2, 14)) { lerr("`%s': test branch to `%s' is out of range", o->f.name, snm); continue; }
                        ins = (ins & 0xfff8001fu) | (((u32)(PC >> 2) & 0x3fffu) << 5); put_le(loc, ins, 4); continue;
                    case 274:
                        if (!fits_s(PC, 21)) { lerr("`%s': adr to `%s' is out of range", o->f.name, snm); continue; }
                        ins = (ins & 0x9f00001fu) | (((u32)PC & 3) << 29) | ((((u32)PC >> 2) & 0x7ffffu) << 5); put_le(loc, ins, 4); continue;
                    case 275: case 311: {
                        i64 pg = (i64)(((u64)X & ~(u64)0xfff)) - (i64)(P & ~(u64)0xfff);
                        if (type == 311) pg = (i64)((G + (u64)addend) & ~(u64)0xfff) - (i64)(P & ~(u64)0xfff);
                        if (!fits_s(pg >> 12, 21)) { lerr("`%s': adrp to `%s' is out of range", o->f.name, snm); continue; }
                        u32 imm = (u32)(pg >> 12);
                        ins = (ins & 0x9f00001fu) | ((imm & 3) << 29) | (((imm >> 2) & 0x7ffffu) << 5); put_le(loc, ins, 4); continue; }
                    case 277: case 286: case 284: case 285: case 299: case 312: {
                        u64 v2 = type == 312 ? (G + (u64)addend) : (u64)X;
                        v2 &= 0xfff;
                        int sh = type == 284 ? 1 : type == 285 ? 2 : type == 286 ? 3 : type == 299 ? 4 : type == 312 ? 3 : 0;
                        ins = (ins & ~(0xfffu << 10)) | ((u32)(v2 >> sh) << 10); put_le(loc, ins, 4); continue; }
                    case 263: case 264: ins = (ins & ~(0xffffu << 5)) | (((u32)X & 0xffffu) << 5); put_le(loc, ins, 4); continue;
                    case 265: case 266: ins = (ins & ~(0xffffu << 5)) | (((u32)((u64)X >> 16) & 0xffffu) << 5); put_le(loc, ins, 4); continue;
                    case 267: case 268: ins = (ins & ~(0xffffu << 5)) | (((u32)((u64)X >> 32) & 0xffffu) << 5); put_le(loc, ins, 4); continue;
                    case 269: ins = (ins & ~(0xffffu << 5)) | (((u32)((u64)X >> 48) & 0xffffu) << 5); put_le(loc, ins, 4); continue;
                    default: lerr("`%s': unsupported ARM64 relocation type %u", o->f.name, type); continue;
                    }
                } else {
                    i64 X = (i64)(S + (u64)addend);
                    i64 PC = X - (i64)P;
                    u32 ins = g32(loc);
                    switch (type) {
                    case 0: case 51: case 43: continue;
                    case 1: val = X; nb = 4; break;
                    case 2: val = X; nb = 8; break;
                    case 35: put_le(loc, (u64)(g64(loc) + (u64)X), 8); continue;
                    case 36: put_le(loc, (u64)(g64(loc) - (u64)X), 8); continue;
                    case 16:
                        if (PC & 1 || !fits_s(PC, 13)) { lerr("`%s': branch to `%s' is out of range", o->f.name, snm); continue; }
                        ins &= 0x01fff07fu;
                        ins |= (u32)(((PC >> 12) & 1) << 31) | (u32)(((PC >> 5) & 0x3f) << 25) | (u32)(((PC >> 1) & 0xf) << 8) | (u32)(((PC >> 11) & 1) << 7);
                        put_le(loc, ins, 4); continue;
                    case 17:
                        if (PC & 1 || !fits_s(PC, 21)) { lerr("`%s': jump to `%s' is out of range", o->f.name, snm); continue; }
                        ins &= 0xfffu;
                        ins |= (u32)(((PC >> 20) & 1) << 31) | (u32)(((PC >> 1) & 0x3ff) << 21) | (u32)(((PC >> 11) & 1) << 20) | (u32)(((PC >> 12) & 0xff) << 12);
                        put_le(loc, ins, 4); continue;
                    case 18: case 19: {
                        if (!fits_s(PC + 0x800, 32)) { lerr("`%s': call to `%s' is out of range", o->f.name, snm); continue; }
                        u32 hi = (u32)((PC + 0x800) >> 12), lo = (u32)PC & 0xfff;
                        put_le(loc, (ins & 0xfffu) | (hi << 12), 4);
                        u32 i2 = g32(loc + 4);
                        put_le(loc + 4, (i2 & 0xfffffu) | (lo << 20), 4);
                        continue; }
                    case 23: case 20: {
                        i64 pc2 = type == 20 ? (i64)(G + (u64)addend) - (i64)P : PC;
                        if (!fits_s(pc2 + 0x800, 32)) { lerr("`%s': pc relative reference to `%s' is out of range", o->f.name, snm); continue; }
                        put_le(loc, (ins & 0xfffu) | (((u32)(pc2 + 0x800) >> 12) << 12), 4); continue; }
                    case 24: case 25: {
                        u64 hiaddr = S;
                        int found = 0;
                        i64 d = 0;
                        for (int j = 1; j < o->nsh && !found; j++) {
                            Sh *hs = &o->sh[j];
                            if (hs->type != 4 || hs->info >= (u32)o->nsh || o->isec[hs->info] != tsec) continue;
                            u64 c2 = hs->size / 24;
                            for (u64 q = 0; q < c2; q++) {
                                const u8 *pp = o->f.d + hs->off + q * 24;
                                u32 ty2 = (u32)g64(pp + 8);
                                if ((ty2 != 23 && ty2 != 20) || tsec->out + g64(pp) != hiaddr) continue;
                                u32 s2 = (u32)(g64(pp + 8) >> 32);
                                if (s2 >= o->nsyms) { lerr("`%s': bad symbol index in relocation", o->f.name); continue; }
                                Sy y2 = get_sy(&o->f, o->syms, s2);
                                u64 S2 = 0;
                                const char *n2 = (const char *)o->strs + y2.name;
                                int g2 = (y2.bind != 0 && *n2) ? gfind(n2) : -1;
                                if (g2 >= 0) { GSym *gg = &gs[g2]; if (gg->special || gg->common) S2 = gg->addr; else if (gg->isabs) S2 = gg->value; else if (O[gg->file].isec[gg->shndx]) S2 = O[gg->file].isec[gg->shndx]->out + gg->value; }
                                else if (y2.shndx < (u32)o->nsh && o->isec[y2.shndx]) S2 = o->isec[y2.shndx]->out + y2.value;
                                d = (i64)(S2 + g64(pp + 16)) - (i64)hiaddr;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) { lerr("`%s': pc relative low part has no matching high part", o->f.name); continue; }
                        u32 lo = (u32)d & 0xfff;
                        if (type == 24) ins = (ins & 0xfffffu) | (lo << 20);
                        else ins = (ins & 0x1fff07fu) | ((lo >> 5) << 25) | ((lo & 0x1f) << 7);
                        put_le(loc, ins, 4); continue; }
                    case 26:
                        put_le(loc, (ins & 0xfffu) | ((((u32)X + 0x800) >> 12) << 12), 4); continue;
                    case 27: ins = (ins & 0xfffffu) | (((u32)X & 0xfff) << 20); put_le(loc, ins, 4); continue;
                    case 28: ins = (ins & 0x1fff07fu) | ((((u32)X & 0xfff) >> 5) << 25) | (((u32)X & 0x1f) << 7); put_le(loc, ins, 4); continue;
                    default: lerr("`%s': unsupported RISC-V relocation type %u", o->f.name, type); continue;
                    }
                }
                (void)shstr;
                if (bad) { lerr("`%s': relocation truncated to fit (%s against `%s')", o->f.name, bad, snm); continue; }
                put_le(loc, (u64)val, nb);
            }
        }
    }
    if (lderr) return 0;
    int ge = gfind("_start");
    if (ge < 0 || !gs[ge].defined) ge = gfind("start");
    u64 entry = base + (u64)ehsz + (u64)phsz * 3;
    int found = 0;
    if (ge >= 0 && gs[ge].defined && !gs[ge].special && !gs[ge].common) {
        GSym *g = &gs[ge];
        entry = g->isabs ? g->value : (O[g->file].isec[g->shndx] ? O[g->file].isec[g->shndx]->out + g->value : entry);
        found = 1;
    }
    if (!found) {
        fprintf(stderr, "cly: ld: warning: entry symbol `_start' not found, starting at the beginning of the code\n");
        for (int i = 0; i < nsecs; i++) if (secs[i].cls == 0) { entry = secs[i].out; break; }
    }
    u8 *h = img;
    h[0] = 0x7f; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
    h[4] = is64 ? 2 : 1; h[5] = 1; h[6] = 1;
    put_le(h + 16, 2, 2);
    put_le(h + 18, (u64)mach, 2);
    put_le(h + 20, 1, 4);
    int po;
    if (is64) {
        put_le(h + 24, entry, 8); put_le(h + 32, 64, 8); put_le(h + 40, 0, 8); put_le(h + 48, 0, 4);
        put_le(h + 52, 64, 2); put_le(h + 54, 56, 2); put_le(h + 56, (u64)nph, 2); put_le(h + 58, 64, 2); put_le(h + 60, 0, 2); put_le(h + 62, 0, 2);
        po = 64;
    } else {
        put_le(h + 24, entry, 4); put_le(h + 28, 52, 4); put_le(h + 32, 0, 4); put_le(h + 36, 0, 4);
        put_le(h + 40, 52, 2); put_le(h + 42, 32, 2); put_le(h + 44, (u64)nph, 2); put_le(h + 46, 40, 2); put_le(h + 48, 0, 2); put_le(h + 50, 0, 2);
        po = 52;
    }
    u8 *ph = h + po;
    if (is64) {
        put_le(ph, 1, 4); put_le(ph + 4, 5, 4); put_le(ph + 8, 0, 8); put_le(ph + 16, base, 8); put_le(ph + 24, base, 8);
        put_le(ph + 32, seg1end, 8); put_le(ph + 40, seg1end, 8); put_le(ph + 48, page, 8);
        ph += 56;
        if (hasdata) {
            put_le(ph, 1, 4); put_le(ph + 4, 6, 4); put_le(ph + 8, doff, 8); put_le(ph + 16, daddr_base, 8); put_le(ph + 24, daddr_base, 8);
            put_le(ph + 32, filesz2, 8); put_le(ph + 40, memend - doff, 8); put_le(ph + 48, page, 8);
            ph += 56;
        }
        put_le(ph, 0x6474e551, 4); put_le(ph + 4, 6, 4); put_le(ph + 48, 16, 8);
    } else {
        put_le(ph, 1, 4); put_le(ph + 4, 0, 4); put_le(ph + 8, base, 4); put_le(ph + 12, base, 4);
        put_le(ph + 16, seg1end, 4); put_le(ph + 20, seg1end, 4); put_le(ph + 24, 5, 4); put_le(ph + 28, page, 4);
        ph += 32;
        if (hasdata) {
            put_le(ph, 1, 4); put_le(ph + 4, doff, 4); put_le(ph + 8, daddr_base, 4); put_le(ph + 12, daddr_base, 4);
            put_le(ph + 16, filesz2, 4); put_le(ph + 20, memend - doff, 4); put_le(ph + 24, 6, 4); put_le(ph + 28, page, 4);
            ph += 32;
        }
        put_le(ph, 0x6474e551, 4); put_le(ph + 24, 6, 4); put_le(ph + 28, 16, 4);
    }
    errno = 0;
    if (write_file(outpath, img, total, 1)) { fprintf(stderr, "cly: cannot write `%s': %s\n", outpath, errno ? strerror(errno) : "unknown error"); return 0; }
    printf("%s: %zu bytes\n", outpath, total);
    return 1;
}
