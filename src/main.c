#include "cly.h"
#include <errno.h>

static int g_dumprt;
static const char *short_usage = "usage: cly <bare|windows|linux|mac> <file> [-bin|-elf|-exe|-macho|-obj|-flat|-img|-com|-hex|-srec]\ntry `cly -h' for all options\n";

static const char *kernel_names[] = { "bare", "windows", "linux", "mac" };

static const char *usage_text =
    "Componly " CLY_VERSION "\n"
    "\n"
    "usage: cly <kernel> <file> [-type] [options]\n"
    "\n"
    "kernels: bare, windows, linux, mac\n"
    "types:   -bin (default), -elf, -exe, -macho, -flat, -img, -com, -hex, -srec, -obj, -coff\n"
    "\n"
    "options:\n"
    "  -out FILE     output file name\n"
    "  -I DIR        add an include directory\n"
    "  -D NAME[=V]   define a macro\n"
    "  -U NAME       undefine a macro\n"
    "  -P FILE       pre-include a file\n"
    "  -l [FILE]     write a listing\n"
    "  -m32, -m64    force 32-bit or 64-bit code (default: 64 for linux, windows, mac; 32 for bare)\n"
    "  -E            preprocess only\n"
    "  -w            hide warnings\n"
    "  -q            quiet\n"
    "  --dump-insns  print the instruction table\n"
    "  -v            version\n"
    "  -h            help\n";

static int parse_kernel(const char *s)
{
    if (!strcasecmp(s, "bare") || !strcasecmp(s, "baremetal") || !strcasecmp(s, "bare-metal") || !strcasecmp(s, "none")) return K_BARE;
    if (!strcasecmp(s, "windows") || !strcasecmp(s, "win") || !strcasecmp(s, "win32") || !strcasecmp(s, "nt")) return K_WINDOWS;
    if (!strcasecmp(s, "linux") || !strcasecmp(s, "lin")) return K_LINUX;
    if (!strcasecmp(s, "mac") || !strcasecmp(s, "macos") || !strcasecmp(s, "osx") || !strcasecmp(s, "darwin")) return K_MAC;
    return -1;
}

static int parse_type(const char *s)
{
    if (*s == '-') s++;
    if (*s == '-') s++;
    if (!strcasecmp(s, "bin")) return T_BIN;
    if (!strcasecmp(s, "elf")) return T_ELF;
    if (!strcasecmp(s, "exe") || !strcasecmp(s, "pe")) return T_EXE;
    if (!strcasecmp(s, "macho") || !strcasecmp(s, "mach-o")) return T_MACHO;
    if (!strcasecmp(s, "flat") || !strcasecmp(s, "raw")) return T_FLAT;
    if (!strcasecmp(s, "img") || !strcasecmp(s, "image")) return T_IMG;
    if (!strcasecmp(s, "com")) return T_COM;
    if (!strcasecmp(s, "hex") || !strcasecmp(s, "ihex")) return T_HEX;
    if (!strcasecmp(s, "srec") || !strcasecmp(s, "s19")) return T_SREC;
    if (!strcasecmp(s, "obj") || !strcasecmp(s, "o")) return T_OBJ;
    if (!strcasecmp(s, "coff") || !strcasecmp(s, "win32")) return T_COFF;
    return -1;
}

static const char *ext_for(int kernel, int type)
{
    switch (type) {
    case T_BIN:
        if (kernel == K_WINDOWS) return ".exe";
        if (kernel == K_BARE) return ".bin";
        return "";
    case T_ELF: return kernel == K_LINUX ? "" : ".elf";
    case T_EXE: return ".exe";
    case T_MACHO: return kernel == K_MAC ? "" : ".macho";
    case T_FLAT: return ".bin";
    case T_IMG: return ".img";
    case T_COM: return ".com";
    case T_HEX: return ".hex";
    case T_SREC: return ".srec";
    case T_OBJ: return kernel == K_WINDOWS ? ".obj" : ".o";
    case T_COFF: return ".obj";
    }
    return "";
}

static const char *fmt_name(int kernel, int type)
{
    if (type == T_BIN) type = kernel == K_WINDOWS ? T_EXE : kernel == K_MAC ? T_MACHO : kernel == K_LINUX ? T_ELF : T_FLAT;
    switch (type) {
    case T_ELF: return "elf32";
    case T_EXE: return "win32";
    case T_MACHO: return "macho32";
    case T_OBJ: return kernel == K_WINDOWS ? "win32" : "elf32";
    case T_COFF: return "win32";
    }
    return "bin";
}

