#include "cly.h"

const char *arch_name(int arch)
{
    switch (arch) {
    case ARCH_A64: return "arm64";
    case ARCH_RV64: return "riscv64";
    case ARCH_RV32: return "riscv32";
    }
    return "x86-64";
}

int arch_bits(int arch)
{
    return arch == ARCH_RV32 ? 32 : 64;
}

static void lower(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; s[i] && i + 1 < n; i++) d[i] = (char)tolower((unsigned char)s[i]);
    d[i] = 0;
}

static int rv_ext_parse(const char *s, int *mask)
{
    int m = 0;
    int any = 0;
    const char *p = s;
    while (*p) {
        if (*p == '_') { p++; continue; }
        if (*p == 'z' || *p == 's' || *p == 'x') {
            char w[32];
            size_t n = 0;
            while (*p && *p != '_' && n < sizeof w - 1) w[n++] = *p++;
            w[n] = 0;
            while (n && (isdigit((unsigned char)w[n - 1]) || w[n - 1] == 'p')) w[--n] = 0;
            if (!strcmp(w, "zicsr")) m |= RVX_ZICSR;
            else if (!strcmp(w, "zifencei")) m |= RVX_ZIFENCEI;
            else if (!strcmp(w, "zba")) m |= RVX_ZBA;
            else if (!strcmp(w, "zbb")) m |= RVX_ZBB;
            else if (!strcmp(w, "zbs")) m |= RVX_ZBS;
            else if (!strcmp(w, "zbc")) m |= RVX_ZBC;
            else if (!strcmp(w, "zicond")) m |= RVX_ZICOND;
            else if (!strcmp(w, "zfh")) m |= RVX_ZFH | RVX_ZFHMIN | RVX_F;
            else if (!strcmp(w, "zfhmin")) m |= RVX_ZFHMIN | RVX_F;
            else if (!strcmp(w, "zbkb")) m |= RVX_ZBKB;
            else if (!strcmp(w, "zbkc")) m |= RVX_ZBC;
            else if (!strcmp(w, "zbkx")) m |= RVX_ZBKX;
            else if (!strcmp(w, "zknd")) m |= RVX_ZKND;
            else if (!strcmp(w, "zkne")) m |= RVX_ZKNE;
            else if (!strcmp(w, "zknh")) m |= RVX_ZKNH;
            else if (!strcmp(w, "zksed")) m |= RVX_ZKSED;
            else if (!strcmp(w, "zksh")) m |= RVX_ZKSH;
            else if (!strcmp(w, "zkn")) m |= RVX_ZBKB | RVX_ZBC | RVX_ZBKX | RVX_ZKND | RVX_ZKNE | RVX_ZKNH;
            else if (!strcmp(w, "zks")) m |= RVX_ZBKB | RVX_ZBC | RVX_ZBKX | RVX_ZKSED | RVX_ZKSH;
            else if (!strcmp(w, "zk")) m |= RVX_ZBKB | RVX_ZBKX | RVX_ZKND | RVX_ZKNE | RVX_ZKNH;
            else if (!strcmp(w, "zicbom")) m |= RVX_ZICBOM;
            else if (!strcmp(w, "zicboz")) m |= RVX_ZICBOZ;
            else if (!strcmp(w, "zicbop")) m |= RVX_ZICBOP;
            else if (!strcmp(w, "zawrs")) m |= RVX_ZAWRS;
            else if (!strcmp(w, "svinval")) m |= RVX_SVINVAL;
            else if (!strcmp(w, "zihintpause") || !strcmp(w, "zihintntl") || !strcmp(w, "zicntr") || !strcmp(w, "zihpm") || !strcmp(w, "ziccif") || !strcmp(w, "zmmul") || !strcmp(w, "zca") || !strcmp(w, "zcb") || !strcmp(w, "sstc") || !strcmp(w, "svinval") || !strcmp(w, "svnapot") || !strcmp(w, "svpbmt")) {
            } else return -1;
            any = 1;
            continue;
        }
        switch (*p) {
        case 'i': m |= RVX_I; break;
        case 'e': m |= RVX_I; break;
        case 'm': m |= RVX_M; break;
        case 'a': m |= RVX_A; break;
        case 'f': m |= RVX_F | RVX_ZICSR; break;
        case 'd': m |= RVX_D | RVX_F | RVX_ZICSR; break;
        case 'g': m |= RVX_I | RVX_M | RVX_A | RVX_F | RVX_D | RVX_ZICSR | RVX_ZIFENCEI; break;
        case 'c': m |= RVX_C; break;
        case 'b': m |= RVX_ZBA | RVX_ZBB | RVX_ZBS; break;
        case 'h': m |= RVX_H; break;
        default:
            if (isdigit((unsigned char)*p) || *p == 'p') break;
            return -1;
        }
        any = 1;
        p++;
    }
    if (!any) return -1;
    *mask = m | RVX_I;
    return 0;
}

