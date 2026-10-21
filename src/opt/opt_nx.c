/* Small optimizer for ARM64 and RISC-V text: removes no-ops, dead code after jumps, jumps to the next line, chains of jumps and a few other safe patterns. */
#include "cly.h"
#include <ctype.h>

enum { N_BLANK, N_INSN, N_OTHER };

typedef struct {
    int kind;
    char *label;
    char mn[24];
    char *ops[6];
    int nops;
    char *scope;
    int dead;
    int changed;
} NL;

typedef struct {
    LineVec *lv;
    int n;
    NL *L;
    int arch;
    int a64;
} NC;

/* Skips blanks. */
static const char *sk(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int idch(int c) { return isalnum(c) || c == '_' || c == '.' || c == '$' || c == '@' || c == '?'; }

/* Releases a parsed line. */
static void free_nl(NL *l)
{
    free(l->label);
    free(l->scope);
    for (int i = 0; i < l->nops; i++) free(l->ops[i]);
    memset(l, 0, sizeof *l);
}

/* Copy of n characters without surrounding blanks. */
static char *trimdup(const char *s, size_t n)
{
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    char *r = xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

/* Parses a line into label, mnemonic and operands. */
static void parse_nl(NC *c, const char *text, NL *L)
{
    memset(L, 0, sizeof *L);
    const char *p = sk(text);
    if (!*p || *p == ';') { L->kind = N_BLANK; return; }
    if (*p == '%' || *p == '[') { L->kind = N_OTHER; return; }
    const char *w = p;
    while (idch((unsigned char)*p)) p++;
    size_t wl = (size_t)(p - w);
    if (wl && *p == ':' && p[1] != ':') {
        L->label = trimdup(w, wl);
        p = sk(p + 1);
        if (!*p || *p == ';') { L->kind = N_BLANK; return; }
        w = p;
        while (idch((unsigned char)*p)) p++;
        wl = (size_t)(p - w);
    }
    if (!wl || wl >= sizeof L->mn || !arch_known(c->arch, "nop") || !arch_known(c->arch, "ret")) { L->kind = N_OTHER; return; }
    memcpy(L->mn, w, wl);
    L->mn[wl] = 0;
    for (char *q = L->mn; *q; q++) *q = (char)tolower((unsigned char)*q);
    if (!arch_known(c->arch, L->mn)) { L->kind = N_OTHER; return; }
    const char *rest = sk(p);
    char tmp[512];
    {
        size_t k = 0;
        while (rest[k] && k < sizeof tmp - 1) { tmp[k] = rest[k]; k++; }
        tmp[k] = 0;
    }
    char *sc = strchr(tmp, ';');
    if (sc) *sc = 0;
    if (strchr(tmp, '"') || strchr(tmp, '\'') || strchr(tmp, '$') || strchr(tmp, '{')) { L->kind = N_OTHER; return; }
    L->kind = N_INSN;
    char *s = tmp;
    int depth = 0;
    char *st = s;
    if (!*sk(s)) return;
    for (char *q = s;; q++) {
        if (*q == '[' || *q == '(') depth++;
        else if (*q == ']' || *q == ')') depth--;
        if ((*q == ',' && depth == 0) || !*q) {
            if (L->nops >= 6) { L->kind = N_OTHER; return; }
            L->ops[L->nops++] = trimdup(st, (size_t)(q - st));
            if (!*q) break;
            st = q + 1;
        }
    }
}

/* Normalized operand text for comparing (no spaces, lower case). */
static char *normop(const char *s)
{
    char *r = xmalloc(strlen(s) + 1);
    size_t k = 0;
    for (; *s; s++) {
        if (*s == ' ' || *s == '\t' || *s == '#') continue;
        r[k++] = (char)tolower((unsigned char)*s);
    }
    r[k] = 0;
    return r;
}

/* True if two operands are the same after normalizing. */
static int opeq(const char *a, const char *b)
{
    char *x = normop(a), *y = normop(b);
    int r = !strcmp(x, y);
    free(x);
    free(y);
    return r;
}

/* Rebuilds the line text from its parts after a change. */
static void rebuild(NC *c, int i)
{
    NL *L = &c->L[i];
    char buf[640];
    size_t n = 0;
    n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s", L->label ? L->label : "", L->label ? ": " : "    ");
    if (L->kind == N_INSN) {
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s", L->mn);
        for (int k = 0; k < L->nops && n < sizeof buf - 2; k++) n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s", k ? ", " : " ", L->ops[k]);
    }
    free(c->lv->v[i].text);
    c->lv->v[i].text = xstrdup(buf);
}

/* Turns an instruction into nothing (the label on the line stays). */
static void kill_insn(NC *c, int i)
{
    NL *L = &c->L[i];
    for (int k = 0; k < L->nops; k++) free(L->ops[k]);
    L->nops = 0;
    L->kind = N_BLANK;
    L->mn[0] = 0;
    if (!L->label) { free(c->lv->v[i].text); c->lv->v[i].text = xstrdup(""); }
    else rebuild(c, i);
    L->changed = 1;
}

/* Replaces an instruction with a new mnemonic and operands. */
static void set_insn(NC *c, int i, const char *mn, int nops, const char *a, const char *b, const char *d)
{
    NL *L = &c->L[i];
    const char *v[3] = { a, b, d };
    char *nw[3] = { NULL, NULL, NULL };
    for (int k = 0; k < nops; k++) nw[k] = xstrdup(v[k]);
    for (int k = 0; k < L->nops; k++) free(L->ops[k]);
    snprintf(L->mn, sizeof L->mn, "%s", mn);
    L->nops = nops;
    for (int k = 0; k < nops; k++) L->ops[k] = nw[k];
    rebuild(c, i);
}

/* Records the enclosing label of every line for local label handling. */
static void compute_scopes(NC *c)
{
    char *cur = xstrdup("");
    for (int i = 0; i < c->n; i++) {
        NL *L = &c->L[i];
        if (L->label && L->label[0] != '.') { free(cur); cur = xstrdup(L->label); }
        free(L->scope);
        L->scope = xstrdup(cur);
    }
    free(cur);
}

/* True if a branch operand is a plain label name. */
static int is_plain_target(const char *t)
{
    if (!(isalpha((unsigned char)*t) || *t == '_' || *t == '.')) return 0;
    for (const char *p = t; *p; p++) if (!idch((unsigned char)*p)) return 0;
    return 1;
}

/* Unique key for a label, including its scope for local names. */
static char *key_of(const char *name, const char *scope)
{
    char buf[512];
    if (name[0] == '.' && name[1] != '.') snprintf(buf, sizeof buf, "%s%s", scope, name);
    else snprintf(buf, sizeof buf, "%s", name);
    return xstrdup(buf);
}

/* True for unconditional branches and returns. */
static int is_uncond(NC *c, const NL *L)
{
    if (L->kind != N_INSN) return 0;
    const char *m = L->mn;
    if (c->a64) return (!strcmp(m, "b") && L->nops == 1) || (!strcmp(m, "br") && L->nops == 1) || (!strcmp(m, "ret") && L->nops <= 1) || !strcmp(m, "eret");
    return (!strcmp(m, "j") && L->nops == 1) || (!strcmp(m, "jr") && L->nops == 1) || (!strcmp(m, "ret") && L->nops == 0) || (!strcmp(m, "tail") && L->nops == 1) || !strcmp(m, "mret") || !strcmp(m, "sret");
}

/* True for a plain jump to a label. */
static int is_jump_label(NC *c, const NL *L)
{
    if (L->kind != N_INSN || L->nops != 1) return 0;
    return c->a64 ? !strcmp(L->mn, "b") : !strcmp(L->mn, "j");
}

/* True for conditional branches; gives the target operand index. */
static int is_cond(NC *c, const NL *L, int *tgt)
{
    if (L->kind != N_INSN) return 0;
    const char *m = L->mn;
    if (c->a64) {
        if (!strncmp(m, "b.", 2) && L->nops == 1) { *tgt = 0; return 1; }
        if ((!strcmp(m, "cbz") || !strcmp(m, "cbnz")) && L->nops == 2) { *tgt = 1; return 1; }
        if ((!strcmp(m, "tbz") || !strcmp(m, "tbnz")) && L->nops == 3) { *tgt = 2; return 1; }
        return 0;
    }
    static const char *const f[] = { "beq", "bne", "blt", "bge", "bltu", "bgeu", "bgt", "ble", "bgtu", "bleu", NULL };
    static const char *const g[] = { "beqz", "bnez", "blez", "bgez", "bltz", "bgtz", NULL };
    for (int i = 0; f[i]; i++) if (!strcmp(m, f[i]) && L->nops == 3) { *tgt = 2; return 1; }
    for (int i = 0; g[i]; i++) if (!strcmp(m, g[i]) && L->nops == 2) { *tgt = 1; return 1; }
    return 0;
}

/* Opposite branch mnemonic (b.eq to b.ne, beq to bne ...). */
static const char *inv_name(NC *c, const char *m, char *buf)
{
    static const char *const pa[][2] = { { "b.eq", "b.ne" }, { "b.cs", "b.cc" }, { "b.hs", "b.lo" }, { "b.mi", "b.pl" }, { "b.vs", "b.vc" }, { "b.hi", "b.ls" }, { "b.ge", "b.lt" }, { "b.gt", "b.le" }, { "cbz", "cbnz" }, { "tbz", "tbnz" }, { NULL, NULL } };
    static const char *const pr[][2] = { { "beq", "bne" }, { "blt", "bge" }, { "bltu", "bgeu" }, { "beqz", "bnez" }, { "blez", "bgtz" }, { "bgez", "bltz" }, { "bgt", "ble" }, { "bgtu", "bleu" }, { NULL, NULL } };
    const char *const (*t)[2] = c->a64 ? pa : pr;
    for (int i = 0; t[i][0]; i++) {
        if (!strcmp(m, t[i][0])) { strcpy(buf, t[i][1]); return buf; }
        if (!strcmp(m, t[i][1])) { strcpy(buf, t[i][0]); return buf; }
    }
    return NULL;
}

/* Next instruction line, or -1 if something blocks the scan. */
static int next_insn_idx(NC *c, int i)
{
    for (int k = i + 1; k < c->n; k++) {
        NL *L = &c->L[k];
        if (L->kind == N_OTHER) return -1;
        if (L->kind == N_INSN) return k;
    }
    return -1;
}

/* Next instruction if only blank lines (no labels) are between. */
static int adjacent_insn(NC *c, int i)
{
    for (int k = i + 1; k < c->n; k++) {
        NL *L = &c->L[k];
        if (L->kind == N_BLANK && !L->label) continue;
        if (L->kind == N_INSN && !L->label) return k;
        return -1;
    }
    return -1;
}

/* Line where a label key is defined, or -1. */
static int label_line(NC *c, const char *key)
{
    for (int i = 0; i < c->n; i++) {
        NL *L = &c->L[i];
        if (!L->label) continue;
        char *k = key_of(L->label, L->scope);
        int eq = !strcmp(k, key);
        free(k);
        if (eq) return i;
    }
    return -1;
}

/* True if a label key is defined before the next instruction (so jumping to it lands where it is expected). */
static int chain_has(NC *c, int from, const char *key)
{
    for (int k = from; k < c->n; k++) {
        NL *L = &c->L[k];
        if (L->kind == N_OTHER) return 0;
        if (L->label) {
            char *kk = key_of(L->label, L->scope);
            int eq = !strcmp(kk, key);
            free(kk);
            if (eq) return 1;
        }
        if (L->kind == N_INSN) return 0;
    }
    return 0;
}

/* Label key of a branch target. */
static int tgt_key(NC *c, int i, int ti, char **key)
{
    NL *L = &c->L[i];
    if (!is_plain_target(L->ops[ti])) return 0;
    *key = key_of(L->ops[ti], L->scope);
    (void)c;
    return 1;
}

/* Removes mov x, x and add x, x, 0 style no-ops. */
static int pass_nops(NC *c)
{
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        NL *L = &c->L[i];
        if (L->kind != N_INSN) continue;
        int rm = 0;
        if (c->a64) {
            if (!strcmp(L->mn, "mov") && L->nops == 2 && (L->ops[0][0] == 'x' || L->ops[0][0] == 'X' || !strcasecmp(L->ops[0], "sp")) && opeq(L->ops[0], L->ops[1])) rm = 1;
            if ((!strcmp(L->mn, "add") || !strcmp(L->mn, "sub")) && L->nops == 3 && (L->ops[0][0] == 'x' || L->ops[0][0] == 'X' || !strcasecmp(L->ops[0], "sp")) && opeq(L->ops[0], L->ops[1]) && opeq(L->ops[2], "0")) rm = 1;
        } else {
            if (!strcmp(L->mn, "mv") && L->nops == 2 && opeq(L->ops[0], L->ops[1]) && strcasecmp(L->ops[0], "zero")) rm = 1;
            if (!strcmp(L->mn, "addi") && L->nops == 3 && opeq(L->ops[0], L->ops[1]) && opeq(L->ops[2], "0") && strcasecmp(L->ops[0], "zero") && strcasecmp(L->ops[0], "x0")) rm = 1;
        }
        if (rm) { kill_insn(c, i); ch = 1; }
    }
    return ch;
}

/* Removes unreachable instructions after an unconditional branch until the next label. */
static int pass_dead(NC *c)
{
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        if (!is_uncond(c, &c->L[i])) continue;
        for (int k = i + 1; k < c->n; k++) {
            NL *L = &c->L[k];
            if (L->kind == N_BLANK && !L->label) continue;
            if (L->kind != N_INSN || L->label) break;
            kill_insn(c, k);
            ch = 1;
        }
    }
    return ch;
}

