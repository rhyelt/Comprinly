/* Command line driver: parses arguments, runs the preprocessor, library, optimizer, assembler and linker, and writes the output file. */
#include "cly.h"
#include <errno.h>

static int g_dumpopt;
static const char *short_usage = "usage: cly <bare|windows|linux|mac> <file> [-bin|-elf|-exe|-macho|-obj|-o|-flat|-img|-com|-hex|-srec]\ntry `cly -h' for all options\n";

static const char *kernel_names[] = { "bare", "windows", "linux", "mac" };

static const char *usage_text =
    "Comprinly " CLY_VERSION "\n"
    "\n"
    "usage: cly <kernel> <file> [-type] [modes] [options]\n"
    "\n"
    "kernels: bare, windows, linux, mac\n"
    "types:   -bin (default), -elf, -exe, -macho, -flat, -img, -com, -hex, -srec, -obj, -o, -coff\n"
    "         (-o writes an object file named .o on every kernel; use -out FILE to name any output)\n"
    "\n"
    "modes (any order, any number):\n"
    "  -nasm         behave exactly like NASM: x86 only, no library, no leak check, no optimizer,\n"
    "                no guessing, no typo hints, case-sensitive symbols, externs must be declared\n"
    "  -nolibc       do not add the built-in C library\n"
    "  -noleak       skip the memory leak check\n"
    "  -fix          make the leak-free copy the main output\n"
    "  -fixsrc       also write the source of the leak-free copy\n"
    "  -O0           turn the optimizer off (it is on by default)\n"
    "  -Werror       treat warnings as errors\n"
    "  -M, -MD       print a make rule with the files used, or write it to <output>.d\n"
    "  -log[=FILE]   log every compile step with timestamps (screen or file)\n"
    "\n"
    "options:\n"
    "  -out FILE     output file name\n"
    "  -I DIR        add an include directory\n"
    "  -D NAME[=V]   define a macro\n"
    "  -U NAME       undefine a macro\n"
    "  -P FILE       pre-include a file\n"
    "  -l [FILE]     write a listing\n"
    "  -m32, -m64    force 32-bit or 64-bit code (default: 64 for linux, windows, mac; 32 for bare)\n"
    "  -cpu NAME     target cpu: x86-64, arm64 or risc-v (rv64gc, rv32imac, ...); a `cpu' line on\n"
    "                the first line of the source does the same, otherwise it is guessed\n"
    "  -E            preprocess only\n"
    "  -w            hide warnings\n"
    "  -q            hide everything but errors\n"
    "  --dump-opt    print the source after optimizing, to stderr\n"
    "  --dump-insns  print the instruction table\n"
    "  -v            version\n"
    "  -h            help\n";

/* Maps a kernel name (and its common spellings) to K_BARE, K_WINDOWS, K_LINUX or K_MAC. Returns -1 if it is not a kernel. */
static int parse_kernel(const char *s)
{
    if (!strcasecmp(s, "bare") || !strcasecmp(s, "baremetal") || !strcasecmp(s, "bare-metal") || !strcasecmp(s, "none")) return K_BARE;
    if (!strcasecmp(s, "windows") || !strcasecmp(s, "win") || !strcasecmp(s, "win32") || !strcasecmp(s, "nt")) return K_WINDOWS;
    if (!strcasecmp(s, "linux") || !strcasecmp(s, "lin")) return K_LINUX;
    if (!strcasecmp(s, "mac") || !strcasecmp(s, "macos") || !strcasecmp(s, "osx") || !strcasecmp(s, "darwin")) return K_MAC;
    return -1;
}

/* Maps an output type word such as -elf, -exe or -hex to its T_* value. Accepts one or two leading dashes. Returns -1 if unknown. */
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

/* Default file extension for a kernel and output type, used when -out is not given. */
static int g_dot_o;
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
    case T_OBJ: return kernel == K_WINDOWS && !g_dot_o ? ".obj" : ".o";
    case T_COFF: return ".obj";
    }
    return "";
}

/* Name of the object format for a kernel, type and bit size (elf64, win32, macho64, bin ...). Fed to the preprocessor as __OUTPUT_FORMAT__. */
static const char *fmt_name(int kernel, int type, int bits)
{
    int b64 = bits == 64;
    if (type == T_BIN) type = kernel == K_WINDOWS ? T_EXE : kernel == K_MAC ? T_MACHO : kernel == K_LINUX ? T_ELF : T_FLAT;
    switch (type) {
    case T_ELF: return b64 ? "elf64" : "elf32";
    case T_EXE: return b64 ? "win64" : "win32";
    case T_MACHO: return b64 ? "macho64" : "macho32";
    case T_OBJ: return kernel == K_WINDOWS ? (b64 ? "win64" : "win32") : (b64 ? "elf64" : "elf32");
    case T_COFF: return b64 ? "win64" : "win32";
    }
    return "bin";
}