int arch_parse(const char *name, int *rvext)
{
    char s[64];
    lower(s, name, sizeof s);
    if (rvext) *rvext = RVX_DEFAULT;
    static const char *x86n[] = { "x86-64", "x86_64", "x64", "amd64", "x86", "ia32", "i386", "i486", "i586", "i686", "intel64", "em64t", "x86-32", "x86_32", NULL };
    for (int i = 0; x86n[i]; i++) if (!strcmp(s, x86n[i])) return ARCH_X86;
    static const char *a64n[] = { "arm64", "aarch64", "armv8", "armv8-a", "armv8a", "armv8.0-a", "arm64e", "armv9", "armv9-a", "armv9a", "arm-64", "a64", "armv8.1-a", "armv8.2-a", "armv8.3-a", "armv8.4-a", "armv8.5-a", "armv8.6-a", "armv8.7-a", "armv8.8-a", "armv9.0-a", "armv9.1-a", "armv9.2-a", NULL };
    for (int i = 0; a64n[i]; i++) if (!strcmp(s, a64n[i])) return ARCH_A64;
    static const char *armold[] = { "arm", "arm32", "armv7", "armv7-a", "armv6", "armv5", "thumb", "thumb2", "aarch32", "armv7a", NULL };
    for (int i = 0; armold[i]; i++) if (!strcmp(s, armold[i])) return -2;
    char t[64];
    size_t n = 0;
    for (const char *q = s; *q && n < sizeof t - 1; q++) {
        if (!strncmp(q, "risc-v", 6)) { memcpy(t + n, "riscv", 5); n += 5; q += 5; continue; }
        t[n++] = *q;
    }
    t[n] = 0;
    const char *rest = NULL;
    if (!strncmp(t, "riscv", 5)) rest = t + 5;
    else if (!strncmp(t, "rv", 2) && (isdigit((unsigned char)t[2]) || !t[2])) rest = t + 2;
    if (!rest) return -1;
    int arch = ARCH_RV64;
    if (!strncmp(rest, "32", 2)) { arch = ARCH_RV32; rest += 2; }
    else if (!strncmp(rest, "64", 2)) { arch = ARCH_RV64; rest += 2; }
    else if (!strncmp(rest, "128", 3)) return -2;
    if (*rest == '-' || *rest == '_') rest++;
    if (*rest) {
        int m = 0;
        if (rv_ext_parse(rest, &m) < 0) return -1;
        if (rvext) *rvext = m;
    }
    return arch;
}

int arch_rv_generic(const char *name)
{
    char s[64];
    lower(s, name, sizeof s);
    if (!strncmp(s, "risc-v", 6)) memmove(s + 4, s + 5, strlen(s + 5) + 1);
    const char *r = NULL;
    if (!strncmp(s, "riscv", 5)) r = s + 5;
    else if (!strncmp(s, "rv", 2)) r = s + 2;
    if (!r) return 0;
    if (!strncmp(r, "32", 2) || !strncmp(r, "64", 2) || !strncmp(r, "128", 3)) return 0;
    return 1;
}