/* Removes a jump to the very next line. */
static int pass_jump_next(NC *c)
{
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        NL *L = &c->L[i];
        int ti;
        int uj = is_jump_label(c, L);
        if (!uj && !is_cond(c, L, &ti)) continue;
        if (uj) ti = 0;
        char *key;
        if (!tgt_key(c, i, ti, &key)) continue;
        int hit = chain_has(c, i + 1, key);
        free(key);
        if (hit) { kill_insn(c, i); ch = 1; }
    }
    return ch;
}

/* Jump threading: a branch to a label that holds only another jump goes straight to the final target. */
static int pass_thread(NC *c)
{
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        NL *L = &c->L[i];
        int ti;
        int uj = is_jump_label(c, L);
        if (!uj && !is_cond(c, L, &ti)) continue;
        if (uj) ti = 0;
        char *key;
        if (!tgt_key(c, i, ti, &key)) continue;
        int ll = label_line(c, key);
        free(key);
        if (ll < 0) continue;
        int k = c->L[ll].kind == N_INSN ? ll : next_insn_idx(c, ll);
        if (k < 0 || !is_jump_label(c, &c->L[k])) continue;
        NL *J = &c->L[k];
        if (!is_plain_target(J->ops[0])) continue;
        if (!strcmp(J->ops[0], L->ops[ti]) && !strcmp(J->scope, L->scope)) continue;
        if (J->ops[0][0] == '.' && J->ops[0][1] != '.' && strcmp(J->scope, L->scope)) continue;
        char *k2 = key_of(J->ops[0], J->scope);
        char *k1 = key_of(L->ops[ti], L->scope);
        int same = !strcmp(k1, k2);
        free(k1);
        free(k2);
        if (same) continue;
        free(L->ops[ti]);
        L->ops[ti] = xstrdup(J->ops[0]);
        rebuild(c, i);
        L->changed = 1;
        ch = 1;
    }
    return ch;
}

