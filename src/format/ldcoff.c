#include "cly.h"

static u16 g16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 g32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u64 g64(const u8 *p) { return (u64)g32(p) | (u64)g32(p + 4) << 32; }

#define IMG_BASE 0x400000

typedef struct { char *name; u32 vsize, rawsize, rawptr, relptr, chars; int nrel, cls, align; i64 outoff; } CSec;
typedef struct { const u8 *d; size_t n; const char *name; int machine, nsec; CSec *sec; u32 nsym; const u8 *syms; const char *str; u32 strsz; int *ref; i64 *refoff; } CObj;
typedef struct { char *name; int def, sec, weak, common, abs, alias, imp; i64 off, csize; int calign; Sym *sym; } G;

static G *gl;
static int ngl, capgl;
static int *gh;
static int ghcap;
static int cerr;

static void cler(const char *fmt, ...)
{
    va_list ap;
    fputs("cly: ld: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    cerr = 1;
}

static u32 hs(const char *s) { u32 h = 5381; while (*s) h = h * 33 + (u8)*s++; return h; }

static void rehash(void)
{
    free(gh);
    ghcap = 1;
    while (ghcap < ngl * 2 + 16) ghcap <<= 1;
    gh = xmalloc(sizeof(int) * (size_t)ghcap);
    for (int i = 0; i < ghcap; i++) gh[i] = -1;
    for (int i = 0; i < ngl; i++) {
        u32 h = hs(gl[i].name) & (u32)(ghcap - 1);
        while (gh[h] >= 0) h = (h + 1) & (u32)(ghcap - 1);
        gh[h] = i;
    }
}

static int gget(const char *nm, int make)
{
    if (ghcap) {
        u32 h = hs(nm) & (u32)(ghcap - 1);
        while (gh[h] >= 0) {
            if (!strcmp(gl[gh[h]].name, nm)) return gh[h];
            h = (h + 1) & (u32)(ghcap - 1);
        }
    }
    if (!make) return -1;
    if (ngl == capgl) { capgl = capgl ? capgl * 2 : 256; gl = xrealloc(gl, sizeof(G) * (size_t)capgl); }
    memset(&gl[ngl], 0, sizeof(G));
    gl[ngl].name = xstrdup(nm);
    gl[ngl].alias = -1;
    ngl++;
    rehash();
    return ngl - 1;
}

int ld_is_coff(const void *buf, size_t n)
{
    const u8 *p = buf;
    if (n < 20) return 0;
    u16 m = g16(p);
    return (m == 0x14C || m == 0x8664) && g16(p + 2) < 0x8000 && g32(p + 8) <= n && (p[0] != 0x7f);
}

static const char *dll_for(const char *n)
{
    static const char *u32p[] = { "MessageBox", "GetMessage", "PeekMessage", "DispatchMessage", "TranslateMessage", "CreateWindow", "RegisterClass", "DefWindowProc", "PostQuitMessage", "ShowWindow", "UpdateWindow", "LoadCursor", "LoadIcon", "GetDC", "ReleaseDC", "SetWindow", "GetWindow", "GetClient", "wsprintf", "wvsprintf", "DrawText", "BeginPaint", "EndPaint", "InvalidateRect", "SendMessage", "PostMessage", "SetTimer", "KillTimer", "FindWindow", "GetKey", "GetAsyncKey", "GetCursor", "SetCursor", "DestroyWindow", "GetSystemMetrics", "MoveWindow", "FillRect", "DialogBox", "EndDialog", "GetDlgItem", "SetFocus", "GetFocus", "MapVirtualKey", "OpenClipboard", "CloseClipboard", "GetForegroundWindow", "SetForegroundWindow", "EnumWindows", "ClientToScreen", "ScreenToClient", "AdjustWindowRect", "LoadImage", "LoadMenu", "TrackPopupMenu", "CharUpper", "CharLower", "IsWindow", "GetSysColor", "SetCapture", "ReleaseCapture", NULL };
    static const char *g32p[] = { "CreateSolidBrush", "SelectObject", "DeleteObject", "TextOut", "Rectangle", "Ellipse", "SetPixel", "GetPixel", "BitBlt", "StretchBlt", "SetBkColor", "SetBkMode", "SetTextColor", "GetStockObject", "CreateFont", "CreatePen", "MoveToEx", "LineTo", "CreateCompatible", "CreateDIBSection", "SwapBuffers", "ChoosePixelFormat", "SetPixelFormat", "GetDeviceCaps", "Polygon", "Polyline", "Arc", "Pie", "GetTextExtent", "SetDIBits", "GetDIBits", "DeleteDC", NULL };
    static const char *crt[] = { "printf", "puts", "scanf", "sscanf", "sprintf", "snprintf", "fprintf", "vprintf", "putchar", "getchar", "malloc", "calloc", "realloc", "free", "exit", "abort", "atexit", "strlen", "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp", "strchr", "strrchr", "strstr", "strtol", "strtoul", "strtod", "atoi", "atol", "atof", "memcpy", "memmove", "memset", "memcmp", "memchr", "fopen", "fclose", "fread", "fwrite", "fgets", "fputs", "fputc", "fgetc", "fseek", "ftell", "fflush", "rand", "srand", "qsort", "bsearch", "abs", "labs", "sin", "cos", "tan", "sqrt", "pow", "exp", "log", "floor", "ceil", "fabs", "time", "clock", "toupper", "tolower", "isalpha", "isdigit", "isspace", "perror", "system", "getenv", "signal", "setvbuf", "_", NULL };
    static const char *adv[] = { "RegOpenKey", "RegCloseKey", "RegQueryValue", "RegSetValue", "RegCreateKey", "RegDeleteKey", "RegEnumKey", "RegEnumValue", "GetUserName", "OpenProcessToken", "AdjustTokenPrivileges", "LookupPrivilegeValue", "CryptAcquireContext", "CryptGenRandom", "CryptReleaseContext", NULL };
    static const char *wsa[] = { "WSAStartup", "WSACleanup", "WSAGetLastError", "socket", "bind", "listen", "accept", "connect", "send", "recv", "closesocket", "htons", "htonl", "ntohs", "ntohl", "inet_addr", "gethostbyname", "select", "shutdown", "setsockopt", "getsockopt", "getaddrinfo", "freeaddrinfo", NULL };
    if (!strncmp(n, "__imp_", 6)) n += 6;
    for (int i = 0; u32p[i]; i++) if (!strncmp(n, u32p[i], strlen(u32p[i]))) return "user32.dll";
    for (int i = 0; g32p[i]; i++) if (!strncmp(n, g32p[i], strlen(g32p[i]))) return "gdi32.dll";
    for (int i = 0; adv[i]; i++) if (!strncmp(n, adv[i], strlen(adv[i]))) return "advapi32.dll";
    for (int i = 0; wsa[i]; i++) if (!strcmp(n, wsa[i])) return "ws2_32.dll";
    for (int i = 0; crt[i]; i++) if (crt[i][0] != '_' && !strcmp(n, crt[i])) return "msvcrt.dll";
    return "kernel32.dll";
}

static char *import_name(const char *n, int is64)
{
    if (!strncmp(n, "__imp_", 6)) n += 6;
    char *s = xstrdup(n);
    if (!is64) {
        if (s[0] == '_') memmove(s, s + 1, strlen(s));
        char *at = strrchr(s, '@');
        if (at && at != s) { char *q = at + 1; int dig = *q != 0; while (*q) { if (*q < '0' || *q > '9') dig = 0; q++; } if (dig) *at = 0; }
    }
    return s;
}

static const char *symname(CObj *o, const u8 *e, char *buf)
{
    if (g32(e) == 0) {
        u32 off = g32(e + 4);
        if (off < 4 || off >= o->strsz) return NULL;
        const char *s = o->str + off;
        size_t room = o->strsz - off;
        if (!memchr(s, 0, room)) return NULL;
        return s;
    }
    memcpy(buf, e, 8);
    buf[8] = 0;
    return buf;
}

static Sec *osec_new(Obj *ob, const char *name, int exec, int write, int nobits)
{
    Sec *s = xcalloc(1, sizeof *s);
    s->name = xstrdup(name);
    s->idx = ob->nsecs;
    s->alloc = 1;
    s->exec = exec;
    s->write = write;
    s->nobits = nobits;
    s->align = 1;
    ob->secs = xrealloc(ob->secs, sizeof(Sec *) * (size_t)(ob->nsecs + 1));
    ob->secs[ob->nsecs++] = s;
    return s;
}

static i64 up(i64 v, i64 a) { return a > 1 ? (v + a - 1) / a * a : v; }

static void addrel(Obj *ob, Reloc *r)
{
    if (ob->nrelocs == ob->caprelocs) {
        ob->caprelocs = ob->caprelocs ? ob->caprelocs * 2 : 256;
        ob->relocs = xrealloc(ob->relocs, sizeof(Reloc) * (size_t)ob->caprelocs);
    }
    ob->relocs[ob->nrelocs++] = *r;
}

int ld_link_coff(char **files, int nfiles, const char *outpath)
{
    cerr = 0;
    ngl = capgl = 0;
    gl = NULL;
    free(gh);
    gh = NULL;
    ghcap = 0;
    CObj *C = xcalloc((size_t)nfiles + 1, sizeof(CObj));
    int machine = 0;
    Obj ob;
    memset(&ob, 0, sizeof ob);
    Sec *S[4];
    S[0] = osec_new(&ob, ".text", 1, 0, 0);
    S[1] = osec_new(&ob, ".rdata", 0, 0, 0);
    S[2] = osec_new(&ob, ".data", 0, 1, 0);
    S[3] = osec_new(&ob, ".bss", 0, 1, 1);
    for (int fi = 0; fi < nfiles; fi++) {
        CObj *o = &C[fi];
        size_t n = 0;
        errno = 0;
        char *buf = read_file(files[fi], &n);
        if (!buf) { cler("cannot open `%s': %s", files[fi], errno ? strerror(errno) : "unreadable"); return 0; }
        o->d = (const u8 *)buf;
        o->n = n;
        o->name = files[fi];
        if (!ld_is_coff(buf, n)) { cler("`%s' is not a COFF object file", files[fi]); return 0; }
        o->machine = g16(o->d);
        if (!machine) machine = o->machine;
        else if (machine != o->machine) { cler("`%s' is for a different architecture than `%s'", files[fi], files[0]); return 0; }
        o->nsec = g16(o->d + 2);
        u32 symptr = g32(o->d + 8);
        o->nsym = g32(o->d + 12);
        u32 optsz = g16(o->d + 16);
        u64 shend = 20 + (u64)optsz + 40 * (u64)o->nsec;
        if (shend > n) { cler("`%s' is truncated", files[fi]); return 0; }
        if (o->nsym) {
            if (!symptr || (u64)symptr + 18 * (u64)o->nsym + 4 > n) { cler("`%s' has a bad symbol table", files[fi]); return 0; }
            o->syms = o->d + symptr;
            o->str = (const char *)o->d + symptr + 18 * (u64)o->nsym;
            o->strsz = g32((const u8 *)o->str);
            if (o->strsz < 4 || o->strsz > n - ((u64)symptr + 18 * (u64)o->nsym)) { cler("`%s' has a bad string table", files[fi]); return 0; }
        }
        o->sec = xcalloc((size_t)o->nsec + 1, sizeof(CSec));
        for (int i = 0; i < o->nsec; i++) {
            const u8 *p = o->d + 20 + optsz + 40 * (size_t)i;
            CSec *s = &o->sec[i];
            char nb[9];
            memcpy(nb, p, 8);
            nb[8] = 0;
            if (nb[0] == '/' && nb[1] >= '0' && nb[1] <= '9') {
                u32 off = (u32)strtoul(nb + 1, NULL, 10);
                if (!o->nsym || off < 4 || off >= o->strsz || !memchr(o->str + off, 0, o->strsz - off)) { cler("`%s' has a bad section name", files[fi]); return 0; }
                s->name = xstrdup(o->str + off);
            } else s->name = xstrdup(nb);
            s->vsize = g32(p + 8);
            s->rawsize = g32(p + 16);
            s->rawptr = g32(p + 20);
            s->relptr = g32(p + 24);
            s->nrel = g16(p + 32);
            s->chars = g32(p + 36);
            s->cls = -1;
            int al = (s->chars >> 20) & 15;
            s->align = al ? 1 << (al - 1) : 1;
            if (s->chars & 0x00000800) continue;
            if (s->chars & 0x02000000) continue;
            if (!strncmp(s->name, ".debug", 6) || !strcmp(s->name, ".drectve") || !strncmp(s->name, ".pdata", 6) || !strncmp(s->name, ".xdata", 6)) continue;
            int code = (s->chars & 0x20) != 0, init = (s->chars & 0x40) != 0, uninit = (s->chars & 0x80) != 0;
            if (!code && !init && !uninit) continue;
            int isbss = !code && uninit && !init;
            u32 size = isbss ? (s->rawsize ? s->rawsize : s->vsize) : s->rawsize;
            if (s->chars & 0x01000000 && s->nrel == 0xFFFF) { cler("`%s': section `%s' has too many relocations", files[fi], s->name); return 0; }
            if (!isbss && size) {
                if (!s->rawptr || (u64)s->rawptr + size > n) { cler("`%s': section `%s' is truncated", files[fi], s->name); return 0; }
            }
            if (s->nrel && (!s->relptr || (u64)s->relptr + 10 * (u64)s->nrel > n)) { cler("`%s': section `%s' has bad relocations", files[fi], s->name); return 0; }
            if (size > (1u << 30)) { cler("`%s': section `%s' is too large", files[fi], s->name); return 0; }
            s->rawsize = size;
            s->cls = code ? 0 : isbss ? 3 : (s->chars & 0x80000000u) ? 2 : 1;
            Sec *t = S[s->cls];
            if (s->align > t->align) t->align = s->align;
            i64 base = t->nobits ? t->size : (i64)t->data.n;
            base = up(base, s->align);
            if (base + (i64)size > ((i64)1 << 30)) { cler("the program would be too large"); return 0; }
            s->outoff = base;
            if (t->nobits) t->size = base + size;
            else {
                bfill(&t->data, 0, (size_t)(base - (i64)t->data.n));
                badd(&t->data, o->d + s->rawptr, size);
                t->size = (i64)t->data.n;
            }
        }
        o->ref = xcalloc((size_t)o->nsym + 1, sizeof(int));
        o->refoff = xcalloc((size_t)o->nsym + 1, sizeof(i64));
        for (u32 i = 0; i < o->nsym; i++) o->ref[i] = -1;
    }
    int is64 = machine == 0x8664;
    ob.is64 = is64;
    ob.bits = is64 ? 64 : 32;
    ob.arch = ARCH_X86;
    for (int fi = 0; fi < nfiles; fi++) {
        CObj *o = &C[fi];
        for (u32 i = 0; i < o->nsym; i++) {
            const u8 *e = o->syms + 18 * (size_t)i;
            int aux = e[17];
            char nb[9];
            const char *nm = symname(o, e, nb);
            u32 val = g32(e + 8);
            int secn = (int16_t)g16(e + 12);
            int sc = e[16];
            if (!nm) { cler("`%s' has a bad symbol name", o->name); return 0; }
            if ((u64)i + 1 + (u64)aux > o->nsym) { cler("`%s' has a bad symbol table", o->name); return 0; }
            if (secn > o->nsec) { cler("`%s': symbol `%s' is in a missing section", o->name, nm); return 0; }
            if (sc == 2 || sc == 105) {
                int g = gget(nm, 1);
                o->ref[i] = g;
                G *x = &gl[g];
                if (sc == 105) {
                    x->weak = 1;
                    if (aux && !x->def && !x->common) {
                        u32 tag = g32(e + 18);
                        if (tag < o->nsym && tag != i) {
                            char nb2[9];
                            const char *tn = symname(o, o->syms + 18 * (size_t)tag, nb2);
                            if (tn) x->alias = gget(tn, 1);
                        }
                    }
                } else if (secn > 0) {
                    CSec *cs = &o->sec[secn - 1];
                    if (cs->cls < 0) {
                    } else if (x->def && !x->weak) { cler("multiple definition of `%s' (in `%s')", nm, o->name); }
                    else {
                        x = &gl[g];
                        x->def = 1; x->weak = 0; x->common = 0; x->abs = 0;
                        x->sec = cs->cls;
                        x->off = cs->outoff + (i64)val;
                    }
                } else if (secn == -1) {
                    if (x->def && !x->weak) cler("multiple definition of `%s' (in `%s')", nm, o->name);
                    else { x->def = 1; x->weak = 0; x->abs = 1; x->off = (i64)(int32_t)val; }
                } else if (secn == 0 && val > 0) {
                    if (!x->def) {
                        x->common = 1;
                        if ((i64)val > x->csize) x->csize = val;
                        x->calign = 16;
                    }
                }
            } else if ((sc == 3 || sc == 6) && secn > 0) {
                CSec *cs = &o->sec[secn - 1];
                if (cs->cls >= 0) { o->ref[i] = -2 - cs->cls; o->refoff[i] = cs->outoff + (i64)val; }
                else o->ref[i] = -100;
            } else if ((sc == 3 || sc == 6) && secn == -1) {
                o->ref[i] = -101;
                o->refoff[i] = (i64)(int32_t)val;
            }
            i += (u32)aux;
        }
    }
    if (cerr) return 0;
    for (int i = 0; i < ngl; i++) {
        G *x = &gl[i];
        if (x->common && !x->def) {
            Sec *t = S[3];
            i64 al = x->csize >= 16 ? 16 : x->csize >= 8 ? 8 : x->csize >= 4 ? 4 : x->csize >= 2 ? 2 : 1;
            i64 base = up(t->size, al);
            if (al > t->align) t->align = (int)al;
            x->def = 1;
            x->sec = 3;
            x->off = base;
            t->size = base + x->csize;
        }
    }
    for (int i = 0; i < ngl; i++) {
        G *x = &gl[i];
        int k = i, hop = 0;
        while (!gl[k].def && gl[k].alias >= 0 && hop++ < 16) k = gl[k].alias;
        if (k != i && gl[k].def) { x->def = 1; x->sec = gl[k].sec; x->abs = gl[k].abs; x->off = gl[k].off; }
    }
    for (int i = 0; i < ngl; i++) {
        G *x = &gl[i];
        Sym *s = xcalloc(1, sizeof *s);
        s->name = xstrdup(x->name);
        s->referenced = 1;
        s->global = 1;
        if (x->def) {
            s->defined = 1;
            if (x->abs) { s->sec = -1; s->off = x->off; s->isabs = 1; }
            else { s->sec = x->sec; s->off = x->off; }
        } else if (x->weak) {
            s->defined = 1;
            s->sec = -1;
            s->off = 0;
            s->isabs = 1;
        } else {
            s->ext = 1;
            s->import = 1;
            const char *dll = dll_for(x->name);
            s->impdll = xstrdup(dll);
            s->impname = import_name(x->name, is64);
            x->imp = 1;
        }
        x->sym = s;
        ob.syms = xrealloc(ob.syms, sizeof(Sym *) * (size_t)(ob.nsyms + 1));
        ob.syms[ob.nsyms++] = s;
    }
    for (int fi = 0; fi < nfiles; fi++) {
        CObj *o = &C[fi];
        for (int si = 0; si < o->nsec; si++) {
            CSec *cs = &o->sec[si];
            if (cs->cls < 0 || !cs->nrel) continue;
            Sec *t = S[cs->cls];
            for (int k = 0; k < cs->nrel; k++) {
                const u8 *p = o->d + cs->relptr + 10 * (size_t)k;
                u32 vaddr = g32(p);
                u32 symi = g32(p + 4);
                int type = g16(p + 8);
                if (symi >= o->nsym) { cler("`%s': relocation against a missing symbol", o->name); return 0; }
                int size, kind = RK_ABS, extra = 0, nb = 0;
                if (is64) {
                    switch (type) {
                    case 0: continue;
                    case 1: size = 8; break;
                    case 2: size = 4; break;
                    case 3: size = 4; nb = 1; break;
                    case 4: case 5: case 6: case 7: case 8: case 9: size = 4; kind = RK_REL; extra = type - 4; break;
                    default: cler("`%s': unsupported relocation type %d", o->name, type); return 0;
                    }
                } else {
                    switch (type) {
                    case 0: continue;
                    case 6: size = 4; break;
                    case 7: size = 4; nb = 1; break;
                    case 0x14: size = 4; kind = RK_REL; break;
                    default: cler("`%s': unsupported relocation type %d", o->name, type); return 0;
                    }
                }
                if ((u64)vaddr + (u64)size > cs->rawsize || t->nobits) { cler("`%s': relocation outside its section", o->name); return 0; }
                size_t at = (size_t)(cs->outoff + (i64)vaddr);
                i64 add = size == 8 ? (i64)g64(t->data.p + at) : (i64)(int32_t)g32(t->data.p + at);
                Reloc r;
                memset(&r, 0, sizeof r);
                r.sec = t->idx;
                r.off = cs->outoff + (i64)vaddr;
                r.size = size;
                r.kind = kind;
                if (kind == RK_REL) r.relbase = r.off + 4 + extra;
                val_const(&r.v, add);
                int ref = o->ref[symi];
                if (ref >= 0) r.v.ext = gl[ref].sym;
                else if (ref <= -2 && ref >= -5) { r.v.pos = -2 - ref; r.v.n += o->refoff[symi]; }
                else if (ref == -101) r.v.n += o->refoff[symi];
                else if (ref == -100) { cler("`%s': relocation against a discarded section", o->name); return 0; }
                else { cler("`%s': relocation against an unsupported symbol", o->name); return 0; }
                if (nb) r.v.n -= IMG_BASE;
                addrel(&ob, &r);
            }
        }
    }
    static const char *ents[] = { "start", "_start", "mainCRTStartup", "_mainCRTStartup", "WinMainCRTStartup", "_WinMainCRTStartup", NULL };
    for (int i = 0; ents[i] && !ob.entry; i++) {
        int g = gget(ents[i], 0);
        if (g >= 0 && gl[g].def) ob.entry = gl[g].sym;
    }
    for (int i = 0; i < ngl; i++) {
        if (gl[i].imp && ob.entry == gl[i].sym) ob.entry = NULL;
    }
    if (cerr) return 0;
    Bytes out = {0};
    g_errors = 0;
    int ok = fmt_pe(&ob, &out);
    if (!ok || g_errors) return 0;
    if (write_file(outpath, out.p, out.n, 1)) { cler("cannot write `%s'", outpath); return 0; }
    printf("%s: %zu bytes\n", outpath, out.n);
    return 1;
}