int arch_cpu_line(const char *text, char *arg, size_t sz)
{
    const char *p = text;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '[') {
        p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    if (strncasecmp(p, "cpu", 3) || (p[3] != ' ' && p[3] != '\t')) return 0;
    p += 3;
    while (*p == ' ' || *p == '\t') p++;
    size_t n = 0;
    while (*p && *p != ';' && *p != ']' && *p != ' ' && *p != '\t' && *p != ',' && n + 1 < sz) arg[n++] = *p++;
    arg[n] = 0;
    return n > 0;
}

static int blank_or_comment(const char *t)
{
    while (*t == ' ' || *t == '\t') t++;
    return !*t || *t == ';';
}

int arch_first_cpu(const LineVec *lv, char *arg, size_t sz, int *lineno)
{
    for (int i = 0; i < lv->n; i++) {
        const char *t = lv->v[i].text;
        if (blank_or_comment(t)) continue;
        if (lineno) *lineno = i;
        return arch_cpu_line(t, arg, sz);
    }
    return 0;
}

int arch_known(int arch, const char *mn)
{
    if (arch == ARCH_A64) return a64_known(mn);
    if (arch == ARCH_RV64 || arch == ARCH_RV32) return rv_known(mn, RVX_DEFAULT);
    return insn_known(mn);
}

int arch_insn(int arch, const char *mn, const char *rest, IC *ic, InsnOut *out)
{
    if (arch == ARCH_A64) return a64_insn(mn, rest, ic, out);
    return rv_insn(mn, rest, ic, out);
}