/* A conditional branch over one unconditional branch becomes one inverted conditional branch. */
static int pass_invert(NC *c)
{
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        NL *L = &c->L[i];
        int ti;
        if (!is_cond(c, L, &ti)) continue;
        int j = adjacent_insn(c, i);
        if (j < 0 || !is_jump_label(c, &c->L[j]) || !is_plain_target(c->L[j].ops[0])) continue;
        char *key;
        if (!tgt_key(c, i, ti, &key)) continue;
        int hit = chain_has(c, j + 1, key);
        free(key);
        if (!hit) continue;
        char nb[24];
        if (!inv_name(c, L->mn, nb)) continue;
        snprintf(L->mn, sizeof L->mn, "%s", nb);
        free(L->ops[ti]);
        L->ops[ti] = xstrdup(c->L[j].ops[0]);
        rebuild(c, i);
        kill_insn(c, j);
        ch = 1;
    }
    return ch;
}

/* True for sp, x29 and similar base registers. */
static int is_stackbase(const char *base)
{
    return !strcasecmp(base, "sp") || !strcasecmp(base, "x29") || !strcasecmp(base, "fp") || !strcasecmp(base, "s0");
}

/* Gets the base register of a memory operand. */
static int mem_base(NC *c, const char *op, char *out, size_t sz)
{
    const char *p = op;
    if (c->a64) {
        if (*p != '[') return 0;
        p++;
        size_t n = 0;
        while (*p && *p != ',' && *p != ']' && n + 1 < sz) out[n++] = *p++;
        out[n] = 0;
        size_t l = strlen(op);
        if (op[l - 1] != ']') return 0;
        while (n && out[n - 1] == ' ') out[--n] = 0;
        return n > 0 && is_stackbase(out);
    }
    const char *o = strchr(op, '(');
    if (!o) return 0;
    size_t n = 0;
    o++;
    while (*o && *o != ')' && n + 1 < sz) out[n++] = *o++;
    out[n] = 0;
    return n > 0 && (!strcasecmp(out, "sp") || !strcasecmp(out, "s0") || !strcasecmp(out, "fp"));
}

