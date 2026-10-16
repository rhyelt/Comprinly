/* System call checks. Follows the number loaded into rax/eax (x8, a7) before each syscall and warns about Linux numbers that do not exist. */
#include "cly.h"
#include <ctype.h>

typedef struct { char name[48]; long v; } Eq;

/* Skips spaces and tabs. */
static const char *skip(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Reads a number or an equ name (with optional sign and # prefix). Returns 0 if the text is anything more complex. */
static int parse_num(const char *s, long *out, const Eq *eq, int neq)
{
    s = skip(s);
    if (*s == '#') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (isdigit((unsigned char)*s)) {
        char *e;
        long v = strtol(s, &e, 0);
        if (*e && *e != ' ' && *e != '\t' && *e != ';') return 0;
        *out = neg ? -v : v;
        return 1;
    }
    size_t l = 0;
    while (ident_char((unsigned char)s[l])) l++;
    if (!l) return 0;
    const char *e = skip(s + l);
    if (*e && *e != ';') return 0;
    for (int i = 0; i < neq; i++)
        if (strlen(eq[i].name) == l && !strncasecmp(eq[i].name, s, l)) { *out = neg ? -eq[i].v : eq[i].v; return 1; }
    return 0;
}

/* True for jumps, calls and returns on any CPU. A known register value does not survive them. */
static int is_flow(const char *m)
{
    static const char *const f[] = { "call", "ret", "retn", "retf", "jmp", "tail", "b", "bl", "br", "blr", "cbz", "cbnz", "tbz", "tbnz", "jal", "jalr", "jr", "j", "loop", "loope", "loopne", "beq", "bne", "blt", "bge", "bltu", "bgeu", "beqz", "bnez", "blez", "bgez", "bltz", "bgtz", "bgt", "ble", "bgtu", "bleu", "iret", "iretq", NULL };
    for (int i = 0; f[i]; i++) if (!strcasecmp(m, f[i])) return 1;
    if ((m[0] == 'j' || m[0] == 'J') && strlen(m) <= 4) return 1;
    if ((m[0] == 'b' || m[0] == 'B') && m[1] == '.') return 1;
    return 0;
}

/* Splits operand text at top-level commas, ignoring commas inside brackets. */
static int split(char *s, char **ops, int max)
{
    int n = 0;
    int depth = 0;
    char *st = s;
    for (char *p = s;; p++) {
        if (*p == '[' || *p == '(') depth++;
        else if (*p == ']' || *p == ')') depth--;
        if ((*p == ',' && depth == 0) || !*p) {
            int end = !*p;
            *p = 0;
            if (n < max) ops[n++] = (char *)skip(st);
            st = p + 1;
            if (end) break;
        }
    }
    return n;
}

/* True for the register that carries the call number on this CPU. */
static int is_numreg(int arch, const char *r)
{
    if (arch == ARCH_X86) return !strcasecmp(r, "eax") || !strcasecmp(r, "rax");
    if (arch == ARCH_A64) return !strcasecmp(r, "x8") || !strcasecmp(r, "w8");
    return !strcasecmp(r, "a7") || !strcasecmp(r, "x17");
}

/* True for al, ah and ax. Writing them makes the call number unknown. */
static int x86_partial(const char *r)
{
    return !strcasecmp(r, "al") || !strcasecmp(r, "ah") || !strcasecmp(r, "ax");
}

/* True if n is a real Linux call number for the CPU and word size. */
static int linux_valid(int arch, long n, int bits)
{
    if (n < 0) return 0;
    if (arch == ARCH_X86) {
        if (bits == 32) return n <= 462;
        return n <= 334 || (n >= 424 && n <= 462);
    }
    if (n > 462) return 0;
    if (n >= 244 && n <= 259) return arch != ARCH_A64 && (n == 258 || n == 259);
    return 1;
}

/* True if the instruction changes its first operand, so a write to the number register invalidates what we knew. */
static int writes_first(int arch, const char *m)
{
    if (is_flow(m)) return 0;
    if (arch == ARCH_X86) return strcasecmp(m, "cmp") && strcasecmp(m, "test") && strcasecmp(m, "push");
    if (arch == ARCH_A64) return strncasecmp(m, "st", 2) && strcasecmp(m, "cmp") && strcasecmp(m, "cmn") && strcasecmp(m, "tst") && strcasecmp(m, "ccmp") && strcasecmp(m, "ccmn") && strcasecmp(m, "prfm");
    return !(m[0] == 's' && (m[1] == 'b' || m[1] == 'h' || m[1] == 'w' || m[1] == 'd') && !m[2]) && strncasecmp(m, "fs", 2);
}

/* Name of a Linux call number, or NULL. Used in warning text. */
static const char *sys_name(long n)
{
    switch (n) {
    case 0: return "read"; case 1: return "write"; case 2: return "open"; case 3: return "close";
    case 9: return "mmap"; case 12: return "brk"; case 35: return "nanosleep"; case 39: return "getpid";
    case 60: return "exit"; case 231: return "exit_group";
    }
    return NULL;
}

/* Walks the lines, keeps the last constant loaded into the call number register and warns when a syscall, svc or ecall number is not a real Linux number. Only Linux calls are checked; other kernels get exactly what the program sends. */
void sysval_run(const LineVec *lv, int arch, int kernel, int bits)
{
    Eq *eq = NULL;
    int neq = 0, ceq = 0;
    for (int i = 0; i < lv->n; i++) {
        const char *t = skip(lv->v[i].text);
        size_t l = 0;
        while (ident_char((unsigned char)t[l])) l++;
        if (!l || l >= sizeof eq[0].name) continue;
        const char *q = skip(t + l);
        if (strncasecmp(q, "equ", 3) || ident_char((unsigned char)q[3])) continue;
        long v;
        if (!parse_num(q + 3, &v, eq, neq)) continue;
        if (neq == ceq) { ceq = ceq ? ceq * 2 : 16; eq = xrealloc(eq, sizeof(Eq) * (size_t)ceq); }
        memcpy(eq[neq].name, t, l);
        eq[neq].name[l] = 0;
        eq[neq].v = v;
        neq++;
    }
    int known = 0;
    long num = 0;
    for (int i = 0; i < lv->n; i++) {
        char buf[512];
        snprintf(buf, sizeof buf, "%s", lv->v[i].text);
        char *sc = strchr(buf, ';');
        if (sc) *sc = 0;
        char *p = (char *)skip(buf);
        if (!*p || *p == '%') continue;
        size_t l = 0;
        while (ident_char((unsigned char)p[l]) || p[l] == '.') l++;
        if (!l) continue;
        const char *a = skip(p + l);
        if (*a == ':') { known = 0; p = (char *)skip(a + 1); l = 0; while (ident_char((unsigned char)p[l]) || p[l] == '.') l++; if (!l) continue; }
        char mn[48];
        if (l >= sizeof mn) continue;
        memcpy(mn, p, l);
        mn[l] = 0;
        char *rest = p + l;
        if (!arch_known(arch, mn)) {
            const char *r = skip(rest);
            if (!*r) known = 0;
            continue;
        }
        char *ops[8];
        int n = split(rest, ops, 8);
        if (n == 1 && !*ops[0]) n = 0;
        if (n == 1 && !strcasecmp(mn, "mov")) {
            char *w = ops[0];
            while (*w && *w != ' ' && *w != '\t') w++;
            if (*w) { *w++ = 0; ops[1] = (char *)skip(w); n = 2; }
        }
        for (int k = 0; k < n; k++) { char *e = ops[k] + strlen(ops[k]); while (e > ops[k] && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0; }
        int isinsn = 0;
        long arg = 0;
        const char *what = NULL;
        if (arch == ARCH_X86 && !strcasecmp(mn, "syscall") && n == 0) { isinsn = 1; what = "syscall"; }
        else if (arch == ARCH_X86 && !strcasecmp(mn, "int") && n == 1 && parse_num(ops[0], &arg, eq, neq) && arg == 0x80) { isinsn = 2; what = "int 0x80"; }
        else if (arch == ARCH_A64 && !strcasecmp(mn, "svc") && n == 1) { isinsn = 1; what = "svc"; parse_num(ops[0], &arg, eq, neq); }
        else if ((arch == ARCH_RV64 || arch == ARCH_RV32) && !strcasecmp(mn, "ecall") && n == 0) { isinsn = 1; what = "ecall"; }
        g_pos.file = lv->v[i].file;
        g_pos.line = lv->v[i].line;
        if (isinsn) {
            if (arch == ARCH_X86 && isinsn == 2) {
                if (bits == 64) warn("`int 0x80' is the 32-bit Linux call and truncates addresses in 64-bit code; use `syscall'");
                else if (kernel != K_LINUX) warn("`int 0x80' only works on Linux; use `syscall' for other targets");
            }
            if (arch == ARCH_A64 && arg != 0) warn("Linux ignores the `svc' number; use `svc 0' and put the call number in x8");
            if (known && kernel == K_LINUX) {
                const char *nm = sys_name(num);
                char desc[80];
                if (nm) snprintf(desc, sizeof desc, "%ld (%s)", num, nm);
                else snprintf(desc, sizeof desc, "%ld", num);
                if (!linux_valid(arch, num, arch == ARCH_X86 && bits == 32 && isinsn == 1 ? 64 : bits)) warn("%s is not a Linux system call number on %s", desc, arch_name(arch));
            }
            if (arch == ARCH_X86) known = 0;
            (void)what;
            continue;
        }
        if (n >= 1 && is_numreg(arch, ops[0])) {
            long v;
            if (arch == ARCH_X86 && !strcasecmp(mn, "xor") && n == 2 && !strcasecmp(ops[0], ops[1])) { known = 1; num = 0; }
            else if ((!strcasecmp(mn, "mov") || !strcasecmp(mn, "li")) && n == 2 && parse_num(ops[1], &v, eq, neq)) { known = 1; num = v; }
            else if (arch != ARCH_X86 && !strcasecmp(mn, "addi") && n == 3 && !strcasecmp(ops[1], "zero") && parse_num(ops[2], &v, eq, neq)) { known = 1; num = v; }
            else if (writes_first(arch, mn)) known = 0;
        } else if (arch == ARCH_X86 && n >= 1 && x86_partial(ops[0])) known = 0;
        else if (arch == ARCH_X86 && (!strcasecmp(mn, "xchg") || !strcasecmp(mn, "cpuid") || !strcasecmp(mn, "pop") || !strcasecmp(mn, "mul") || !strcasecmp(mn, "div") || !strcasecmp(mn, "imul") || !strcasecmp(mn, "idiv") || !strcasecmp(mn, "cdq") || !strcasecmp(mn, "cqo") || !strcasecmp(mn, "cdqe") || !strcasecmp(mn, "cwde") || !strcasecmp(mn, "lodsb") || !strcasecmp(mn, "lodsd") || !strcasecmp(mn, "lodsq") || !strcasecmp(mn, "rdtsc") || !strcasecmp(mn, "cmpxchg") || !strcasecmp(mn, "cmpxchg8b") || !strcasecmp(mn, "cmpxchg16b") || !strcasecmp(mn, "lahf") || !strcasecmp(mn, "xlat") || !strcasecmp(mn, "xlatb") || !strcasecmp(mn, "lodsw") || !strcasecmp(mn, "rdtscp") || !strcasecmp(mn, "rdmsr") || !strcasecmp(mn, "rdpmc") || !strcasecmp(mn, "xgetbv") || !strcasecmp(mn, "rep") || !strcasecmp(mn, "repe") || !strcasecmp(mn, "repne"))) known = 0;
        if (is_flow(mn)) known = 0;
    }
    g_pos.file = NULL;
    free(eq);
}