static int is_rv_reg(const char *s, size_t n)
{
    static const char *names[] = { "zero", "ra", "gp", "tp", "fp", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7", "fa0", "fa1", "fa2", "fa3", "fs0", "fs1", NULL };
    for (int i = 0; names[i]; i++) if (strlen(names[i]) == n && !strncasecmp(names[i], s, n)) return 1;
    return 0;
}

int g_guess_line;
char g_guess_tok[64];

static int is_a64_only_reg(const char *s, size_t n)
{
    if (n == 3 && (!strncasecmp(s, "xzr", 3) || !strncasecmp(s, "wzr", 3))) return 1;
    if (n >= 2 && n <= 3 && (s[0] == 'w' || s[0] == 'W' || s[0] == 'v' || s[0] == 'V') && isdigit((unsigned char)s[1])) {
        int v = s[1] - '0';
        if (n == 3) { if (!isdigit((unsigned char)s[2])) return 0; v = v * 10 + s[2] - '0'; }
        return v <= (s[0] == 'w' || s[0] == 'W' ? 30 : 31);
    }
    return 0;
}

static int is_x_reg(const char *s, size_t n)
{
    if (n < 2 || n > 3 || (s[0] != 'x' && s[0] != 'X') || !isdigit((unsigned char)s[1])) return 0;
    int v = s[1] - '0';
    if (n == 3) { if (!isdigit((unsigned char)s[2])) return 0; v = v * 10 + s[2] - '0'; }
    return v <= 31;
}

static int is_rv_only_reg(const char *s, size_t n)
{
    if (is_rv_reg(s, n)) {
        if (n == 2 && !strncasecmp(s, "s0", 2)) return 0;
        if (n == 2 && !strncasecmp(s, "s1", 2)) return 0;
        return 1;
    }
    if (n >= 3 && n <= 4 && (s[0] == 'f' || s[0] == 'F') && (s[1] == 't' || s[1] == 'T' || s[1] == 's' || s[1] == 'S' || s[1] == 'a' || s[1] == 'A') && isdigit((unsigned char)s[2])) return 1;
    return 0;
}

static int is_x86_reg_tok(const char *s, size_t n)
{
    if (n < 2 || n > 8) return 0;
    char tmp[16];
    int sz;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    if (!strcasecmp(tmp, "sp") || !strcasecmp(tmp, "al") || !strcasecmp(tmp, "fp") || !strcasecmp(tmp, "ra") || !strcasecmp(tmp, "gp") || !strcasecmp(tmp, "tp")) return 0;
    reg_mode(64);
    int rg = reg_lookup(tmp, &sz);
    if (rg <= 0) return 0;
    int c = REGCLS(rg);
    return c == RC_R8 || c == RC_R16 || c == RC_R32 || c == RC_R64 || c == RC_XMM || c == RC_YMM || c == RC_ZMM || c == RC_RIP;
}

static const char *line_mnemonic(const char *t, char *w, size_t sz, const char **ops)
{
    while (*t == ' ' || *t == '\t') t++;
    if (!*t || *t == ';' || *t == '%') return NULL;
    const char *p = t;
    size_t n = 0;
    while ((ident_char((unsigned char)*p) || *p == '.') && n < sz - 1) w[n++] = *p++;
    w[n] = 0;
    if (!n) return NULL;
    const char *q = p;
    while (*q == ' ' || *q == '\t') q++;
    if (*q != ':') {
        static const char *const dd[] = { "equ", "db", "dw", "dd", "dq", "dt", "do", "dy", "dz", "resb", "resw", "resd", "resq", "rest", "reso", "resy", "resz", "times", "incbin", "istruc", "struc", NULL };
        const char *e = q;
        while (ident_char((unsigned char)*e)) e++;
        for (int i = 0; dd[i]; i++) if (strlen(dd[i]) == (size_t)(e - q) && !strncasecmp(dd[i], q, (size_t)(e - q))) return NULL;
    }
    if (*q == ':') {
        p = q + 1;
        while (*p == ' ' || *p == '\t') p++;
        n = 0;
        while ((ident_char((unsigned char)*p) || *p == '.') && n < sz - 1) w[n++] = *p++;
        w[n] = 0;
        if (!n) return NULL;
    }
    *ops = p;
    return w;
}

static int tok_clue(const char *b, size_t l)
{
    if (is_a64_only_reg(b, l)) return ARCH_A64;
    if (is_rv_only_reg(b, l)) return ARCH_RV64;
    if (is_x86_reg_tok(b, l)) return ARCH_X86;
    return -1;
}

static void note_guess(int line, const char *b, size_t l)
{
    g_guess_line = line;
    size_t k = l < sizeof g_guess_tok - 1 ? l : sizeof g_guess_tok - 1;
    memcpy(g_guess_tok, b, k);
    g_guess_tok[k] = 0;
}

int arch_guess(const LineVec *lv, int *rvext)
{
    int xregs = 0;
    int xline = 0;
    if (rvext) *rvext = RVX_DEFAULT;
    insn_init();
    g_guess_line = 0;
    g_guess_tok[0] = 0;
    for (int i = 0; i < lv->n; i++) {
        char w[64];
        const char *p;
        if (!line_mnemonic(lv->v[i].text, w, sizeof w, &p)) continue;
        if (!strcasecmp(w, "cpu") || !strcasecmp(w, "bits") || !strcasecmp(w, "section") || !strcasecmp(w, "segment") || !strcasecmp(w, "global") || !strcasecmp(w, "extern") || !strcasecmp(w, "times") || !strcasecmp(w, "org") || !strcasecmp(w, "align") || !strcasecmp(w, "default") || !strcasecmp(w, "use16") || !strcasecmp(w, "use32") || !strcasecmp(w, "use64") || !strcasecmp(w, "at") || !strcasecmp(w, "istruc") || !strcasecmp(w, "iend")) {
            if (!strcasecmp(w, "bits") || !strcasecmp(w, "use16") || !strcasecmp(w, "use32") || !strcasecmp(w, "use64")) { note_guess(lv->v[i].line, w, strlen(w)); return ARCH_X86; }
            continue;
        }
        int x86 = insn_known(w);
        int a = a64_known(w);
        int r = rv_known(w, RVX_DEFAULT);
        if (x86 && !a && !r) { note_guess(lv->v[i].line, w, strlen(w)); return ARCH_X86; }
        if (a && !x86 && !r) { note_guess(lv->v[i].line, w, strlen(w)); return ARCH_A64; }
        if (r && !x86 && !a) { note_guess(lv->v[i].line, w, strlen(w)); return ARCH_RV64; }
        const char *e = p;
        while (*e && *e != ';') {
            if (*e == '"' || *e == '\'') { char qc = *e++; while (*e && *e != qc) e++; if (*e) e++; continue; }
            if (ident_char((unsigned char)*e)) {
                const char *b = e;
                while (ident_char((unsigned char)*e)) e++;
                size_t l = (size_t)(e - b);
                if (b > p && b[-1] == '.') continue;
                int c = tok_clue(b, l);
                if (c >= 0) { note_guess(lv->v[i].line, b, l); return c; }
                if (is_x_reg(b, l) && !xregs) { xregs = 1; xline = lv->v[i].line; }
            } else e++;
        }
    }
    if (xregs) { g_guess_line = xline; snprintf(g_guess_tok, sizeof g_guess_tok, "x0"); return ARCH_A64; }
    return ARCH_X86;
}

static int label_defined(const LineVec *lv, const char *name, size_t l)
{
    for (int i = 0; i < lv->n; i++) {
        const char *t = lv->v[i].text;
        while (*t == ' ' || *t == '\t') t++;
        if (!strncasecmp(t, name, l) && !ident_char((unsigned char)t[l])) {
            const char *q = t + l;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == ':' || !strncasecmp(q, "equ", 3) || !strncasecmp(q, "db", 2) || !strncasecmp(q, "dw", 2) || !strncasecmp(q, "dd", 2) || !strncasecmp(q, "dq", 2) || !strncasecmp(q, "res", 3) || !*q || *q == ';') return 1;
        }
    }
    return 0;
}