/* A store followed immediately by a load from the same address: the load is dropped or becomes a register copy. */
static int pass_fwd(NC *c)
{
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        NL *S = &c->L[i];
        if (S->kind != N_INSN || S->nops != 2) continue;
        int ok = c->a64 ? (!strcmp(S->mn, "str") && (S->ops[0][0] == 'x' || S->ops[0][0] == 'X') && strcasecmp(S->ops[0], "xzr"))
                        : ((c->arch == ARCH_RV64 && !strcmp(S->mn, "sd")) || (c->arch == ARCH_RV32 && !strcmp(S->mn, "sw")));
        if (!ok) continue;
        char base[32];
        if (!mem_base(c, S->ops[1], base, sizeof base)) continue;
        if (strchr(S->ops[1], '!')) continue;
        int j = adjacent_insn(c, i);
        if (j < 0) continue;
        NL *T = &c->L[j];
        const char *lm = c->a64 ? "ldr" : (c->arch == ARCH_RV64 ? "ld" : "lw");
        if (T->kind != N_INSN || strcmp(T->mn, lm) || T->nops != 2 || !opeq(T->ops[1], S->ops[1])) continue;
        if (c->a64 && !(T->ops[0][0] == 'x' || T->ops[0][0] == 'X')) continue;
        if (c->a64 && !strcasecmp(T->ops[0], "xzr")) continue;
        if (!c->a64 && !strcasecmp(T->ops[0], "zero")) { kill_insn(c, j); ch = 1; continue; }
        if (opeq(T->ops[0], S->ops[0])) kill_insn(c, j);
        else set_insn(c, j, c->a64 ? "mov" : "mv", 2, T->ops[0], S->ops[0], NULL);
        ch = 1;
    }
    return ch;
}