static char *strip_ext(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    char *r = xstrdup(path);
    char *dot = strrchr(r + (base - path), '.');
    if (dot && dot != r + (base - path)) *dot = 0;
    return r;
}

static int has_org(const LineVec *l)
{
    for (int i = 0; i < l->n; i++) {
        const char *t = l->v[i].text;
        while (*t == ' ' || *t == '\t') t++;
        if (!strncasecmp(t, "org", 3) && (t[3] == ' ' || t[3] == '\t')) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int kernel = -1, type = T_BIN, type_set = 0;
    const char *file = NULL, *outpath = NULL;
    PPOpts po;
    memset(&po, 0, sizeof po);
    int pponly = 0, quiet = 0, list = 0, dump = 0;
    char *listpath = NULL;
    char **pos = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    int npos = 0;
    char **inc = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    char **defs = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    char **undefs = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    char **pre = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    int ni = 0, nd = 0, nu = 0, np = 0;
    int mbits = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "-?")) { fputs(usage_text, stdout); return 0; }
        if (!strcmp(a, "-v") || !strcmp(a, "--version") || !strcmp(a, "-version")) { printf("Componly %s\n", CLY_VERSION); return 0; }
        if (!strcmp(a, "-m32")) { mbits = 32; continue; }
        if (!strcmp(a, "-m64")) { mbits = 64; continue; }
        if (!strcmp(a, "-m16")) { mbits = 16; continue; }
        if (!strcmp(a, "--dump-insns")) { dump = 1; continue; }
        if (!strcmp(a, "--dump-rt")) { g_dumprt = 1; continue; }
        if (!strcmp(a, "-out") || !strcmp(a, "-o") || !strcmp(a, "--out")) {
            if (i + 1 >= argc) { fprintf(stderr, "cly: option `%s' needs a file name\n", a); return 1; }
            outpath = argv[++i];
            continue;
        }
        if (!strncmp(a, "-I", 2) || !strncmp(a, "-D", 2) || !strncmp(a, "-U", 2) || !strncmp(a, "-P", 2)) {
            const char *val = a + 2;
            if (!*val) {
                if (i + 1 >= argc) { fprintf(stderr, "cly: option `%s' needs an argument\n", a); return 1; }
                val = argv[++i];
            }
            if (a[1] == 'I') inc[ni++] = (char *)val;
            else if (a[1] == 'D') defs[nd++] = (char *)val;
            else if (a[1] == 'U') undefs[nu++] = (char *)val;
            else pre[np++] = (char *)val;
            continue;
        }
        if (!strcmp(a, "-E")) { pponly = 1; continue; }
        if (!strcmp(a, "-w")) { g_nowarn = 1; continue; }
        if (!strcmp(a, "-q")) { quiet = 1; continue; }
        if (!strcmp(a, "-l")) {
            list = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-' && !strstr(argv[i + 1], ".cly") && parse_kernel(argv[i + 1]) < 0) listpath = argv[++i];
            continue;
        }
        if (a[0] == '-' && a[1]) {
            int t = parse_type(a);
            if (t >= 0) { type = t; type_set = 1; continue; }
            fprintf(stderr, "cly: unknown option `%s'\n", a);
            {
                static const char *opts[] = { "bin", "elf", "exe", "macho", "obj", "flat", "img", "com", "hex", "srec", NULL };
                const char *nm = a + (a[1] == '-' ? 2 : 1);
                for (int k = 0; opts[k]; k++)
                    if (edit_distance(nm, opts[k]) <= 1) { fprintf(stderr, "cly: did you mean `-%s'?\n", opts[k]); break; }
            }
            return 1;
        }
        pos[npos++] = (char *)a;
    }
    if (dump) {
        insn_init();
        insn_dump(stdout);
        return 0;
    }
    for (int i = 0; i < npos; i++) {
        int k = parse_kernel(pos[i]);
        if (k >= 0 && kernel < 0 && !file_exists(pos[i])) { kernel = k; continue; }
        if (!file) { file = pos[i]; continue; }
        if (kernel < 0 && !type_set && parse_type(pos[i]) < 0) break;
        if (!type_set) {
            int t = parse_type(pos[i]);
            if (t >= 0) { type = t; type_set = 1; continue; }
        }
        fprintf(stderr, "cly: unexpected argument `%s'\n", pos[i]);
        return 1;
    }
    if (kernel < 0 && file && !file_exists(file) && !dump && !g_dumprt) {
        fprintf(stderr, "cly: unknown kernel `%s'; use bare, windows, linux or mac\n", file);
        for (int k = 0; k < 4; k++)
            if (edit_distance(file, kernel_names[k]) <= 2) { fprintf(stderr, "cly: did you mean `%s'?\n", kernel_names[k]); break; }
        return 1;
    }
    if (g_dumprt && kernel >= 0) {
        Str rt = {0};
        rt_build(kernel, type, 32, &rt);
        fputs(rt.s, stdout);
        return 0;
    }
    if (kernel < 0 && !file) {
        fputs(usage_text, stderr);
        return 1;
    }
    if (kernel < 0) {
        fprintf(stderr, "cly: missing kernel (bare, windows, linux or mac)\n%s", short_usage);
        return 1;
    }
    if (!file) {
        fprintf(stderr, "cly: missing input file\n%s", short_usage);
        return 1;
    }
    if (is_dir(file)) {
        fprintf(stderr, "cly: `%s' is a directory\n", file);
        return 1;
    }
    {
        size_t flen = 0;
        errno = 0;
        char *probe = read_file(file, &flen);
        if (!probe) {
            fprintf(stderr, "cly: cannot open `%s': %s\n", file, errno ? strerror(errno) : "unreadable");
            return 1;
        }
        int binary = 0;
        for (size_t k = 0; k < flen && !binary; k++) {
            unsigned char ch = (unsigned char)probe[k];
            if (ch < 9 || (ch > 13 && ch < 32 && ch != 27)) binary = 1;
        }
        free(probe);
        if (binary) {
            fprintf(stderr, "cly: `%s' is not a text source file\n", file);
            return 1;
        }
    }
    char *dir = path_dir(file);
    inc[ni++] = dir;
    po.incdirs = inc;
    po.nincdirs = ni;
    po.defs = defs;
    po.ndefs = nd;
    po.undefs = undefs;
    po.nundefs = nu;
    po.preincs = pre;
    po.npreincs = np;
    po.outfmt = fmt_name(kernel, type);
    po.bits = mbits ? mbits : 32;
    LineVec lines;
    memset(&lines, 0, sizeof lines);
    if (!pp_run(file, &po, &lines) || g_errors) return 1;
    if (pponly) {
        for (int i = 0; i < lines.n; i++) puts(lines.v[i].text);
        return 0;
    }
    AsmOpts ao;
    memset(&ao, 0, sizeof ao);
    ao.kernel = kernel;
    ao.type = type;
    ao.srcpath = file;
    ao.cpu = 99;
    ao.bits = mbits;
    ao.list = list;
    int flat = type == T_FLAT || type == T_COM || type == T_HEX || type == T_SREC;
    ao.flat_mode = flat;
    if (type == T_COM) {
        lv_add(&ao.lines, "bits 16", "<com>", 1);
        if (!has_org(&lines)) lv_add(&ao.lines, "org 0x100", "<com>", 2);
    }
    for (int i = 0; i < lines.n; i++) lv_add(&ao.lines, lines.v[i].text, lines.v[i].file, lines.v[i].line);
    char *base = strip_ext(file);
    if (list) {
        if (!listpath) {
            Str s = {0};
            sfmt(&s, "%s.lst", base);
            listpath = sget(&s);
        }
        ao.listpath = listpath;
    }
    Obj obj;
    memset(&obj, 0, sizeof obj);
    if (!assemble(&ao, &obj) || g_errors) {
        fprintf(stderr, "cly: %d error%s", g_errors, g_errors == 1 ? "" : "s");
        if (g_warnings) fprintf(stderr, ", %d warning%s", g_warnings, g_warnings == 1 ? "" : "s");
        fputc('\n', stderr);
        return 1;
    }
    Bytes out = {0};
    if (!link_obj(&obj, kernel, type, &out) || g_errors) {
        fprintf(stderr, "cly: link failed\n");
        return 1;
    }
    char *oname;
    if (outpath) oname = xstrdup(outpath);
    else {
        Str s = {0};
        const char *ext = ext_for(kernel, type);
        sfmt(&s, "%s%s", base, ext);
        oname = sget(&s);
        if (!strcmp(oname, file)) {
            sfmt(&s, ".out");
            oname = sget(&s);
        }
    }
    int exec = (type == T_ELF || type == T_MACHO || (type == T_BIN && (kernel == K_LINUX || kernel == K_MAC)));
    if (!strcmp(oname, file)) {
        fprintf(stderr, "cly: output would overwrite the input file `%s'\n", file);
        return 1;
    }
    errno = 0;
    if (write_file(oname, out.p, out.n, exec)) {
        fprintf(stderr, "cly: cannot write `%s': %s\n", oname, errno ? strerror(errno) : "unknown error");
        return 1;
    }
    if (!quiet) printf("%s: %zu bytes\n", oname, out.n);
    return 0;
}