int arch_check_foreign(const LineVec *lv, int arch)
{
    int nerr = 0;
    int shown = 0;
    insn_init();
    for (int i = 0; i < lv->n; i++) {
        char w[64];
        const char *p;
        if (!line_mnemonic(lv->v[i].text, w, sizeof w, &p)) continue;
        if (!arch_known(arch, w)) continue;
        const char *e = p;
        while (*e && *e != ';') {
            if (*e == '"' || *e == '\'') { char qc = *e++; while (*e && *e != qc) e++; if (*e) e++; continue; }
            if (!ident_char((unsigned char)*e)) { e++; continue; }
            const char *b = e;
            while (ident_char((unsigned char)*e)) e++;
            size_t l = (size_t)(e - b);
            if (b > p && b[-1] == '.') continue;
            int c = tok_clue(b, l);
            if (c < 0 && arch == ARCH_X86 && is_x_reg(b, l)) c = ARCH_A64;
            if (c < 0) continue;
            int same = (c == ARCH_RV64 && (arch == ARCH_RV64 || arch == ARCH_RV32)) || c == arch;
            if (same) continue;
            if (arch == ARCH_RV64 || arch == ARCH_RV32) { if (is_a64_only_reg(b, l) && 0) continue; }
            if (arch == ARCH_A64 && is_rv_only_reg(b, l) && (l <= 3) && (b[0] == 's' || b[0] == 'S' || b[0] == 'd' || b[0] == 'D')) continue;
            if (label_defined(lv, b, l)) continue;
            const char *on = c == ARCH_X86 ? "an x86" : c == ARCH_A64 ? "an ARM64" : "a RISC-V";
            g_pos.file = lv->v[i].file;
            g_pos.line = lv->v[i].line;
            if (g_guess_line && shown == 0) err("`%.*s' is %s register but the cpu is %s (chosen from `%s' on line %d; put a `cpu' line first to choose by hand)", (int)l, b, on, arch_name(arch), g_guess_tok, g_guess_line);
            else err("`%.*s' is %s register but the cpu is %s", (int)l, b, on, arch_name(arch));
            shown++;
            nerr++;
            break;
        }
    }
    g_pos.file = NULL;
    return nerr;
}