/* Register number, or 0 if the text is not a register. */
static int reg_id(NC *c, const char *r)
{
    if (c->a64) {
        if ((r[0] == 'x' || r[0] == 'w' || r[0] == 'X' || r[0] == 'W') && isdigit((unsigned char)r[1])) return atoi(r + 1);
        return -1;
    }
    static const char *const ab[] = { "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6" };
    for (int i = 0; i < 32; i++) if (!strcasecmp(r, ab[i])) return i;
    if (!strcasecmp(r, "fp")) return 8;
    if ((r[0] == 'x' || r[0] == 'X') && isdigit((unsigned char)r[1])) return atoi(r + 1);
    return -1;
}

/* True if the operands use the register. */
static int mentions_reg(NC *c, const NL *L, int from, int id)
{
    for (int k = from; k < L->nops; k++) {
        const char *s = L->ops[k];
        while (*s) {
            if (idch((unsigned char)*s)) {
                const char *b = s;
                while (idch((unsigned char)*s)) s++;
                char t[24];
                size_t n = (size_t)(s - b);
                if (n < sizeof t) { memcpy(t, b, n); t[n] = 0; if (reg_id(c, t) == id) return 1; }
            } else s++;
        }
    }
    return 0;
}

/* Removes a register write that is immediately overwritten by another write to the same register. */
static int pass_waw(NC *c)
{
    static const char *const firsta[] = { "mov", "movz", "movn", NULL };
    static const char *const firstr[] = { "mv", "li", NULL };
    static const char *const seca[] = { "mov", "movz", "movn", "ldr", "ldrb", "ldrh", "ldrsw", "adr", "adrp", NULL };
    static const char *const secr[] = { "mv", "li", "ld", "lw", "lwu", "lh", "lhu", "lb", "lbu", "lui", "auipc", "la", "lla", NULL };
    const char *const *f1 = c->a64 ? firsta : firstr;
    const char *const *f2 = c->a64 ? seca : secr;
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        NL *A = &c->L[i];
        if (A->kind != N_INSN || A->nops < 2) continue;
        int ok = 0;
        for (int k = 0; f1[k]; k++) if (!strcmp(A->mn, f1[k])) ok = 1;
        if (!ok) continue;
        int id = reg_id(c, A->ops[0]);
        if (id <= 0 || (c->a64 && (id > 28))) continue;
        if (c->a64 && !strcasecmp(A->ops[0], "sp")) continue;
        if (mentions_reg(c, A, 1, id) || strchr(A->ops[1], '[')) continue;
        int j = adjacent_insn(c, i);
        if (j < 0) continue;
        NL *B = &c->L[j];
        ok = 0;
        for (int k = 0; f2[k]; k++) if (!strcmp(B->mn, f2[k])) ok = 1;
        if (!ok || B->nops < 2 || reg_id(c, B->ops[0]) != id || mentions_reg(c, B, 1, id)) continue;
        if (c->a64 && strchr(B->ops[1], '!')) continue;
        if (c->a64 && B->nops > 2) continue;
        kill_insn(c, i);
        ch = 1;
    }
    return ch;
}