/* Copies the file name without its folder and without its extension. Used to build default output names. */
static char *strip_ext(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    char *r = xstrdup(path);
    char *dot = strrchr(r + (base - path), '.');
    if (dot && dot != r + (base - path)) *dot = 0;
    return r;
}

/* True if the source has an org line, which means the user wants a raw flat binary with a fixed load address. */
static int has_org(const LineVec *l)
{
    for (int i = 0; i < l->n; i++) {
        const char *t = l->v[i].text;
        while (*t == ' ' || *t == '\t') t++;
        if (!strncasecmp(t, "org", 3) && (t[3] == ' ' || t[3] == '\t')) return 1;
    }
    return 0;
}

static size_t g_last_size;
static int g_nolibc;

/* One full build of an image: copy the lines, add the C library, run the optimizer, assemble, link and write the file. Called again for the leak-free copy. */
static int build_image(AsmOpts *ao, int arch, int kernel, int type, const char *oname, const char *file, int quiet, int optimize)
{
    /* Work on a copy so the original lines can be built again later (for the leak-free copy). */
    LineVec work;
    memset(&work, 0, sizeof work);
    for (int i = 0; i < ao->lines.n; i++) lv_add(&work, ao->lines.v[i].text, ao->lines.v[i].file, ao->lines.v[i].line);
    AsmOpts a2 = *ao;
    a2.lines = work;
    /* Step 1: add the built-in C library blocks the program needs (Linux x86 only). */
    if (arch == ARCH_X86 && !g_nolibc) {
        int n0 = a2.lines.n;
        xlog("libc", "scanning for library calls (%d lines)", n0);
        libc_run(&a2.lines, kernel, type, detect_bits(&a2));
        xlog("libc", "%s, %d lines now", a2.lines.n != n0 ? "library code added" : "nothing to add", a2.lines.n);
    } else xlog("libc", "skipped");
    /* Step 2: optimize the text (peephole rules for x86, small passes for ARM64 and RISC-V). */
    g_strip = optimize;
    if (optimize) {
        int n0 = a2.lines.n;
        xlog("optimize", "running peephole passes (%d lines)", n0);
        if (arch == ARCH_X86) opt_run(&a2.lines, detect_bits(&a2), kernel, type);
        else opt_run_nx(&a2.lines, arch, kernel, type);
        xlog("optimize", "%d lines in, %d lines out", n0, a2.lines.n);
    } else xlog("optimize", "off");
    if (g_dumpopt) for (int i = 0; i < a2.lines.n; i++) fprintf(stderr, "%s\n", a2.lines.v[i].text);
    /* Step 3: assemble all lines into sections, symbols and relocations. */
    xlog("assemble", "starting (%d lines)", a2.lines.n);
    Obj obj;
    memset(&obj, 0, sizeof obj);
    if (!assemble(&a2, &obj) || g_errors) {
        fprintf(stderr, "cly: %d error%s", g_errors, g_errors == 1 ? "" : "s");
        if (g_warnings) fprintf(stderr, ", %d warning%s", g_warnings, g_warnings == 1 ? "" : "s");
        fputc('\n', stderr);
        return 1;
    }
    /* Step 4: link into the requested file format. */
    Bytes out = {0};
    xlog("link", "building the %s image", fmt_name(kernel, type, detect_bits(&a2)));
    if (!link_obj(&obj, kernel, type, &out) || g_errors) {
        fprintf(stderr, "cly: link failed\n");
        return 1;
    }
    /* Step 5: write the file, marking it executable for ELF and Mach-O. */
    int exec = (type == T_ELF || type == T_MACHO || (type == T_BIN && (kernel == K_LINUX || kernel == K_MAC)));
    errno = 0;
    if (write_file(oname, out.p, out.n, exec)) {
        fprintf(stderr, "cly: cannot write `%s': %s\n", oname, errno ? strerror(errno) : "unknown error");
        return 1;
    }
    xlog("write", "%s, %zu bytes", oname, out.n);
    g_last_size = out.n;
    if (!quiet) printf("%s: %zu bytes\n", oname, out.n);
    (void)file;
    return 0;
}

