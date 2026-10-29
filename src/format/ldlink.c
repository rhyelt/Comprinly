/* Small linker: joins ELF relocatable objects (x86-64, x32, i386, ARM64, RISC-V 64) into a static ELF program, a dynamically linked program or a shared library (.so). */
#include "cly.h"

LdOpts g_ldo;

typedef struct { const u8 *d; size_t n; int is64, mach; const char *name; } EF;

static u16 g16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 g32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u64 g64(const u8 *p) { return (u64)g32(p) | (u64)g32(p + 4) << 32; }

typedef struct { u32 name, type; u64 flags, off, size; u32 link, info; u64 align, entsize; } Sh;
typedef struct { u32 name; int bind, type, other; u32 shndx; u64 value, size; } Sy;

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
        y.name = g32(p); y.bind = p[4] >> 4; y.type = p[4] & 15; y.other = p[5] & 3; y.shndx = g16(p + 6); y.value = g64(p + 8); y.size = g64(p + 16);
    } else {
        const u8 *p = base + (u64)i * 16;
        y.name = g32(p); y.value = g32(p + 4); y.size = g32(p + 8); y.bind = p[12] >> 4; y.type = p[12] & 15; y.other = p[13] & 3; y.shndx = g16(p + 14);
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
    int stype, vis;
    u64 ssize;
    int imp, lib, itype;
    u64 isize;
    int plt, dsi, copy, used, exported, uw, prot;
    u64 copyaddr;
    int nameoff;
} GSym;

typedef struct { char *name; int type, weak, prot; u64 size; } LSym;
typedef struct { char *path, *soname; LSym *s; int n; int nameoff; } Lib;

typedef struct { int gi, imp, abs; u64 S; } Slot;

typedef struct { char *name; u8 *buf; size_t n; } MemFile;
static MemFile *mfs;
static int nmfs;

void ld_mem_add(const char *name, const void *buf, size_t n)
{
    mfs = xrealloc(mfs, sizeof(MemFile) * (size_t)(nmfs + 1));
    mfs[nmfs].name = xstrdup(name);
    mfs[nmfs].buf = xmalloc(n + 1);
    memcpy(mfs[nmfs].buf, buf, n);
    mfs[nmfs].n = n;
    nmfs++;
}

static char *ld_read(const char *name, size_t *n)
{
    for (int i = 0; i < nmfs; i++) {
        if (strcmp(mfs[i].name, name)) continue;
        char *b = xmalloc(mfs[i].n + 1);
        memcpy(b, mfs[i].buf, mfs[i].n);
        b[mfs[i].n] = 0;
        *n = mfs[i].n;
        return b;
    }
    return read_file(name, n);
}

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
    if (!ghash) return -1;
    u32 h = hstr(nm) & (u32)(ghcap - 1);
    while (ghash[h] >= 0) {
        if (!strcmp(gs[ghash[h]].name, nm)) return ghash[h];
        h = (h + 1) & (u32)(ghcap - 1);
    }
    return -1;
}

static int gadd(const char *nm)
{
    if (ngs == capgs) { capgs = capgs ? capgs * 2 : 256; gs = xrealloc(gs, sizeof(GSym) * (size_t)capgs); }
    memset(&gs[ngs], 0, sizeof(GSym));
    gs[ngs].name = xstrdup(nm);
    gs[ngs].gslot = -1;
    gs[ngs].lib = -1;
    gs[ngs].plt = -1;
    gs[ngs].dsi = -1;
    int gi = ngs++;
    if (ghash) {
        if (ngs * 2 > ghcap) ghash_build();
        else {
            u32 h = hstr(gs[gi].name) & (u32)(ghcap - 1);
            while (ghash[h] >= 0) h = (h + 1) & (u32)(ghcap - 1);
            ghash[h] = gi;
        }
    }
    return gi;
}

static int lderr;
static int ldquiet;