/* True if an instruction leaves the flags alone. */
static int flag_clean(NC *c, int i)
{
    NL *L = &c->L[i];
    if (L->kind != N_INSN) return 0;
    static const char *const ok[] = { "cmp", "cmn", "tst", "subs", "adds", "ands", "bics", "negs", "fcmp", "fcmpe", "ret", "bl", "blr", NULL };
    for (int k = 0; ok[k]; k++) if (!strcmp(L->mn, ok[k])) return 1;
    return 0;
}

/* cmp x, #0 followed by beq or bne becomes cbz or cbnz (ARM64). */
static int pass_cbz(NC *c)
{
    if (!c->a64) return 0;
    int ch = 0;
    for (int i = 0; i < c->n; i++) {
        NL *P = &c->L[i];
        if (P->kind != N_INSN || P->nops != 2 || (strcmp(P->mn, "cmp") && strcmp(P->mn, "tst"))) continue;
        char r0 = P->ops[0][0];
        if (r0 != 'x' && r0 != 'w' && r0 != 'X' && r0 != 'W') continue;
        if (!strcasecmp(P->ops[0], "xzr") || !strcasecmp(P->ops[0], "wzr")) continue;
        if (!strcmp(P->mn, "cmp")) { if (!(opeq(P->ops[1], "0") || opeq(P->ops[1], "xzr") || opeq(P->ops[1], "wzr"))) continue; }
        else if (!opeq(P->ops[0], P->ops[1])) continue;
        int j = adjacent_insn(c, i);
        if (j < 0) continue;
        NL *B = &c->L[j];
        int z = !strcmp(B->mn, "b.eq");
        if (!z && strcmp(B->mn, "b.ne")) continue;
        if (B->nops != 1) continue;
        int f = next_insn_idx(c, j);
        if (f < 0 || !flag_clean(c, f)) continue;
        char *key;
        if (!tgt_key(c, j, 0, &key)) continue;
        int tl = label_line(c, key);
        free(key);
        if (tl < 0) continue;
        int t = c->L[tl].kind == N_INSN ? tl : next_insn_idx(c, tl);
        if (t < 0 || !flag_clean(c, t)) continue;
        char tg[256];
        snprintf(tg, sizeof tg, "%s", B->ops[0]);
        char reg[32];
        snprintf(reg, sizeof reg, "%s", P->ops[0]);
        set_insn(c, j, z ? "cbz" : "cbnz", 2, reg, tg, NULL);
        kill_insn(c, i);
        ch = 1;
    }
    return ch;
}

/* Runs the passes until nothing changes. */
void opt_run_nx(LineVec *lv, int arch, int kernel, int type)
{
    (void)kernel;
    if (type == T_FLAT || type == T_COM || type == T_HEX || type == T_SREC || type == T_IMG) return;
    NC c;
    c.lv = lv;
    c.n = lv->n;
    c.arch = arch;
    c.a64 = arch == ARCH_A64;
    c.L = xcalloc((size_t)c.n + 1, sizeof(NL));
    for (int i = 0; i < c.n; i++) {
        const char *t = sk(lv->v[i].text);
        if (!strncasecmp(t, "org", 3) && !idch((unsigned char)t[3])) { free(c.L); return; }
        if (!strncasecmp(t, "incbin", 6) || !strncasecmp(t, "align", 5) || !strncasecmp(t, "times", 5)) { }
    }
    for (int i = 0; i < c.n; i++) parse_nl(&c, lv->v[i].text, &c.L[i]);
    for (int round = 0; round < 8; round++) {
        compute_scopes(&c);
        int ch = 0;
        ch |= pass_nops(&c);
        ch |= pass_dead(&c);
        ch |= pass_thread(&c);
        ch |= pass_invert(&c);
        ch |= pass_jump_next(&c);
        ch |= pass_fwd(&c);
        ch |= pass_waw(&c);
        ch |= pass_cbz(&c);
        if (!ch) break;
    }
    for (int i = 0; i < c.n; i++) free_nl(&c.L[i]);
    free(c.L);
}