/* Entry point. Reads options in any order, finds the kernel, source file and output type, picks the CPU, then calls build_image and the leak check. */
int main(int argc, char **argv)
{
    int kernel = -1, type = T_BIN, type_set = 0, saw_o = 0;
    const char *file = NULL, *outpath = NULL;
    PPOpts po;
    memset(&po, 0, sizeof po);
    int fix_src = 0, no_leak = 0, fix_main = 0, pponly = 0, quiet = 0, list = 0, dump = 0;
    char *listpath = NULL;
    char **pos = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    int npos = 0;
    char **inc = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    char **defs = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    char **undefs = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    char **pre = xmalloc(sizeof(char *) * (size_t)(argc + 1));
    int ni = 0, nd = 0, nu = 0, np = 0;
    int mbits = 0;
    int optimize = 1, depmode = 0;
    int cli_arch = -1, cli_rvext = 0, rv_generic = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "-?")) { fputs(usage_text, stdout); return 0; }
        if (!strcmp(a, "-v") || !strcmp(a, "--version") || !strcmp(a, "-version")) { printf("Comprinly %s\n", CLY_VERSION); return 0; }
        if (!strcmp(a, "-O0")) { optimize = 0; continue; }
        if (!strcmp(a, "-O") || !strcmp(a, "-O1") || !strcmp(a, "-O2") || !strcmp(a, "-O3") || !strcmp(a, "-Os")) { optimize = 1; continue; }
        if (!strcmp(a, "-cpu") || !strcmp(a, "--cpu") || !strcmp(a, "-arch") || !strcmp(a, "--arch")) {
            if (i + 1 >= argc) { fprintf(stderr, "cly: option `%s' needs a cpu name\n", a); return 1; }
            const char *nm = argv[++i];
            cli_arch = arch_parse(nm, &cli_rvext);
            rv_generic = arch_rv_generic(nm);
            if (cli_arch == -2) { fprintf(stderr, "cly: `%s' is not supported (32-bit ARM is not available; use arm64)\n", nm); return 1; }
            if (cli_arch < 0) { fprintf(stderr, "cly: unknown cpu `%s'; use x86-64, arm64 or risc-v\n", nm); return 1; }
            continue;
        }
        if (!strcmp(a, "-m32")) { mbits = 32; continue; }
        if (!strcmp(a, "-m64")) { mbits = 64; continue; }
        if (!strcmp(a, "-m16")) { mbits = 16; continue; }
        if (!strcmp(a, "--dump-insns")) { dump = 1; continue; }
        if (!strcmp(a, "--dump-opt")) { g_dumpopt = 1; continue; }
        if (!strcmp(a, "-o")) { type = T_OBJ; type_set = 1; g_dot_o = 1; saw_o = 1; continue; }
        if (!strcmp(a, "-out") || !strcmp(a, "--out")) {
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
        if (!strcmp(a, "-nasm")) { g_nasm = 1; continue; }
        if (!strcmp(a, "-nolibc")) { g_nolibc = 1; continue; }
        if (!strcmp(a, "-log")) { log_open(NULL); continue; }
        if (!strncmp(a, "-log=", 5)) { if (!log_open(a + 5)) fprintf(stderr, "cly: cannot open log file `%s'\n", a + 5); continue; }
        if (!strcmp(a, "-noleak")) { no_leak = 1; continue; }
        if (!strcmp(a, "-fix")) { fix_main = 1; continue; }
        if (!strcmp(a, "-fixsrc")) { fix_src = 1; continue; }
        if (!strcmp(a, "-w")) { g_nowarn = 1; continue; }
        if (!strcmp(a, "-Werror")) { g_werror = 1; continue; }
        if (!strcmp(a, "-M")) { depmode = 1; continue; }
        if (!strcmp(a, "-MD")) { depmode = 2; continue; }
        if (!strcmp(a, "-q")) { quiet = 1; g_hidewarn = 1; continue; }
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
    if (g_nasm) {
        g_nolibc = 1;
        no_leak = 1;
        optimize = 0;
        if (fix_main || fix_src) { fprintf(stderr, "cly: -fix and -fixsrc do not work with -nasm\n"); return 1; }
        if (cli_arch > 0) { fprintf(stderr, "cly: -nasm is x86 only; `-cpu' chose %s\n", arch_name(cli_arch)); return 1; }
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
        if (saw_o) fprintf(stderr, "cly: -o means write an object file (.o); use -out FILE to name the output\n");
        return 1;
    }
    if (kernel < 0 && file && !file_exists(file) && !dump) {
        fprintf(stderr, "cly: unknown kernel `%s'; use bare, windows, linux or mac\n", file);
        for (int k = 0; k < 4; k++)
            if (edit_distance(file, kernel_names[k]) <= 2) { fprintf(stderr, "cly: did you mean `%s'?\n", kernel_names[k]); break; }
        return 1;
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
    int rawtype = type == T_FLAT || type == T_COM || type == T_HEX || type == T_SREC || type == T_IMG;
    int arch = cli_arch, rvext = cli_rvext;
    int arch_from_file = 0;
    {
        size_t flen = 0;
        char *raw = read_file(file, &flen);
        if (raw) {
            LineVec tmp;
            memset(&tmp, 0, sizeof tmp);
            const char *p = raw;
            while (*p) {
                const char *nl = cly_nl(p);
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                char *t = xstrndup(p, len);
                lv_add(&tmp, t, file, tmp.n + 1);
                free(t);
                if (!nl) break;
                p = nl + 1;
            }
            char arg[64];
            if (arch_first_cpu(&tmp, arg, sizeof arg, NULL)) {
                int fx = 0;
                int fa = arch_parse(arg, &fx);
                if (fa == -2) { fprintf(stderr, "cly: `cpu %s' is not supported (32-bit ARM is not available; use arm64)\n", arg); return 1; }
                if (g_nasm && fa > 0) { fprintf(stderr, "cly: -nasm is x86 only; the source says `cpu %s'\n", arg); return 1; }
                if (fa >= 0) {
                    int ca = arch == ARCH_X86 ? 0 : arch == ARCH_A64 ? 1 : 2;
                    int cf = fa == ARCH_X86 ? 0 : fa == ARCH_A64 ? 1 : 2;
                    if (arch >= 0 && ca != cf) {
                        fprintf(stderr, "cly: the source says `cpu %s' but -cpu chose %s\n", arg, arch_name(arch));
                        return 1;
                    }
                    arch = fa;
                    rvext = fx;
                    rv_generic = arch_rv_generic(arg);
                    arch_from_file = 1;
                }
            }
            for (int i = 0; i < tmp.n; i++) free(tmp.v[i].text);
            free(tmp.v);
            free(raw);
        }
    }
    int guessed = 0;
    if (arch < 0) arch = ARCH_X86;
    else guessed = 1;
    (void)arch_from_file;
    int need_guess = !guessed && !g_nasm;
    int defbits;
    LineVec lines;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (arch == ARCH_RV64 && mbits == 32) {
            if (!rv_generic) { fprintf(stderr, "cly: -m32 does not fit cpu rv64; use rv32 or plain risc-v\n"); return 1; }
            arch = ARCH_RV32;
        } else if (arch == ARCH_RV32 && mbits == 64) {
            if (!rv_generic) { fprintf(stderr, "cly: -m64 does not fit cpu rv32; use rv64 or plain risc-v\n"); return 1; }
            arch = ARCH_RV64;
        }
        if (arch == ARCH_A64 && mbits && mbits != 64) { fprintf(stderr, "cly: ARM64 is 64-bit only; -m%d does not apply\n", mbits); return 1; }
        defbits = arch != ARCH_X86 ? arch_bits(arch) : (mbits ? mbits : (g_nasm && rawtype) ? 16 : ((kernel == K_BARE || rawtype) ? 32 : 64));
        po.outfmt = fmt_name(kernel, type, defbits);
        po.bits = defbits;
        po.arch = arch;
        memset(&lines, 0, sizeof lines);
        g_errors = 0;
        xlog("preprocess", "reading `%s' (%s, %d-bit)", file, arch_name(arch), defbits);
        if (!pp_run(file, &po, &lines) || g_errors) return 1;
        xlog("preprocess", "done, %d lines", lines.n);
        if (need_guess && attempt == 0) {
            int gx = 0;
            int ga = arch_guess(&lines, &gx);
            need_guess = 0;
            if (ga != ARCH_X86) {
                arch = ga;
                rvext = gx;
                continue;
            }
        }
        break;
    }
    if (!pponly && arch_check_foreign(&lines, arch)) { fprintf(stderr, "cly: %d error%s\n", g_errors, g_errors == 1 ? "" : "s"); return 1; }
    if (arch != ARCH_X86) {
        if (type == T_COM) { fprintf(stderr, "cly: -com is a 16-bit x86 format and does not apply to %s\n", arch_name(arch)); return 1; }
        if (arch != ARCH_A64 && (kernel == K_WINDOWS || kernel == K_MAC)) {
            fprintf(stderr, "cly: %s has no %s target; use linux or bare\n", arch_name(arch), kernel == K_WINDOWS ? "windows" : "mac");
            return 1;
        }
        if (type == T_IMG) type = T_FLAT;
    }
    if (!pponly && !g_nasm) sysval_run(&lines, arch, kernel, defbits);
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
    ao.bits = g_nasm ? defbits : mbits;
    ao.arch = arch;
    ao.rvext = rvext;
    ao.optimize = optimize;
    ao.list = list;
    int flat = type == T_FLAT || type == T_COM || type == T_HEX || type == T_SREC;
    ao.flat_mode = flat;
    if (type == T_COM) {
        lv_add(&ao.lines, "bits 16", "<com>", 1);
        if (!has_org(&lines)) lv_add(&ao.lines, "org 0x100", "<com>", 2);
    }
    for (int i = 0; i < lines.n; i++) lv_add(&ao.lines, lines.v[i].text, lines.v[i].file, lines.v[i].line);
    LineVec fixed;
    memset(&fixed, 0, sizeof fixed);
    int nleaks = 0, nfix = 0;
    if (arch == ARCH_X86 && !no_leak && !g_nowarn && !g_quiet) {
        int lb = detect_bits(&ao);
        if (lb == 32 || lb == 64) {
            xlog("leakcheck", "scanning %d-bit code", lb);
            nleaks = leak_check(&ao.lines, lb, &fixed, &nfix);
            xlog("leakcheck", "%d leak%s found", nleaks, nleaks == 1 ? "" : "s");
        }
    }
    char *base = strip_ext(file);
    if (list) {
        if (!listpath) {
            Str s = {0};
            sfmt(&s, "%s.lst", base);
            listpath = sget(&s);
        }
        ao.listpath = listpath;
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
    if (!strcmp(oname, file)) {
        fprintf(stderr, "cly: output would overwrite the input file `%s'\n", file);
        return 1;
    }
    int canfix = nfix && kernel == K_LINUX && (type == T_ELF || type == T_BIN);
    if (nleaks && !canfix && nfix) nfix = 0;
    if (fix_main && canfix) {
        ao.lines = fixed;
        nleaks = 0;
    }
    xlog("target", "kernel %d, output type %d, file `%s'", kernel, type, oname);
    int rc = build_image(&ao, arch, kernel, type, oname, file, quiet, optimize);
    if (!rc && g_werror && g_warnings) {
        fprintf(stderr, "cly: %d warning%s treated as error%s (-Werror)\n", g_warnings, g_warnings == 1 ? "" : "s", g_warnings == 1 ? "" : "s");
        remove(oname);
        return 1;
    }
    if (!rc && depmode) {
        Str ds = {0};
        sfmt(&ds, "%s: %s", oname, file);
        for (int i = 0; i < dep_count(); i++) { sadd(&ds, " "); sadd(&ds, dep_get(i)); }
        sadc(&ds, '\n');
        if (depmode == 1) fputs(ds.s, stdout);
        else {
            Str dn = {0};
            sfmt(&dn, "%s.d", oname);
            write_file(sget(&dn), ds.s, ds.n, 0);
        }
    }
    if (rc) { xlog("done", "failed with %d error%s, %d warning%s", g_errors, g_errors == 1 ? "" : "s", g_warnings, g_warnings == 1 ? "" : "s"); return rc; }
    if (nleaks && canfix && !fix_main) {
        Str fs = {0};
        if (outpath) sfmt(&fs, "%s_fixed", outpath);
        else {
            sfmt(&fs, "%s_fixed%s", base, ext_for(kernel, type));
        }
        char *fname = sget(&fs);
        if (fix_src) {
            Str ts = {0};
            for (int q = 0; q < fixed.n; q++) { sadd(&ts, fixed.v[q].text); sadc(&ts, '\n'); }
            Str pn = {0};
            sfmt(&pn, "%s.asm", fname);
            write_file(sget(&pn), ts.s ? ts.s : "", ts.n, 0);
        }
        AsmOpts ao2 = ao;
        ao2.lines = fixed;
        ao2.listpath = NULL;
        ao2.list = 0;
        if (build_image(&ao2, arch, kernel, type, fname, file, 1, optimize)) {
            fprintf(stderr, "cly: could not build the leak-free copy\n");
            return 1;
        }
        if (!quiet) printf("%s: leak-free copy, %zu bytes\n", fname, g_last_size);
    }
    return 0;
}