static void lerr(const char *fmt, ...)
{
    va_list ap;
    lderr++;
    if (ldquiet) return;
    fputs("cly: ld: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static u64 al(u64 v, u64 a) { return a > 1 ? (v + a - 1) / a * a : v; }

static void put_le(u8 *p, u64 v, int n) { for (int i = 0; i < n; i++) p[i] = (u8)(v >> (8 * i)); }

static int fits_s(i64 v, int bits) { return v >= -((i64)1 << (bits - 1)) && v < ((i64)1 << (bits - 1)); }
static int fits_u(i64 v, int bits) { return v >= 0 && v < ((i64)1 << bits); }

static Obj1 *O;
static int nobj;
static Lib *libs;
static int nlibs;
static ISec *secs;
static int nsecs;
static int mach, is64, ptr, x32;
static int dyn, shared;
static Slot *slots;
static int nslot, capslot;
static int nplt, ndynrel, ncopyrel, textrel;
static u8 *img;
static u64 gotaddr, pltaddr, dynaddr;
static Bytes relb;
static char **pneed;
static int npneed;

static int new_slot(int gi, int imp, int abs)
{
    if (nslot == capslot) { capslot = capslot ? capslot * 2 : 64; slots = xrealloc(slots, sizeof(Slot) * (size_t)capslot); }
    slots[nslot].gi = gi;
    slots[nslot].imp = imp;
    slots[nslot].abs = abs;
    slots[nslot].S = 0;
    if (dyn && (imp || (shared && !abs))) ndynrel++;
    return nslot++;
}

enum { A_STATIC, A_RELATIVE, A_SYMABS, A_PLT, A_COPY, A_GOTLEA, A_ERR };
enum { C_OTHER, C_ABSPTR, C_ABSSM, C_PC, C_CALL, C_GOT };

static int rclass(u32 type)
{
    if (mach == 62) {
        switch (type) {
        case 1: return C_ABSPTR;
        case 10: case 11: case 12: case 14: return C_ABSSM;
        case 2: return C_PC;
        case 4: return C_CALL;
        case 3: case 9: case 41: case 42: return C_GOT;
        }
    } else if (mach == 3) {
        switch (type) {
        case 1: return C_ABSPTR;
        case 20: case 22: return C_ABSSM;
        case 2: return C_PC;
        case 4: return C_CALL;
        case 3: case 43: return C_GOT;
        }
    } else if (mach == 183) {
        switch (type) {
        case 257: return C_ABSPTR;
        case 258: case 259: case 263: case 264: case 265: case 266: case 267: case 268: case 269: return C_ABSSM;
        case 282: case 283: return C_CALL;
        case 260: case 261: case 262: case 280: case 273: case 279: case 274: case 275: case 277: case 284: case 285: case 286: case 299: return C_PC;
        case 311: case 312: return C_GOT;
        }
    } else {
        switch (type) {
        case 2: return C_ABSPTR;
        case 1: case 26: case 27: return C_ABSSM;
        case 18: case 19: return C_CALL;
        case 16: case 17: case 23: return C_PC;
        case 20: return C_GOT;
        }
    }
    return C_OTHER;
}

static const char *rname(u32 type)
{
    static char b[32];
    if (mach == 62) {
        switch (type) { case 2: return "R_X86_64_PC32"; case 10: return "R_X86_64_32"; case 11: return "R_X86_64_32S"; case 12: return "R_X86_64_16"; case 14: return "R_X86_64_8"; }
    } else if (mach == 3) {
        switch (type) { case 2: return "R_386_PC32"; case 20: return "R_386_16"; case 22: return "R_386_8"; }
    }
    snprintf(b, sizeof b, "relocation type %u", type);
    return b;
}

static int dyn_action(GSym *g, int isimp, u32 type, int callsite, int tcls, int isabs, int islea)
{
    int c = rclass(type);
    if (!dyn) return A_STATIC;
    if (isimp) {
        switch (c) {
        case C_ABSPTR: return A_SYMABS;
        case C_CALL: return A_PLT;
        case C_PC:
            if (mach == 62 || mach == 3) {
                if (callsite) return A_PLT;
                if (islea && mach == 62) return A_GOTLEA;
                if (g && g->itype == 2) return A_PLT;
                return shared ? A_ERR : A_COPY;
            }
            return A_ERR;
        case C_ABSSM:
            if (shared) return A_ERR;
            if (g && g->itype == 1) return A_COPY;
            return A_PLT;
        default: return A_STATIC;
        }
    }
    if (shared && !isabs) {
        if (c == C_ABSPTR) return A_RELATIVE;
        if (c == C_ABSSM) return A_ERR;
    }
    (void)tcls;
    return A_STATIC;
}

static u64 infer_size(GSym *g);

static u32 elf_hash(const char *n)
{
    u32 h = 0;
    while (*n) {
        h = (h << 4) + (u8)*n++;
        u32 g = h & 0xf0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static int load_lib(const char *path, const u8 *d, size_t n, int libmach, int libis64)
{
    EF f;
    f.d = d; f.n = n; f.is64 = libis64; f.mach = libmach; f.name = path;
    u64 shoff = f.is64 ? g64(d + 40) : g32(d + 32);
    int ent = g16(d + (f.is64 ? 58 : 46));
    int nsh = g16(d + (f.is64 ? 60 : 48));
    libs = xrealloc(libs, sizeof(Lib) * (size_t)(nlibs + 1));
    Lib *L = &libs[nlibs];
    memset(L, 0, sizeof *L);
    L->path = xstrdup(path);
    if (!shoff || !nsh || ent < (f.is64 ? 64 : 40) || shoff > n || (u64)nsh * (u64)ent > n - shoff) {
        nlibs++;
        return 1;
    }
    Sh *sh = xmalloc(sizeof(Sh) * (size_t)nsh);
    for (int i = 0; i < nsh; i++) {
        sh[i] = get_sh(&f, shoff, ent, i);
        if (sh[i].type != 8 && (sh[i].off > n || sh[i].size > n - sh[i].off)) sh[i].type = 0;
    }
    for (int i = 0; i < nsh; i++) {
        if (sh[i].type == 6 && sh[i].link < (u32)nsh && sh[sh[i].link].type == 3 && sh[sh[i].link].size) {
            const u8 *st = d + sh[sh[i].link].off;
            u64 ssz = sh[sh[i].link].size;
            int es = f.is64 ? 16 : 8;
            for (u64 k = 0; (k + 1) * (u64)es <= sh[i].size; k++) {
                const u8 *p = d + sh[i].off + k * (u64)es;
                u64 tag = f.is64 ? g64(p) : g32(p), val = f.is64 ? g64(p + 8) : g32(p + 4);
                if (tag == 0) break;
                if (tag == 14 && val < ssz && memchr(st + val, 0, (size_t)(ssz - val))) L->soname = xstrdup((const char *)st + val);
            }
        }
    }
    for (int i = 0; i < nsh; i++) {
        if (sh[i].type != 11 || sh[i].link >= (u32)nsh) continue;
        Sh *sy = &sh[i], *st = &sh[sy->link];
        if (st->type != 3 || !st->size) continue;
        u64 es = f.is64 ? 24 : 16;
        u64 cnt = sy->size / es;
        for (u64 k = 1; k < cnt; k++) {
            Sy y = get_sy(&f, d + sy->off, (u32)k);
            if (y.shndx == 0 || y.bind == 0 || y.name >= st->size) continue;
            const char *nm = (const char *)d + st->off + y.name;
            if (!memchr(nm, 0, (size_t)(st->size - y.name))) continue;
            L->s = xrealloc(L->s, sizeof(LSym) * (size_t)(L->n + 1));
            L->s[L->n].name = xstrdup(nm);
            L->s[L->n].type = y.type;
            L->s[L->n].weak = y.bind == 2;
            L->s[L->n].prot = y.other == 3;
            L->s[L->n].size = y.size;
            L->n++;
        }
    }
    free(sh);
    nlibs++;
    return 1;
}

static int lib_has(Lib *L, const char *name, int *type, u64 *size, int *prot)
{
    for (int i = 0; i < L->n; i++) {
        if (strcmp(L->s[i].name, name)) continue;
        *type = L->s[i].type;
        *size = L->s[i].size;
        *prot = L->s[i].prot;
        return 1;
    }
    return 0;
}

static int write_dynrel(u64 off, u32 sym, u32 type, i64 addend)
{
    if (is64) {
        badle(&relb, off, 8);
        badle(&relb, ((u64)sym << 32) | type, 8);
        badle(&relb, (u64)addend, 8);
    } else {
        badle(&relb, off, 4);
        badle(&relb, ((u32)sym << 8) | (type & 255), 4);
    }
    return 1;
}

static u32 t_relative(void) { return mach == 62 || mach == 3 ? 8 : mach == 183 ? 1027 : 3; }
static u32 t_glob(void) { return mach == 62 || mach == 3 ? 6 : mach == 183 ? 1025 : 2; }
static u32 t_abs(void) { return mach == 62 ? 1 : mach == 3 ? 1 : mach == 183 ? 257 : 2; }
static u32 t_copy(void) { return mach == 62 || mach == 3 ? 5 : mach == 183 ? 1024 : 4; }

static int ld_link_buf(char **files, int nfiles, Bytes *outb);

int ld_link(char **files, int nfiles, const char *outpath)
{
    lderr = 0;
    for (int i = 0; i < nfiles; i++) {
        if (!strcmp(files[i], outpath)) { lerr("the output `%s' is also an input file", outpath); return 0; }
    }
    {
        size_t n0 = 0;
        char *b0 = ld_read(files[0], &n0);
        if (b0 && ld_is_coff(b0, n0)) { free(b0); return ld_link_coff(files, nfiles, outpath); }
        free(b0);
    }
    Bytes out = {0};
    if (!ld_link_buf(files, nfiles, &out)) return 0;
    errno = 0;
    if (write_file(outpath, out.p, out.n, g_ldo.shared ? 0 : 1)) { fprintf(stderr, "cly: cannot write `%s': %s\n", outpath, errno ? strerror(errno) : "unknown error"); return 0; }
    printf("%s: %zu bytes\n", outpath, out.n);
    return 1;
}

int ld_link_mem(char **files, int nfiles, Bytes *outb)
{
    lderr = 0;
    return ld_link_buf(files, nfiles, outb);
}

static int ld_link_buf(char **files, int nfiles, Bytes *outb)
{
    ngs = capgs = 0;
    gs = NULL;
    free(ghash);
    ghash = NULL;
    libs = NULL;
    nlibs = 0;
    slots = NULL;
    nslot = capslot = 0;
    nplt = ndynrel = ncopyrel = textrel = 0;
    relb.n = 0;
    npneed = 0;
    pneed = NULL;
    int nall = nfiles;
    char **all = xmalloc(sizeof(char *) * (size_t)(nfiles + g_ldo.nneeded + 1));
    for (int i = 0; i < nfiles; i++) all[i] = files[i];
    for (int i = 0; i < g_ldo.nneeded; i++) {
        size_t nn = 0;
        char *b = ld_read(g_ldo.needed[i], &nn);
        if (b && nn > 20 && !memcmp(b, "\x7f" "ELF", 4) && g16((const u8 *)b + 16) == 3) all[nall++] = g_ldo.needed[i];
        else {
            pneed = xrealloc(pneed, sizeof(char *) * (size_t)(npneed + 1));
            pneed[npneed++] = g_ldo.needed[i];
        }
        free(b);
    }
    files = all;
    nfiles = nall;
    Obj1 *Oa = xcalloc((size_t)nfiles + 1, sizeof(Obj1));
    O = Oa;
    nobj = 0;
    mach = 0;
    is64 = -1;
    for (int fi = 0; fi < nfiles; fi++) {
        size_t n = 0;
        errno = 0;
        char *buf = ld_read(files[fi], &n);
        if (!buf) { lerr("cannot open `%s': %s", files[fi], errno ? strerror(errno) : "unreadable"); return 0; }
        if (n < 52 || memcmp(buf, "\x7f" "ELF", 4) || (buf[4] == 2 && n < 64)) {
            if (n > 13 && !memcmp(buf, "OUTPUT_FORMAT", 13)) lerr("`%s' is a linker script, not a shared library; give the real .so file", files[fi]);
            else lerr("`%s' is not an ELF object file (assemble it with -o first)", files[fi]);
            return 0;
        }
        if (buf[4] != 1 && buf[4] != 2) { lerr("`%s' has an unknown ELF class", files[fi]); return 0; }
        if (buf[5] != 1) { lerr("`%s' is not little endian", files[fi]); return 0; }
        int ftype = g16((const u8 *)buf + 16);
        int fmach = g16((const u8 *)buf + 18);
        int f64 = buf[4] == 2;
        if (ftype != 1 && ftype != 3) { lerr("`%s' is not a relocatable object or a shared library", files[fi]); return 0; }
        if (fmach != 62 && fmach != 3 && fmach != 183 && fmach != 243) { lerr("`%s': machine type %d is not supported", files[fi], fmach); return 0; }
        if (fmach == 243 && !f64) { lerr("`%s': 32-bit RISC-V is not supported by the built-in linker", files[fi]); return 0; }
        if (mach == 0) { mach = fmach; is64 = f64; }
        else if (mach != fmach || is64 != f64) { lerr("`%s' is for a different architecture than `%s'", files[fi], files[0]); return 0; }
        if (ftype == 3) {
            load_lib(files[fi], (const u8 *)buf, n, fmach, f64);
            continue;
        }
        Obj1 *o = &O[nobj++];
        o->f.d = (const u8 *)buf;
        o->f.n = n;
        o->f.name = files[fi];
        o->f.is64 = f64;
        o->f.mach = fmach;
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
    if (!nobj) { lerr("no object files to link"); return 0; }
    shared = g_ldo.shared;
    dyn = shared || g_ldo.dynamic || nlibs > 0 || g_ldo.nneeded > 0;
    x32 = mach == 62 && !is64;
    if (dyn && x32) { lerr("shared libraries and dynamic linking are not supported for x32 objects"); return 0; }
    ptr = is64 ? 8 : 4;
    secs = NULL;
    nsecs = 0;
    for (int fi = 0; fi < nobj; fi++) {
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
    for (int fi = 0; fi < nobj; fi++)
        for (int i = 1; i < O[fi].nsh; i++) if (O[fi].secmap[i]) O[fi].isec[i] = &secs[O[fi].secmap[i] - 1];
    for (int fi = 0; fi < nobj; fi++) {
        Obj1 *o = &O[fi];
        for (u32 si = 1; si < o->nsyms; si++) {
            Sy y = get_sy(&o->f, o->syms, si);
            if (y.bind == 0 || y.type == 3 || y.type == 4) continue;
            const char *nm = (const char *)o->strs + y.name;
            if (!*nm) continue;
            if (!ghash) { ghcap = 16; ghash = xmalloc(sizeof(int) * 16); for (int k = 0; k < 16; k++) ghash[k] = -1; }
            int gi = gfind(nm);
            if (gi < 0) gi = gadd(nm);
            GSym *g = &gs[gi];
            if (y.shndx == 0) { g->uw |= y.bind == 2 ? 1 : 2; continue; }
            int weak = y.bind == 2;
            if (y.shndx == 0xfff2) {
                if (!g->defined || g->common) {
                    if (!g->common || y.size > g->csize) { g->csize = y.size; }
                    if (y.value > g->calign) g->calign = y.value;
                    g->common = 1;
                    g->defined = 1;
                    g->file = fi;
                    g->stype = 1;
                    g->ssize = g->csize;
                }
                continue;
            }
            if (g->defined && !g->common && !g->weak && !weak) { lerr("multiple definition of `%s' (in `%s' and `%s')", nm, O[g->file].f.name, O[fi].f.name); continue; }
            if (g->defined && !g->common && !g->weak && weak) continue;
            if (g->defined && !g->common && g->weak && weak) continue;
            g->defined = 1;
            g->common = 0;
            g->weak = weak;
            g->file = fi;
            g->shndx = y.shndx;
            g->value = y.value;
            g->isabs = y.shndx == 0xfff1;
            g->stype = y.type;
            g->ssize = y.size;
            g->vis = y.other;
            if (y.shndx >= 0xff00 && y.shndx != 0xfff1) { lerr("`%s': symbol `%s' has an unsupported section index", o->f.name, nm); continue; }
            if (y.shndx != 0xfff1 && y.shndx < (u32)o->nsh && !o->isec[y.shndx]) {
                g->defined = 0;
            }
        }
    }
    if (lderr) return 0;
    static const char *const specials[] = { "__executable_start", "__ehdr_start", "etext", "_etext", "__etext", "edata", "_edata", "__bss_start", "end", "_end", "_GLOBAL_OFFSET_TABLE_", "_DYNAMIC", NULL };
    for (int si = 0; specials[si]; si++) {
        int gi = gfind(specials[si]);
        if (gi >= 0 && !gs[gi].defined) { gs[gi].defined = 1; gs[gi].special = si + 1; }
    }
    static const char *const arrs[] = { "__preinit_array_start", "__preinit_array_end", "__init_array_start", "__init_array_end", "__fini_array_start", "__fini_array_end", NULL };
    for (int si = 0; arrs[si]; si++) {
        int gi = gfind(arrs[si]);
        if (gi >= 0 && !gs[gi].defined) { gs[gi].defined = 1; gs[gi].special = 13 + si; }
    }
    for (int gi = 0; gi < ngs; gi++) {
        GSym *g = &gs[gi];
        if (g->defined) continue;
        int st = !strncmp(g->name, "__start_", 8), sp = !strncmp(g->name, "__stop_", 7);
        if (!st && !sp) continue;
        const char *nm = g->name + (st ? 8 : 7);
        int found = 0;
        if (isalpha((unsigned char)nm[0]) || nm[0] == '_') for (int i = 0; i < nsecs; i++) if (!strcmp(secs[i].name, nm)) found = 1;
        if (found) { g->defined = 1; g->special = st ? 19 : 20; }
    }
    for (int gi = 0; gi < ngs; gi++) {
        GSym *g = &gs[gi];
        if (g->defined || !dyn) continue;
        g->imp = 1;
        for (int li = 0; li < nlibs; li++) {
            int t, pr; u64 sz;
            if (lib_has(&libs[li], g->name, &t, &sz, &pr)) { g->prot = pr; g->lib = li; g->itype = t == 1 ? 1 : t == 2 || t == 10 ? 2 : 0; g->isize = sz; break; }
        }
    }
    for (int fi = 0; fi < nobj; fi++) {
        Obj1 *o = &O[fi];
        for (u32 si = 1; si < o->nsyms; si++) {
            Sy y = get_sy(&o->f, o->syms, si);
            if (y.shndx != 0 || y.bind == 0) continue;
            const char *nm = (const char *)o->strs + y.name;
            if (!*nm) continue;
            int gi = gfind(nm);
            if (gi >= 0 && gs[gi].defined) continue;
            if (y.bind == 2) continue;
            if (dyn && (shared || npneed > 0)) continue;
            if (dyn && gi >= 0 && gs[gi].lib >= 0) continue;
            int seen = 0;
            for (int fj = 0; fj < fi && !seen; fj++) {
                Obj1 *p = &O[fj];
                for (u32 sj = 1; sj < p->nsyms; sj++) { Sy z = get_sy(&p->f, p->syms, sj); if (z.shndx == 0 && z.bind && !strcmp((const char *)p->strs + z.name, nm)) { seen = 1; break; } }
            }
            if (!seen) lerr("undefined reference to `%s' (in `%s')", nm, o->f.name);
        }
    }
    if (lderr) return 0;
    int ngot = 0, needbase = 0;
    for (int fi = 0; fi < nobj; fi++) {
        Obj1 *o = &O[fi];
        for (int i = 1; i < o->nsh; i++) {
            Sh *rs = &o->sh[i];
            if (rs->type != 4 && rs->type != 9) continue;
            if (rs->info >= (u32)o->nsh || !o->isec[rs->info]) continue;
            ISec *tsec = o->isec[rs->info];
            int rela = rs->type == 4;
            int rsz = o->f.is64 ? (rela ? 24 : 16) : (rela ? 12 : 8);
            u64 cnt = rs->size / (u64)rsz;
            for (u64 k = 0; k < cnt; k++) {
                const u8 *p = o->f.d + rs->off + k * (u64)rsz;
                u32 type, sym;
                u64 roff;
                if (o->f.is64) { u64 info = g64(p + 8); type = (u32)info; sym = (u32)(info >> 32); roff = g64(p); }
                else { u32 info = g32(p + 4); type = info & 255; sym = info >> 8; roff = g32(p); }
                int needgot = 0;
                if (mach == 62 && (type == 9 || type == 41 || type == 42 || type == 3 || type == 26)) needgot = 1;
                if (mach == 3 && (type == 3 || type == 10 || type == 9 || type == 43)) needgot = 1;
                if (mach == 183 && (type == 311 || type == 312 || type == 313 || type == 309 || type == 310)) needgot = 1;
                if (mach == 243 && type == 20) needgot = 1;
                if (mach == 62 && type == 25) needbase = 1;
                if (sym >= o->nsyms) continue;
                Sy y = get_sy(&o->f, o->syms, sym);
                GSym *g = NULL;
                if (y.bind != 0 && *((const char *)o->strs + y.name)) {
                    int gi = gfind((const char *)o->strs + y.name);
                    if (gi >= 0) g = &gs[gi];
                }
                int isimp = dyn && g && g->imp;
                if (dyn && sym) {
                    int callsite = 0;
                    if (roff && roff <= tsec->size && !tsec->nobits) {
                        const u8 *c = tsec->data;
                        if (roff >= 1 && (c[roff - 1] == 0xe8 || c[roff - 1] == 0xe9)) callsite = 1;
                        if (roff >= 2 && c[roff - 2] == 0xff && (c[roff - 1] == 0x25 || c[roff - 1] == 0x15)) callsite = 1;
                    }
                    int islea = 0;
                    if (roff >= 2 && !tsec->nobits && roff <= tsec->size && tsec->data[roff - 2] == 0x8d && (tsec->data[roff - 1] & 0xc7) == 0x05) islea = 1;
                    int act = dyn_action(g, isimp, type, callsite, tsec->cls, g ? g->isabs : y.shndx == 0xfff1, islea);
                    if (act == A_GOTLEA && g) { g->used = 1; needgot = 1; }
                    if (act == A_RELATIVE || act == A_SYMABS) { ndynrel++; if (tsec->cls == 0) textrel = 1; }
                    if (act == A_SYMABS) g->used = 1;
                    if (act == A_PLT && g) { g->used = 1; if (g->plt < 0) g->plt = -2; }
                    if (act == A_COPY && g && g->prot) { lerr("`%s' is a protected data symbol of its library and cannot be copied into the program; reference it through the GOT ([rel %s wrt ..got]) instead", g->name, g->name); }
                    else if (act == A_COPY && g) { g->used = 1; g->copy = 1; }
                    if (act == A_ERR) {
                        if (isimp && g && rclass(type) == C_PC && !(mach == 62 || mach == 3)) lerr("`%s': %s against `%s' needs the GOT in a shared object or dynamic program; use :got: / GOT_HI20 forms", o->f.name, rname(type), g->name);
                        else if (isimp && rclass(type) == C_PC) lerr("`%s': %s against undefined `%s' cannot be used in a shared library; use `call %s wrt ..plt' for functions or `[rel %s wrt ..got]' for data", o->f.name, rname(type), g ? g->name : "?", g ? g->name : "?", g ? g->name : "?");
                        else lerr("`%s': %s against `%s' cannot be used in a shared library; use rip-relative addressing ([rel x], lea reg, [rel x]) instead of a 32-bit address", o->f.name, rname(type), g ? g->name : ((const char *)o->strs + y.name)[0] ? (const char *)o->strs + y.name : (y.shndx < 0xff00 && o->isec[y.shndx] && o->isec[y.shndx]->name ? o->isec[y.shndx]->name : "(section)"));
                    }
                }
                if (!needgot) continue;
                if ((mach == 62 && (type == 26)) || (mach == 3 && (type == 10 || type == 9))) { needbase = 1; continue; }
                int *slot;
                if (g) slot = &g->gslot;
                else slot = &o->gotslot[sym];
                if (isimp && g) g->used = 1;
                if (*slot < 0) *slot = new_slot(g ? (int)(g - gs) : -1, isimp, g ? g->isabs : y.shndx == 0xfff1);
            }
        }
    }
    if (lderr) return 0;
    for (int gi = 0; gi < ngs; gi++) {
        GSym *g = &gs[gi];
        if (g->plt != -2) continue;
        g->plt = nplt++;
        if (g->gslot < 0) g->gslot = new_slot(gi, 1, 0);
    }
    for (int gi = 0; gi < ngs; gi++) {
        GSym *g = &gs[gi];
        if (!g->copy) continue;
        if (!g->isize) { lerr("cannot copy the data symbol `%s' into the program: give the shared library that defines it as an input so its size is known", g->name); continue; }
        ncopyrel++;
        ndynrel++;
    }
    if (lderr) return 0;
    int hasgot = ngot > 0 || needbase || nslot > 0 || nplt > 0;
    for (int si = 0; si < ngs; si++) if (gs[si].special == 11) hasgot = 1;
    ngot = nslot;
    if (dyn && shared && mach == 3) for (int i = 0; i < nplt; i++) { ndynrel++; textrel = 1; }
    Bytes dynstr = {0}, dynsym = {0}, hashb = {0};
    int *needoff = NULL;
    int sonameoff = 0, rpathoff = 0;
    int ndsym = 0;
    int *dsorder = NULL;
    char **neededs = NULL;
    int nneeded = 0;
    Bytes interp = {0};
    int ndyn = 0;
    int pltent = mach == 183 || mach == 243 ? 16 : 6;
    u64 base = mach == 3 ? 0x08048000u : 0x400000u;
    if (dyn && shared) base = 0;
    u64 page = 0x1000;
    int ehsz = is64 ? 64 : 52, phsz = is64 ? 56 : 32;
    int nph = 3;
    if (dyn) nph = shared ? 4 : 6;
    u64 off = (u64)ehsz + (u64)phsz * (u64)nph;
    u64 a_interp = 0, a_hash = 0, a_dsym = 0, a_dstr = 0, a_rela = 0, a_plt = 0;
    u64 sz_hash = 0, sz_dsym = 0, sz_dstr = 0, sz_rela = 0, sz_plt = 0;
    int relent = is64 ? 24 : 8;
    if (dyn) {
        bad1(&dynstr, 0);
        bfill(&dynsym, 0, is64 ? 24 : 16);
        ndsym = 1;
        dsorder = xmalloc(sizeof(int) * (size_t)(ngs + 1));
        for (int gi = 0; gi < ngs; gi++) {
            GSym *g = &gs[gi];
            int exp = 0;
            if (g->defined && !g->special && (shared || g_ldo.export_dyn) && g->vis < 1 && !g->isabs) exp = 1;
            if (g->defined && !g->special && (shared || g_ldo.export_dyn) && g->vis == 3 && !g->isabs) exp = 1;
            if (g->copy) exp = 1;
            if (g->defined && g->isabs && shared && g->vis < 1) exp = 1;
            if (!exp && !(g->imp && g->used)) continue;
            g->exported = exp;
            g->dsi = ndsym++;
            dsorder[g->dsi - 1] = gi;
            g->nameoff = (int)dynstr.n;
            badd(&dynstr, g->name, strlen(g->name) + 1);
        }
        for (int li = 0; li < nlibs; li++) {
            const char *nm = libs[li].soname ? libs[li].soname : libs[li].path;
            neededs = xrealloc(neededs, sizeof(char *) * (size_t)(nneeded + 1));
            neededs[nneeded++] = (char *)nm;
        }
        for (int i = 0; i < npneed; i++) {
            neededs = xrealloc(neededs, sizeof(char *) * (size_t)(nneeded + 1));
            neededs[nneeded++] = pneed[i];
        }
        needoff = xmalloc(sizeof(int) * (size_t)(nneeded + 1));
        for (int i = 0; i < nneeded; i++) { needoff[i] = (int)dynstr.n; badd(&dynstr, neededs[i], strlen(neededs[i]) + 1); }
        if (g_ldo.soname) { sonameoff = (int)dynstr.n; badd(&dynstr, g_ldo.soname, strlen(g_ldo.soname) + 1); }
        if (g_ldo.rpath) { rpathoff = (int)dynstr.n; badd(&dynstr, g_ldo.rpath, strlen(g_ldo.rpath) + 1); }
        if (!shared) {
            const char *ip = g_ldo.interp;
            if (!ip) ip = mach == 62 ? "/lib64/ld-linux-x86-64.so.2" : mach == 3 ? "/lib/ld-linux.so.2" : mach == 183 ? "/lib/ld-linux-aarch64.so.1" : "/lib/ld-linux-riscv64-lp64d.so.1";
            badd(&interp, ip, strlen(ip) + 1);
        }
        int nb = ndsym < 4 ? 1 : (ndsym / 2) | 1;
        sz_hash = 4 * (2 + (u64)nb + (u64)ndsym);
        sz_dsym = (u64)ndsym * (u64)(is64 ? 24 : 16);
        sz_dstr = dynstr.n;
        sz_rela = (u64)ndynrel * (u64)relent;
        sz_plt = (u64)nplt * (u64)pltent;
        if (interp.n) { a_interp = off; off += interp.n; }
        off = al(off, 4); a_hash = off; off += sz_hash;
        off = al(off, 8); a_dsym = off; off += sz_dsym;
        a_dstr = off; off += sz_dstr;
        off = al(off, 8); a_rela = off; off += sz_rela;
        if (nplt) { off = al(off, 16); a_plt = off; off += sz_plt; }
        {
            int hi = 0, hf = 0, hp = 0;
            for (int i = 0; i < nsecs; i++) {
                if (secs[i].cls != 2) continue;
                if (!strncmp(secs[i].name, ".init_array", 11)) hi = 1;
                else if (!strncmp(secs[i].name, ".fini_array", 11)) hf = 1;
                else if (!strncmp(secs[i].name, ".preinit_array", 14)) hp = 1;
            }
            int ii = gfind("_init"), ff = gfind("_fini");
            int hin = ii >= 0 && gs[ii].defined && !gs[ii].special && !gs[ii].isabs;
            int hfi = ff >= 0 && gs[ff].defined && !gs[ff].special && !gs[ff].isabs;
            ndyn = nneeded + 5 + (g_ldo.soname ? 1 : 0) + (g_ldo.rpath ? 1 : 0) + (ndynrel ? 3 : 0) + (textrel ? 1 : 0) + hin + hfi + hp * 2 + hi * 2 + hf * 2 + (shared ? 0 : 1) + 1;
        }
        {
            int nb2 = nb;
            hashb.n = 0;
            badle(&hashb, (u64)nb2, 4);
            badle(&hashb, (u64)ndsym, 4);
            u32 *bk = xcalloc((size_t)nb2 + 1, 4), *ch = xcalloc((size_t)ndsym + 1, 4);
            for (int i = 1; i < ndsym; i++) {
                u32 h = elf_hash(gs[dsorder[i - 1]].name) % (u32)nb2;
                ch[i] = bk[h];
                bk[h] = (u32)i;
            }
            for (int i = 0; i < nb2; i++) badle(&hashb, bk[i], 4);
            for (int i = 0; i < ndsym; i++) badle(&hashb, ch[i], 4);
            free(bk);
            free(ch);
        }
    }
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
    u64 cur = doff;
    u64 data_start = doff;
    u64 a_dynamic = 0, sz_dynamic = 0;
    if (dyn) {
        cur = al(cur, 8);
        a_dynamic = cur;
        sz_dynamic = (u64)ndyn * (u64)(is64 ? 16 : 8);
        cur += sz_dynamic;
    }
    int *order = xmalloc(sizeof(int) * (size_t)(nsecs + 1));
    int nord = 0;
    {
        static const char *const pref[3] = { ".preinit_array", ".init_array", ".fini_array" };
        for (int k = 0; k < 3; k++) {
            int st = nord;
            size_t pl = strlen(pref[k]);
            for (int i = 0; i < nsecs; i++) if (secs[i].cls == 2 && !strncmp(secs[i].name, pref[k], pl)) order[nord++] = i;
            for (int a = st + 1; a < nord; a++) {
                int t = order[a];
                const char *tn = secs[t].name + pl;
                long tk = (tn[0] == '.' && isdigit((unsigned char)tn[1])) ? atol(tn + 1) : 0x7fffffffL;
                int b = a - 1;
                while (b >= st) {
                    const char *bn = secs[order[b]].name + pl;
                    long bk = (bn[0] == '.' && isdigit((unsigned char)bn[1])) ? atol(bn + 1) : 0x7fffffffL;
                    if (bk <= tk) break;
                    order[b + 1] = order[b];
                    b--;
                }
                order[b + 1] = t;
            }
        }
        for (int i = 0; i < nsecs; i++) {
            if (secs[i].cls != 2) continue;
            int isarr = !strncmp(secs[i].name, pref[0], strlen(pref[0])) || !strncmp(secs[i].name, pref[1], strlen(pref[1])) || !strncmp(secs[i].name, pref[2], strlen(pref[2]));
            if (isarr) continue;
            order[nord++] = i;
        }
    }
    u64 initarr = 0, initarr_sz = 0, finiarr = 0, finiarr_sz = 0, preinit = 0, preinit_sz = 0;
    for (int q = 0; q < nord; q++) {
        int i = order[q];
        cur = al(cur, secs[i].align);
        secs[i].off = cur;
        secs[i].out = daddr_base + (cur - doff);
        if (dyn) {
            if (!strncmp(secs[i].name, ".init_array", 11)) { if (!initarr) initarr = secs[i].out; initarr_sz = secs[i].out + secs[i].size - initarr; }
            else if (!strncmp(secs[i].name, ".fini_array", 11)) { if (!finiarr) finiarr = secs[i].out; finiarr_sz = secs[i].out + secs[i].size - finiarr; }
            else if (!strncmp(secs[i].name, ".preinit_array", 14)) { if (!preinit) preinit = secs[i].out; preinit_sz = secs[i].out + secs[i].size - preinit; }
        }
        cur += secs[i].size;
    }
    free(order);
    u64 gotoff = 0;
    gotaddr = 0;
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
    for (int gi = 0; gi < ngs; gi++) {
        if (!gs[gi].copy) continue;
        cur = al(cur, gs[gi].isize >= 16 ? 16 : 8);
        gs[gi].copyaddr = daddr_base + (cur - doff);
        cur += gs[gi].isize;
    }
    for (int si = 0; si < ngs; si++) {
        if (!gs[si].common) continue;
        cur = al(cur, gs[si].calign ? gs[si].calign : 1);
        gs[si].addr = daddr_base + (cur - doff);
        cur += gs[si].csize;
    }
    u64 memend = cur;
    int hasdata = memend > data_start || dyn;
    if (!hasdata) nph = 2;
    dynaddr = dyn ? daddr_base + (a_dynamic - doff) : 0;
    pltaddr = dyn ? base + a_plt : 0;
    for (int si = 0; si < ngs; si++) {
        GSym *g = &gs[si];
        switch (g->special) {
        case 1: case 2: g->addr = base; break;
        case 3: case 4: case 5: g->addr = base + end_rx; break;
        case 6: case 7: g->addr = daddr_base + (dataend - doff); break;
        case 8: g->addr = daddr_base + (dataend - doff); break;
        case 9: case 10: g->addr = daddr_base + (memend - doff); break;
        case 11: g->addr = gotaddr; break;
        case 12: g->addr = dynaddr; break;
        case 13: case 14: case 15: case 16: case 17: case 18: {
            static const char *const pf[3] = { ".preinit_array", ".init_array", ".fini_array" };
            const char *pr = pf[(g->special - 13) / 2];
            u64 lo = 0, hi = 0;
            int any = 0;
            for (int i = 0; i < nsecs; i++) {
                if (strncmp(secs[i].name, pr, strlen(pr)) || !secs[i].size) continue;
                if (!any || secs[i].out < lo) lo = secs[i].out;
                if (!any || secs[i].out + secs[i].size > hi) hi = secs[i].out + secs[i].size;
                any = 1;
            }
            if (!any) lo = hi = daddr_base + (dataend - doff);
            g->addr = (g->special - 13) % 2 ? hi : lo;
            break;
        }
        case 19: case 20: {
            const char *nm = g->name + (g->special == 19 ? 8 : 7);
            u64 lo = 0, hi = 0;
            int any = 0;
            for (int i = 0; i < nsecs; i++) {
                if (strcmp(secs[i].name, nm)) continue;
                if (!any || secs[i].out < lo) lo = secs[i].out;
                if (!any || secs[i].out + secs[i].size > hi) hi = secs[i].out + secs[i].size;
                any = 1;
            }
            g->addr = g->special == 19 ? lo : hi;
            break;
        }
        }
    }
    if (memend > ((u64)1 << 31)) { lerr("the program would be larger than 2 GB"); return 0; }
    u64 filesz2 = dataend - doff;
    size_t total = (size_t)(hasdata ? doff + filesz2 : seg1end);
    if (hasdata && filesz2 == 0) total = (size_t)doff;
    int shnum_v = 0;
    size_t shoff_v = 0;
    int withsh = dyn && (shared || g_debug);
    size_t shroom = withsh ? 4096 : 0;
    img = xcalloc(total + 16 + shroom, 1);
    for (int i = 0; i < nsecs; i++) {
        if (secs[i].nobits || secs[i].cls == 3 || !secs[i].size) continue;
        memcpy(img + secs[i].off, secs[i].data, (size_t)secs[i].size);
    }
    for (int fi = 0; fi < nobj; fi++) {
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
                if (!rela) {
                    int nb0 = 4;
                    if (mach == 3 && (type == 20 || type == 21)) nb0 = 2;
                    if (mach == 3 && (type == 22 || type == 23)) nb0 = 1;
                    u64 v0 = 0;
                    for (int b = 0; b < nb0; b++) v0 |= (u64)loc[b] << (8 * b);
                    addend = nb0 == 4 ? (int32_t)v0 : nb0 == 2 ? (int16_t)v0 : (int8_t)v0;
                }
                u64 S = 0;
                int symdef = 1;
                const char *snm = "";
                int gslot = -1;
                GSym *gg0 = NULL;
                int isabs0 = 0;
                if (sym) {
                    if (sym >= o->nsyms) { lerr("`%s': bad symbol index in relocation", o->f.name); continue; }
                    Sy y = get_sy(&o->f, o->syms, sym);
                    snm = (const char *)o->strs + y.name;
                    int gi = -1;
                    if (y.bind != 0 && *snm) gi = gfind(snm);
                    if (gi >= 0) {
                        GSym *g = &gs[gi];
                        gg0 = g;
                        gslot = g->gslot;
                        if (!g->defined) { S = 0; symdef = 0; }
                        else if (g->special || g->common) S = g->addr;
                        else if (g->isabs) { S = g->value; isabs0 = 1; }
                        else {
                            ISec *ds = O[g->file].isec[g->shndx];
                            if (!ds) { lerr("`%s' is defined in a section that was dropped", snm); continue; }
                            S = ds->out + g->value;
                        }
                    } else {
                        gslot = o->gotslot[sym];
                        if (y.shndx == 0xfff1) { S = y.value; isabs0 = 1; }
                        else if (y.shndx == 0) { S = 0; symdef = 0; }
                        else if (y.shndx < (u32)o->nsh && o->isec[y.shndx]) S = o->isec[y.shndx]->out + y.value;
                        else if (y.shndx < (u32)o->nsh) S = 0;
                        else { lerr("`%s': unsupported symbol section", o->f.name); continue; }
                    }
                }
                (void)symdef;
                u64 S0 = S;
                int act = A_STATIC;
                if (dyn && sym) {
                    int isimp = gg0 && gg0->imp;
                    int callsite = 0;
                    if (roff >= 1 && !tsec->nobits) {
                        if (tsec->data[roff - 1] == 0xe8 || tsec->data[roff - 1] == 0xe9) callsite = 1;
                        if (roff >= 2 && tsec->data[roff - 2] == 0xff && (tsec->data[roff - 1] == 0x25 || tsec->data[roff - 1] == 0x15)) callsite = 1;
                    }
                    int islea = 0;
                    if (roff >= 2 && !tsec->nobits && tsec->data[roff - 2] == 0x8d && (tsec->data[roff - 1] & 0xc7) == 0x05) islea = 1;
                    act = dyn_action(gg0, isimp, type, callsite, tsec->cls, isabs0, islea);
                    if (act == A_PLT && gg0) S = pltaddr + (u64)gg0->plt * (u64)pltent;
                    else if (act == A_COPY && gg0) S = gg0->copyaddr;
                    else if (act == A_GOTLEA && gg0 && gg0->gslot >= 0) { S = gotaddr + (u64)(gg0->gslot + 3) * (u64)ptr; loc[-2] = 0x8b; }
                    if (act == A_RELATIVE || act == A_SYMABS) {
                        u32 ty = act == A_RELATIVE ? t_relative() : t_abs();
                        write_dynrel(P, act == A_RELATIVE ? 0 : (u32)gg0->dsi, ty, (i64)(S + (u64)addend));
                    }
                }
                if (dyn && gg0 && gg0->copy && act != A_PLT) S = gg0->copyaddr;
                u64 G = gslot >= 0 ? gotaddr + (u64)(gslot + 3) * (u64)ptr : 0;
                if (gslot >= 0) {
                    u8 *slotp = img + gotoff + (u64)(gslot + 3) * (u64)ptr;
                    put_le(slotp, S0, ptr);
                    slots[gslot].S = S0;
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
                                if (ty2 == 20) {
                                    int gs2 = g2 >= 0 ? gs[g2].gslot : o->gotslot[s2];
                                    if (gs2 < 0) { lerr("`%s': GOT entry for pc relative low part is missing", o->f.name); continue; }
                                    S2 = gotaddr + (u64)(gs2 + 3) * (u64)ptr;
                                }
                                d = (i64)(S2 + (ty2 == 20 ? 0 : g64(pp + 16))) - (i64)hiaddr;
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
    if (dyn) {
        for (int i = 0; i < nslot; i++) {
            u64 sa = gotaddr + (u64)(i + 3) * (u64)ptr;
            Slot *sl = &slots[i];
            if (sl->imp) write_dynrel(sa, (u32)gs[sl->gi].dsi, t_glob(), 0);
            else if (shared && !sl->abs) write_dynrel(sa, 0, t_relative(), (i64)sl->S);
        }
        for (int gi = 0; gi < ngs; gi++) {
            GSym *g = &gs[gi];
            if (!g->copy) continue;
            write_dynrel(g->copyaddr, (u32)g->dsi, t_copy(), 0);
        }
        for (int gi = 0; gi < ngs; gi++) {
            GSym *g = &gs[gi];
            if (g->plt < 0) continue;
            u64 sa = gotaddr + (u64)(g->gslot + 3) * (u64)ptr;
            u64 pa = pltaddr + (u64)g->plt * (u64)pltent;
            u8 *pp = img + a_plt + (u64)g->plt * (u64)pltent;
            if (mach == 62) {
                pp[0] = 0xff; pp[1] = 0x25;
                put_le(pp + 2, sa - (pa + 6), 4);
            } else if (mach == 3) {
                pp[0] = 0xff; pp[1] = 0x25;
                put_le(pp + 2, sa, 4);
                if (shared) write_dynrel(pa + 2, 0, t_relative(), 0);
            } else if (mach == 183) {
                i64 pg = (i64)(sa & ~(u64)0xfff) - (i64)(pa & ~(u64)0xfff);
                u32 imm = (u32)(pg >> 12);
                put_le(pp, 0x90000010u | ((imm & 3) << 29) | (((imm >> 2) & 0x7ffffu) << 5), 4);
                put_le(pp + 4, 0xf9400211u | ((u32)((sa & 0xfff) >> 3) << 10), 4);
                put_le(pp + 8, 0xd61f0220u, 4);
                put_le(pp + 12, 0xd503201fu, 4);
            } else {
                i64 d = (i64)sa - (i64)pa;
                u32 hi = (u32)((d + 0x800) >> 12), lo = (u32)d & 0xfff;
                put_le(pp, 0x00000e17u | (hi << 12), 4);
                put_le(pp + 4, 0x000e3e03u | (lo << 20), 4);
                put_le(pp + 8, 0x000e0367u, 4);
                put_le(pp + 12, 0x00000013u, 4);
            }
        }
        if ((u64)(relb.n) != sz_rela) { lerr("internal error: dynamic relocation count mismatch (%zu bytes, expected %llu)", relb.n, (unsigned long long)sz_rela); return 0; }
        if (hasgot) put_le(img + gotoff, dynaddr, ptr);
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
    if (shared) entry = 0;
    else if (!found) {
        fprintf(stderr, "cly: ld: warning: entry symbol `_start' not found, starting at the beginning of the code\n");
        for (int i = 0; i < nsecs; i++) if (secs[i].cls == 0) { entry = secs[i].out; break; }
    }
    if (dyn) {
        u8 *dp = img + a_interp;
        if (interp.n) memcpy(dp, interp.p, interp.n);
        if (hashb.n) memcpy(img + a_hash, hashb.p, hashb.n);
        for (int i = 1; i < ndsym; i++) {
            GSym *g = &gs[dsorder[i - 1]];
            u8 *sp = img + a_dsym + (u64)i * (u64)(is64 ? 24 : 16);
            int bind = g->weak ? 2 : 1;
            int type = g->stype == 1 || g->stype == 2 ? g->stype : 0;
            u64 sv = 0, ssz = 0;
            u32 shn = 0;
            int other = 0;
            if (g->copy) {
                sv = g->copyaddr; ssz = g->isize; type = 1; shn = 1; bind = 1;
            } else if (g->defined) {
                shn = 1;
                ssz = g->ssize;
                {
                    ISec *ds0 = (g->isabs || g->special || g->common) ? NULL : O[g->file].isec[g->shndx];
                    if (g->vis == 3 || g->common || (!g->isabs && !g->special && (g->stype == 1 || (g->stype == 0 && ds0 && ds0->cls != 0)))) other = 3;
                }
                if (!ssz && type == 1 && !g->isabs && !g->common && !g->special) ssz = infer_size(g);
                if (g->isabs) { sv = g->value; shn = 0xfff1; }
                else if (g->special || g->common) sv = g->addr;
                else {
                    ISec *ds = O[g->file].isec[g->shndx];
                    sv = ds ? ds->out + g->value : 0;
                    if (ds && type == 0 && ds->cls == 0 && (ds->flags & 4)) type = 2;
                }
            } else {
                type = g->itype == 1 ? 1 : g->itype == 2 ? 2 : 0;
                if (g->uw == 1) bind = 2;
            }
            if (is64) {
                put_le(sp, (u64)g->nameoff, 4);
                sp[4] = (u8)((bind << 4) | type);
                sp[5] = (u8)other;
                put_le(sp + 6, shn, 2);
                put_le(sp + 8, sv, 8);
                put_le(sp + 16, ssz, 8);
            } else {
                put_le(sp, (u64)g->nameoff, 4);
                put_le(sp + 4, sv, 4);
                put_le(sp + 8, ssz, 4);
                sp[12] = (u8)((bind << 4) | type);
                sp[13] = (u8)other;
                put_le(sp + 14, shn, 2);
            }
        }
        if (dynstr.n) memcpy(img + a_dstr, dynstr.p, dynstr.n);
        if (relb.n) memcpy(img + a_rela, relb.p, relb.n);
        u8 *dd = img + a_dynamic;
        int di = 0;
#define DENT(t, v) do { put_le(dd + (u64)di * (u64)(is64 ? 16 : 8), (u64)(t), is64 ? 8 : 4); put_le(dd + (u64)di * (u64)(is64 ? 16 : 8) + (is64 ? 8 : 4), (u64)(v), is64 ? 8 : 4); di++; } while (0)
        for (int i = 0; i < nneeded; i++) DENT(1, needoff[i]);
        if (g_ldo.soname) DENT(14, sonameoff);
        if (g_ldo.rpath) DENT(29, rpathoff);
        DENT(4, base + a_hash);
        DENT(5, base + a_dstr);
        DENT(6, base + a_dsym);
        DENT(10, sz_dstr);
        DENT(11, is64 ? 24 : 16);
        if (ndynrel) {
            if (is64) { DENT(7, base + a_rela); DENT(8, sz_rela); DENT(9, 24); }
            else { DENT(17, base + a_rela); DENT(18, sz_rela); DENT(19, 8); }
        }
        if (textrel) DENT(22, 0);
        {
            int gi = gfind("_init");
            if (gi >= 0 && gs[gi].defined && !gs[gi].special && !gs[gi].isabs && O[gs[gi].file].isec[gs[gi].shndx]) DENT(12, O[gs[gi].file].isec[gs[gi].shndx]->out + gs[gi].value);
            gi = gfind("_fini");
            if (gi >= 0 && gs[gi].defined && !gs[gi].special && !gs[gi].isabs && O[gs[gi].file].isec[gs[gi].shndx]) DENT(13, O[gs[gi].file].isec[gs[gi].shndx]->out + gs[gi].value);
        }
        if (preinit) { DENT(32, preinit); DENT(33, preinit_sz); }
        if (initarr) { DENT(25, initarr); DENT(27, initarr_sz); }
        if (finiarr) { DENT(26, finiarr); DENT(28, finiarr_sz); }
        if (!shared) DENT(21, 0);
        DENT(0, 0);
        if (di > ndyn) { lerr("internal error: dynamic section overflow"); return 0; }
    }
    if (withsh) {
        typedef struct { const char *nm; u32 type; u64 flags, addr, off, size; u32 link, info; u64 align, ent; int nameoff; } SH;
        SH t[16];
        int nt = 0;
        memset(t, 0, sizeof t);
        nt = 1;
        int sy = 0;
        t[nt++] = (SH){ ".hash", 5, 2, base + a_hash, a_hash, sz_hash, 2, 0, 4, 4, 0 };
        sy = nt;
        t[nt++] = (SH){ ".dynsym", 11, 2, base + a_dsym, a_dsym, sz_dsym, 3, 1, 8, (u64)(is64 ? 24 : 16), 0 };
        t[nt++] = (SH){ ".dynstr", 3, 2, base + a_dstr, a_dstr, sz_dstr, 0, 0, 1, 0, 0 };
        if (sz_rela) t[nt++] = (SH){ is64 ? ".rela.dyn" : ".rel.dyn", is64 ? 4 : 9, 2, base + a_rela, a_rela, sz_rela, (u32)sy, 0, 8, (u64)relent, 0 };
        if (nplt) t[nt++] = (SH){ ".plt", 1, 6, base + a_plt, a_plt, sz_plt, 0, 0, 16, 0, 0 };
        {
            u64 lo = ~(u64)0, hi = 0;
            for (int i = 0; i < nsecs; i++) if (secs[i].cls == 0 || secs[i].cls == 1) { if (secs[i].off < lo) lo = secs[i].off; if (secs[i].off + secs[i].size > hi) hi = secs[i].off + secs[i].size; }
            if (lo != ~(u64)0 && hi > lo) t[nt++] = (SH){ ".text", 1, 6, base + lo, lo, hi - lo, 0, 0, 16, 0, 0 };
        }
        t[nt++] = (SH){ ".dynamic", 6, 3, dynaddr, a_dynamic, sz_dynamic, 3, 0, 8, (u64)(is64 ? 16 : 8), 0 };
        if (hasgot) t[nt++] = (SH){ ".got", 1, 3, gotaddr, gotoff, (u64)(ngot + 3) * (u64)ptr, 0, 0, (u64)ptr, (u64)ptr, 0 };
        {
            u64 lo = ~(u64)0, hi = 0;
            for (int i = 0; i < nsecs; i++) if (secs[i].cls == 2) { if (secs[i].off < lo) lo = secs[i].off; if (secs[i].off + secs[i].size > hi) hi = secs[i].off + secs[i].size; }
            if (lo != ~(u64)0 && hi > lo) t[nt++] = (SH){ ".data", 1, 3, daddr_base + (lo - doff), lo, hi - lo, 0, 0, 8, 0, 0 };
        }
        if (memend > dataend) t[nt++] = (SH){ ".bss", 8, 3, daddr_base + (dataend - doff), dataend, memend - dataend, 0, 0, 16, 0, 0 };
        Bytes shs = {0};
        bad1(&shs, 0);
        for (int i = 1; i < nt; i++) { t[i].nameoff = (int)shs.n; badd(&shs, t[i].nm, strlen(t[i].nm) + 1); }
        const char *sn = ".shstrtab";
        int snoff = (int)shs.n;
        badd(&shs, sn, strlen(sn) + 1);
        t[nt++] = (SH){ sn, 3, 0, 0, 0, shs.n, 0, 0, 1, 0, snoff };
        size_t sstart = al(total, 8);
        t[nt - 1].off = sstart;
        if (shs.n) memcpy(img + sstart, shs.p, shs.n);
        size_t shpos = al(sstart + shs.n, 8);
        for (int i = 0; i < nt; i++) {
            u8 *q = img + shpos + (size_t)i * (size_t)(is64 ? 64 : 40);
            if (is64) {
                put_le(q, (u64)t[i].nameoff, 4); put_le(q + 4, t[i].type, 4); put_le(q + 8, t[i].flags, 8); put_le(q + 16, t[i].addr, 8);
                put_le(q + 24, t[i].off, 8); put_le(q + 32, t[i].size, 8); put_le(q + 40, t[i].link, 4); put_le(q + 44, t[i].info, 4);
                put_le(q + 48, t[i].align, 8); put_le(q + 56, t[i].ent, 8);
            } else {
                put_le(q, (u64)t[i].nameoff, 4); put_le(q + 4, t[i].type, 4); put_le(q + 8, t[i].flags, 4); put_le(q + 12, t[i].addr, 4);
                put_le(q + 16, t[i].off, 4); put_le(q + 20, t[i].size, 4); put_le(q + 24, t[i].link, 4); put_le(q + 28, t[i].info, 4);
                put_le(q + 32, t[i].align, 4); put_le(q + 36, t[i].ent, 4);
            }
        }
        total = shpos + (size_t)nt * (size_t)(is64 ? 64 : 40);
        shnum_v = nt;
        shoff_v = shpos;
    }
    u8 *h = img;
    h[0] = 0x7f; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
    h[4] = is64 ? 2 : 1; h[5] = 1; h[6] = 1;
    put_le(h + 16, shared ? 3 : 2, 2);
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
    if (shnum_v) {
        if (is64) { put_le(h + 40, shoff_v, 8); put_le(h + 60, (u64)shnum_v, 2); put_le(h + 62, (u64)(shnum_v - 1), 2); }
        else { put_le(h + 32, shoff_v, 4); put_le(h + 48, (u64)shnum_v, 2); put_le(h + 50, (u64)(shnum_v - 1), 2); }
    }
    if (mach == 243 || mach == 183) put_le(h + (is64 ? 48 : 36), g32(O[0].f.d + (is64 ? 48 : 36)), 4);
    u8 *ph = h + po;
    u32 rxflags = textrel ? 7 : 5;
    if (is64) {
        if (dyn && !shared) {
            put_le(ph, 6, 4); put_le(ph + 4, 4, 4); put_le(ph + 8, 64, 8); put_le(ph + 16, base + 64, 8); put_le(ph + 24, base + 64, 8);
            put_le(ph + 32, (u64)phsz * (u64)nph, 8); put_le(ph + 40, (u64)phsz * (u64)nph, 8); put_le(ph + 48, 8, 8);
            ph += 56;
            put_le(ph, 3, 4); put_le(ph + 4, 4, 4); put_le(ph + 8, a_interp, 8); put_le(ph + 16, base + a_interp, 8); put_le(ph + 24, base + a_interp, 8);
            put_le(ph + 32, interp.n, 8); put_le(ph + 40, interp.n, 8); put_le(ph + 48, 1, 8);
            ph += 56;
        }
        put_le(ph, 1, 4); put_le(ph + 4, rxflags, 4); put_le(ph + 8, 0, 8); put_le(ph + 16, base, 8); put_le(ph + 24, base, 8);
        put_le(ph + 32, seg1end, 8); put_le(ph + 40, seg1end, 8); put_le(ph + 48, page, 8);
        ph += 56;
        if (hasdata) {
            put_le(ph, 1, 4); put_le(ph + 4, 6, 4); put_le(ph + 8, doff, 8); put_le(ph + 16, daddr_base, 8); put_le(ph + 24, daddr_base, 8);
            put_le(ph + 32, filesz2, 8); put_le(ph + 40, memend - doff, 8); put_le(ph + 48, page, 8);
            ph += 56;
        }
        if (dyn) {
            put_le(ph, 2, 4); put_le(ph + 4, 6, 4); put_le(ph + 8, doff + (a_dynamic - doff), 8); put_le(ph + 16, dynaddr, 8); put_le(ph + 24, dynaddr, 8);
            put_le(ph + 32, sz_dynamic, 8); put_le(ph + 40, sz_dynamic, 8); put_le(ph + 48, 8, 8);
            ph += 56;
        }
        put_le(ph, 0x6474e551, 4); put_le(ph + 4, 6, 4); put_le(ph + 48, 16, 8);
    } else {
        if (dyn && !shared) {
            put_le(ph, 6, 4); put_le(ph + 4, 52, 4); put_le(ph + 8, base + 52, 4); put_le(ph + 12, base + 52, 4);
            put_le(ph + 16, (u64)phsz * (u64)nph, 4); put_le(ph + 20, (u64)phsz * (u64)nph, 4); put_le(ph + 24, 4, 4); put_le(ph + 28, 4, 4);
            ph += 32;
            put_le(ph, 3, 4); put_le(ph + 4, a_interp, 4); put_le(ph + 8, base + a_interp, 4); put_le(ph + 12, base + a_interp, 4);
            put_le(ph + 16, interp.n, 4); put_le(ph + 20, interp.n, 4); put_le(ph + 24, 4, 4); put_le(ph + 28, 1, 4);
            ph += 32;
        }
        put_le(ph, 1, 4); put_le(ph + 4, 0, 4); put_le(ph + 8, base, 4); put_le(ph + 12, base, 4);
        put_le(ph + 16, seg1end, 4); put_le(ph + 20, seg1end, 4); put_le(ph + 24, rxflags, 4); put_le(ph + 28, page, 4);
        ph += 32;
        if (hasdata) {
            put_le(ph, 1, 4); put_le(ph + 4, doff, 4); put_le(ph + 8, daddr_base, 4); put_le(ph + 12, daddr_base, 4);
            put_le(ph + 16, filesz2, 4); put_le(ph + 20, memend - doff, 4); put_le(ph + 24, 6, 4); put_le(ph + 28, page, 4);
            ph += 32;
        }
        if (dyn) {
            put_le(ph, 2, 4); put_le(ph + 4, a_dynamic, 4); put_le(ph + 8, dynaddr, 4); put_le(ph + 12, dynaddr, 4);
            put_le(ph + 16, sz_dynamic, 4); put_le(ph + 20, sz_dynamic, 4); put_le(ph + 24, 6, 4); put_le(ph + 28, 4, 4);
            ph += 32;
        }
        put_le(ph, 0x6474e551, 4); put_le(ph + 24, 6, 4); put_le(ph + 28, 16, 4);
    }
    badd(outb, img, total);
    return 1;
}

int fmt_so(Obj *o, Bytes *out)
{
    Bytes ob = {0};
    if (!fmt_elfobj(o, &ob)) return 0;
    ld_mem_add("<cly>", ob.p, ob.n);
    char *f[1] = { "<cly>" };
    return ld_link_mem(f, 1, out);
}

static u64 infer_size(GSym *g)
{
    Obj1 *o = &O[g->file];
    ISec *ds = o->isec[g->shndx];
    if (!ds) return 0;
    u64 best = ds->size;
    for (u32 si = 1; si < o->nsyms; si++) {
        Sy y = get_sy(&o->f, o->syms, si);
        if (y.shndx != g->shndx || y.value <= g->value || y.type == 3) continue;
        if (y.value < best) best = y.value;
    }
    return best > g->value ? best - g->value : 0;
}
