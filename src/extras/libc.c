#include "cly.h"

extern const char libc_src64[], libc_src32[];

typedef struct {
    char *names[12];
    int nn;
    char *deps[8];
    int nd;
    char *body;
    int internal;
    int direct;
    int used;
} Blk;

typedef struct {
    Blk *b;
    int n;
} Lib;

static const char *skipws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int words(const char *p, char **out, int max)
{
    int n = 0;
    for (;;) {
        p = skipws(p);
        if (!*p || *p == '\n') break;
        const char *s = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (n < max) out[n++] = xstrndup(s, (size_t)(p - s));
    }
    return n;
}

static void lib_load(Lib *L, const char *src)
{
    int cap = 0;
    L->b = NULL;
    L->n = 0;
    const char *p = src;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= 3 && !strncmp(p, ";;@", 3)) {
            if (L->n == cap) { cap = cap ? cap * 2 : 64; L->b = xrealloc(L->b, sizeof(Blk) * (size_t)cap); }
            Blk *b = &L->b[L->n++];
            memset(b, 0, sizeof *b);
            char *tmp = xstrndup(p + 3, len - 3);
            b->nn = words(tmp, b->names, 12);
            free(tmp);
            if (b->nn && b->names[0][0] == '-') { b->internal = 1; memmove(b->names[0], b->names[0] + 1, strlen(b->names[0])); }
            else if (b->nn && b->names[0][0] == '=') { b->direct = 1; memmove(b->names[0], b->names[0] + 1, strlen(b->names[0])); }
            Str body = {0};
            p = nl ? nl + 1 : p + len;
            while (*p && strncmp(p, ";;@", 3)) {
                const char *e = strchr(p, '\n');
                size_t l2 = e ? (size_t)(e - p) : strlen(p);
                if (l2 >= 3 && !strncmp(p, ";;>", 3)) {
                    char *t2 = xstrndup(p + 3, l2 - 3);
                    b->nd = words(t2, b->deps, 8);
                    free(t2);
                } else {
                    saddn(&body, p, l2);
                    sadc(&body, '\n');
                }
                p = e ? e + 1 : p + l2;
            }
            b->body = sget(&body);
            continue;
        }
        p = nl ? nl + 1 : p + len;
    }
}

static int is_id(int c) { return ident_char((unsigned char)c); }

typedef struct { char **v; int n, cap; } Set;

static int set_has(const Set *s, const char *k, size_t l)
{
    for (int i = 0; i < s->n; i++) if (strlen(s->v[i]) == l && !strncmp(s->v[i], k, l)) return 1;
    return 0;
}

static void set_add(Set *s, const char *k, size_t l)
{
    if (set_has(s, k, l)) return;
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 32; s->v = xrealloc(s->v, sizeof(char *) * (size_t)s->cap); }
    s->v[s->n++] = xstrndup(k, l);
}

static const char *line_body(const char *t)
{
    return skipws(t);
}

static int data_word(const char *p, size_t l)
{
    static const char *const k[] = { "equ", "db", "dw", "dd", "dq", "dt", "do", "dy", "dz", "resb", "resw", "resd", "resq", "rest", "reso", "resy", "resz", "times", "incbin", "istruc", "struc", NULL };
    for (int i = 0; k[i]; i++) if (strlen(k[i]) == l && !strncasecmp(k[i], p, l)) return 1;
    return 0;
}

static void collect_defs(const LineVec *lv, Set *defs)
{
    for (int i = 0; i < lv->n; i++) {
        const char *t = line_body(lv->v[i].text);
        if (!ident_start((unsigned char)*t) && *t != '.' && *t != '$') continue;
        const char *s = t;
        while (is_id(*t) || *t == '.' || *t == '$' || *t == '@' || *t == '?') t++;
        size_t l = (size_t)(t - s);
        const char *q = skipws(t);
        if (*q == ':') { set_add(defs, s, l); continue; }
        const char *w = q;
        while (is_id(*w)) w++;
        if (w > q && data_word(q, (size_t)(w - q))) set_add(defs, s, l);
    }
}

static int is_cmd(const char *t, const char *w)
{
    size_t l = strlen(w);
    return !strncasecmp(t, w, l) && !is_id(t[l]);
}

static void rewrite_got(LineVec *lv, Set *gots, int *changed)
{
    for (int i = 0; i < lv->n; i++) {
        char *t = lv->v[i].text;
        char *w = t;
        int edited = 0;
        for (;;) {
            char *q = NULL;
            for (char *s = w; *s; s++) {
                if ((s == t || !is_id(s[-1])) && !strncasecmp(s, "wrt", 3) && !is_id(s[3])) { q = s; break; }
                if (*s == ';') break;
            }
            if (!q) break;
            char *a = (char *)skipws(q + 3);
            if (!(a[0] == '.' && a[1] == '.')) { w = q + 3; continue; }
            char *e = a + 2;
            while (is_id(*e)) e++;
            int isplt = (e - a - 2 == 3 && !strncasecmp(a + 2, "plt", 3));
            int isgot = (e - a - 2 >= 3 && !strncasecmp(a + 2, "got", 3));
            if (!isplt && !isgot) { w = e; continue; }
            char *sp = q;
            while (sp > t && (sp[-1] == ' ' || sp[-1] == '\t')) sp--;
            char *ne = sp;
            char *ns = ne;
            while (ns > t && (is_id(ns[-1]) || ns[-1] == '$')) ns--;
            Str out = {0};
            if (isgot && ns < ne) {
                saddn(&out, t, (size_t)(ns - t));
                sadd(&out, "__cly_got_");
                saddn(&out, ns, (size_t)(ne - ns));
                set_add(gots, ns, (size_t)(ne - ns));
            } else {
                saddn(&out, t, (size_t)(sp - t));
            }
            size_t pos = out.n;
            sadd(&out, e);
            char *nt = sget(&out);
            free(lv->v[i].text);
            lv->v[i].text = nt;
            t = nt;
            w = t + pos;
            edited = 1;
        }
        for (char *s = t; *s && *s != ';'; s++) {
            if (*s == '@' && !strncasecmp(s + 1, "plt", 3) && !is_id(s[4]) && s > t && is_id(s[-1])) {
                memmove(s, s + 4, strlen(s + 4) + 1);
                edited = 1;
                s--;
            }
        }
        if (edited) *changed = 1;
    }
}

static int find_block(Lib *L, const char *name, size_t l)
{
    for (int i = 0; i < L->n; i++)
        for (int k = 0; k < L->b[i].nn; k++)
            if (strlen(L->b[i].names[k]) == l && !strncmp(L->b[i].names[k], name, l)) return i;
    return -1;
}

static int is_public_name(Lib *L, const char *name, size_t l, int *bi)
{
    int i = find_block(L, name, l);
    if (i < 0 || L->b[i].internal) return 0;
    if (strlen(L->b[i].names[0]) == l && !strncmp(L->b[i].names[0], name, l) && L->b[i].direct) { *bi = i; return 1; }
    *bi = i;
    return 1;
}

static void need(Lib *L, int bi)
{
    if (L->b[bi].used) return;
    L->b[bi].used = 1;
    for (int k = 0; k < L->b[bi].nd; k++) {
        const char *d = L->b[bi].deps[k];
        if (*d == '-') d++;
        int j = find_block(L, d, strlen(d));
        if (j >= 0) need(L, j);
    }
}

static void strip_extern(LineVec *lv, Lib *L, const Set *defs)
{
    for (int i = 0; i < lv->n; i++) {
        const char *t = line_body(lv->v[i].text);
        int ext = is_cmd(t, "extern") || is_cmd(t, "extrn");
        if (!ext) continue;
        const char *p = t + (is_cmd(t, "extern") ? 6 : 5);
        Str out = {0};
        sadd(&out, is_cmd(t, "extern") ? "extern " : "extrn ");
        int keep = 0;
        for (;;) {
            p = skipws(p);
            if (*p == ',') { p++; continue; }
            if (!*p || *p == ';') break;
            const char *s = p;
            while (*p && *p != ',' && *p != ' ' && *p != '\t' && *p != ';') p++;
            size_t l = (size_t)(p - s);
            int bi;
            if (is_public_name(L, s, l, &bi) && !set_has(defs, s, l)) continue;
            if (keep) sadd(&out, ", ");
            saddn(&out, s, l);
            keep++;
        }
        free(lv->v[i].text);
        if (keep) lv->v[i].text = sget(&out);
        else { lv->v[i].text = xstrdup(""); free(out.s); }
    }
}

static void collect_refs(const LineVec *lv, Lib *L, const Set *defs, Set *refs)
{
    for (int i = 0; i < lv->n; i++) {
        const char *t = line_body(lv->v[i].text);
        if (is_cmd(t, "extern") || is_cmd(t, "extrn") || is_cmd(t, "global") || is_cmd(t, "section") || is_cmd(t, "segment") || is_cmd(t, "bits") || is_cmd(t, "default")) continue;
        const char *p = t;
        int prevcall = 0;
        const char *prev = NULL;
        while (*p && *p != ';') {
            if (*p == '"' || *p == '\'' || *p == '`') {
                char qc = *p++;
                while (*p && *p != qc) { if (*p == '\\' && qc == '`' && p[1]) p++; p++; }
                if (*p) p++;
                continue;
            }
            if (is_id(*p) && !isdigit((unsigned char)*p)) {
                const char *s = p;
                while (is_id(*p) || *p == '$' || *p == '@' || *p == '?') p++;
                size_t l = (size_t)(p - s);
                if (s > t && s[-1] == '.') { prev = s; continue; }
                int bi;
                if (is_public_name(L, s, l, &bi) && !set_has(defs, s, l)) {
                    int ok = 1;
                    if (l == 3 && !strncmp(s, "abs", 3)) ok = prev && ((!strncasecmp(prev, "call", 4)) || (!strncasecmp(prev, "jmp", 3)));
                    if (ok && !prevcall) set_add(refs, s, l);
                    else if (ok) set_add(refs, s, l);
                }
                prev = s;
                continue;
            }
            p++;
        }
    }
}

static int has_def(const Set *defs, const char *n) { return set_has(defs, n, strlen(n)); }

static int target_bits(const LineVec *lv, int defbits)
{
    for (int i = 0; i < lv->n; i++) {
        const char *t = line_body(lv->v[i].text);
        if (*t == '[') t = skipws(t + 1);
        if (is_cmd(t, "bits")) { int b = atoi(skipws(t + 4)); if (b == 16 || b == 32 || b == 64) return b; }
        if (is_cmd(t, "use64")) return 64;
        if (is_cmd(t, "use32")) return 32;
    }
    return defbits;
}


typedef struct { char *name; int len; } StrLen;

static int parse_num_item(const char *p, long *v, const char **end)
{
    const char *s = p;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (!isdigit((unsigned char)*s)) return 0;
    char *e;
    long x = strtol(s, &e, 0);
    if (e == s) return 0;
    if ((*e == 'h' || *e == 'H') && !is_id(e[1])) {
        x = strtol(s, &e, 16);
        e++;
    }
    if (is_id(*e)) return 0;
    *v = neg ? -x : x;
    *end = e;
    return 1;
}

static int db_len(const char *p, int *len, int *done)
{
    for (;;) {
        p = skipws(p);
        if (!*p || *p == ';') return 1;
        if (*p == '"' || *p == '\'' || *p == '`') {
            char qc = *p++;
            while (*p && *p != qc) {
                int c = (unsigned char)*p;
                if (qc == '`' && c == '\\') {
                    p++;
                    c = (unsigned char)*p;
                    if (c == 'n' || c == 't' || c == 'r' || c == '\\' || c == 'e' || c == 'a' || c == 'b' || c == 'f' || c == 'v' || c == '"' || c == '\'' || c == '`') { }
                    else if (c == '0' && !(p[1] >= '0' && p[1] <= '7')) { *done = 1; return 1; }
                    else return 0;
                } else if (c == '%') return 0;
                if (c == 0) { *done = 1; return 1; }
                (*len)++;
                p++;
            }
            if (!*p) return 0;
            p++;
        } else {
            long v;
            const char *e;
            if (!parse_num_item(p, &v, &e)) return 0;
            if ((v & 255) == 0) { *done = 1; return 1; }
            (*len)++;
            p = e;
        }
        p = skipws(p);
        if (*p == ',') { p++; continue; }
        if (!*p || *p == ';') return 1;
        return 0;
    }
}

static void collect_strs(const LineVec *lv, StrLen **out, int *n)
{
    int cap = 0;
    *n = 0;
    *out = NULL;
    for (int i = 0; i < lv->n; i++) {
        const char *t = line_body(lv->v[i].text);
        if (!ident_start((unsigned char)*t) && *t != '.' && *t != '$') continue;
        const char *s = t;
        while (is_id(*t) || *t == '.' || *t == '$' || *t == '@' || *t == '?') t++;
        size_t l = (size_t)(t - s);
        const char *q = skipws(t);
        if (*q == ':') q = skipws(q + 1);
        const char *dbp = NULL;
        if (!*q || *q == ';') {
            const char *nx = i + 1 < lv->n ? line_body(lv->v[i + 1].text) : "";
            if (is_cmd(nx, "db")) { dbp = nx + 2; i++; }
        } else if (is_cmd(q, "db")) dbp = q + 2;
        if (!dbp) continue;
        int len = 0, done = 0;
        int ok = db_len(dbp, &len, &done);
        int j = i;
        while (ok && !done && j + 1 < lv->n) {
            const char *nx = line_body(lv->v[j + 1].text);
            if (!is_cmd(nx, "db")) break;
            ok = db_len(nx + 2, &len, &done);
            j++;
        }
        if (!ok || !done) continue;
        if (*n == cap) { cap = cap ? cap * 2 : 16; *out = xrealloc(*out, sizeof(StrLen) * (size_t)cap); }
        (*out)[*n].name = xstrndup(s, l);
        (*out)[*n].len = len;
        (*n)++;
    }
}

static int find_len(StrLen *sl, int n, const char *name, size_t l)
{
    for (int i = 0; i < n; i++) if (strlen(sl[i].name) == l && !strncmp(sl[i].name, name, l)) return sl[i].len;
    return -1;
}

static int match_sym_operand(const char *p, char *name, size_t sz)
{
    p = skipws(p);
    if (!strncasecmp(p, "offset", 6) && !is_id(p[6])) p = skipws(p + 6);
    if (*p == '[') {
        p = skipws(p + 1);
        if (!strncasecmp(p, "rel", 3) && !is_id(p[3])) p = skipws(p + 3);
        if (!strncasecmp(p, "abs", 3) && !is_id(p[3])) p = skipws(p + 3);
        const char *s = p;
        while (is_id(*p) || *p == '.' || *p == '$') p++;
        if (p == s) return 0;
        size_t l = (size_t)(p - s);
        p = skipws(p);
        if (*p != ']') return 0;
        if (l >= sz) return 0;
        memcpy(name, s, l);
        name[l] = 0;
        return 1;
    }
    const char *s = p;
    if (!ident_start((unsigned char)*p)) return 0;
    while (is_id(*p) || *p == '.' || *p == '$') p++;
    size_t l = (size_t)(p - s);
    p = skipws(p);
    if (*p && *p != ';') return 0;
    if (l >= sz) return 0;
    memcpy(name, s, l);
    name[l] = 0;
    return 1;
}

static int prev_fmt_load(const LineVec *lv, int i, int bits, char *name, size_t sz)
{
    int seen = 0;
    for (int k = i - 1; k >= 0 && seen < 4; k--) {
        const char *t = line_body(lv->v[k].text);
        if (!*t || *t == ';') continue;
        seen++;
        if (bits == 64) {
            if (is_cmd(t, "lea") || is_cmd(t, "mov")) {
                const char *o = skipws(t + 3);
                if ((!strncasecmp(o, "rdi", 3) || !strncasecmp(o, "edi", 3)) && !is_id(o[3])) {
                    o = skipws(o + 3);
                    if (*o != ',') return 0;
                    return match_sym_operand(o + 1, name, sz);
                }
                if (is_cmd(t, "mov")) {
                    if ((!strncasecmp(o, "eax", 3) || !strncasecmp(o, "rax", 3)) && !is_id(o[3])) {
                        const char *r = skipws(skipws(o + 3) + 1);
                        if (*r == '0' && !is_id(r[1])) continue;
                    }
                }
                return 0;
            }
            if (is_cmd(t, "xor")) {
                const char *o = skipws(t + 3);
                if ((!strncasecmp(o, "eax", 3) && !is_id(o[3])) || (!strncasecmp(o, "al", 2) && !is_id(o[2])) || (!strncasecmp(o, "rax", 3) && !is_id(o[3]))) continue;
                return 0;
            }
            return 0;
        } else {
            if (is_cmd(t, "push")) return match_sym_operand(t + 4, name, sz);
            return 0;
        }
    }
    return 0;
}

static int call_target(const char *t, const char *fn)
{
    if (!is_cmd(t, "call")) return 0;
    const char *p = skipws(t + 4);
    size_t l = strlen(fn);
    if (strncmp(p, fn, l) || is_id(p[l])) return 0;
    p = skipws(p + l);
    return !*p || *p == ';';
}

static int fold_rdi(LineVec *out)
{
    if (out->n < 1) return 0;
    char *t = out->v[out->n - 1].text;
    const char *b = line_body(t);
    if (is_cmd(b, "xor") && out->n >= 2) {
        const char *o = skipws(b + 3);
        if (!strncasecmp(o, "eax", 3) && !is_id(o[3])) {
            free(out->v[out->n - 1].text);
            out->n--;
            t = out->v[out->n - 1].text;
            b = line_body(t);
        } else return 0;
    }
    if (!(is_cmd(b, "lea") || is_cmd(b, "mov"))) return 0;
    const char *o = skipws(b + 3);
    if (strncmp(o, "rdi", 3) && strncmp(o, "edi", 3)) return 0;
    if (is_id(o[3])) return 0;
    char *nt = xmalloc(strlen(t) + 1);
    strcpy(nt, t);
    char *q = nt + (o - t);
    q[0] = 'r' == o[0] ? 'r' : 'e';
    q[1] = 's';
    q[2] = 'i';
    free(out->v[out->n - 1].text);
    out->v[out->n - 1].text = nt;
    return 1;
}

static void inline_calls(LineVec *lv, const Set *defs, int bits)
{
    StrLen *sl;
    int nsl;
    collect_strs(lv, &sl, &nsl);
    LineVec out;
    memset(&out, 0, sizeof out);
    for (int i = 0; i < lv->n; i++) {
        const char *t = line_body(lv->v[i].text);
        const char *file = lv->v[i].file;
        int ln = lv->v[i].line;
        char name[128];
        int done = 0;
        if (!set_has(defs, "printf", 6) && call_target(t, "printf") && prev_fmt_load(lv, i, bits, name, sizeof name)) {
            int len = find_len(sl, nsl, name, strlen(name));
            if (len > 0) {
                char b[160];
                if (bits == 64) {
                    if (!fold_rdi(&out)) lv_add(&out, "    mov rsi, rdi", file, ln);
                    snprintf(b, sizeof b, "    mov edx, %d", len);
                    lv_add(&out, b, file, ln);
                    lv_add(&out, "    mov edi, 1", file, ln);
                    lv_add(&out, "    mov eax, 1", file, ln);
                    lv_add(&out, "    syscall", file, ln);
                } else {
                    lv_add(&out, "    push ebx", file, ln);
                    lv_add(&out, "    mov ecx, [esp+4]", file, ln);
                    snprintf(b, sizeof b, "    mov edx, %d", len);
                    lv_add(&out, b, file, ln);
                    lv_add(&out, "    mov ebx, 1", file, ln);
                    lv_add(&out, "    mov eax, 4", file, ln);
                    lv_add(&out, "    int 0x80", file, ln);
                    lv_add(&out, "    pop ebx", file, ln);
                }
                done = 1;
            }
        } else if (!set_has(defs, "puts", 4) && call_target(t, "puts") && prev_fmt_load(lv, i, bits, name, sizeof name)) {
            int len = find_len(sl, nsl, name, strlen(name));
            if (len > 0) {
                char b[160];
                if (bits == 64) {
                    if (!fold_rdi(&out)) lv_add(&out, "    mov rsi, rdi", file, ln);
                    snprintf(b, sizeof b, "    mov edx, %d", len);
                    lv_add(&out, b, file, ln);
                    lv_add(&out, "    mov edi, 1", file, ln);
                    lv_add(&out, "    mov eax, 1", file, ln);
                    lv_add(&out, "    syscall", file, ln);
                    lv_add(&out, "    push 10", file, ln);
                    lv_add(&out, "    mov rsi, rsp", file, ln);
                    lv_add(&out, "    mov edx, 1", file, ln);
                    lv_add(&out, "    mov edi, 1", file, ln);
                    lv_add(&out, "    mov eax, 1", file, ln);
                    lv_add(&out, "    syscall", file, ln);
                    lv_add(&out, "    pop rax", file, ln);
                } else {
                    lv_add(&out, "    push ebx", file, ln);
                    lv_add(&out, "    mov ecx, [esp+4]", file, ln);
                    snprintf(b, sizeof b, "    mov edx, %d", len);
                    lv_add(&out, b, file, ln);
                    lv_add(&out, "    mov ebx, 1", file, ln);
                    lv_add(&out, "    mov eax, 4", file, ln);
                    lv_add(&out, "    int 0x80", file, ln);
                    lv_add(&out, "    push 10", file, ln);
                    lv_add(&out, "    mov ecx, esp", file, ln);
                    lv_add(&out, "    mov edx, 1", file, ln);
                    lv_add(&out, "    mov eax, 4", file, ln);
                    lv_add(&out, "    int 0x80", file, ln);
                    lv_add(&out, "    pop eax", file, ln);
                    lv_add(&out, "    pop ebx", file, ln);
                }
                done = 1;
            }
        } else if (!set_has(defs, "putchar", 7) && call_target(t, "putchar")) {
            if (bits == 64) {
                lv_add(&out, "    push rdi", file, ln);
                lv_add(&out, "    mov rsi, rsp", file, ln);
                lv_add(&out, "    mov edi, 1", file, ln);
                lv_add(&out, "    mov edx, 1", file, ln);
                lv_add(&out, "    mov eax, 1", file, ln);
                lv_add(&out, "    syscall", file, ln);
                lv_add(&out, "    pop rax", file, ln);
                lv_add(&out, "    movzx eax, al", file, ln);
            } else {
                lv_add(&out, "    push ebx", file, ln);
                lv_add(&out, "    lea ecx, [esp+8]", file, ln);
                lv_add(&out, "    mov edx, 1", file, ln);
                lv_add(&out, "    mov ebx, 1", file, ln);
                lv_add(&out, "    mov eax, 4", file, ln);
                lv_add(&out, "    int 0x80", file, ln);
                lv_add(&out, "    pop ebx", file, ln);
                lv_add(&out, "    movzx eax, byte [esp+4]", file, ln);
            }
            done = 1;
        } else if (!set_has(defs, "exit", 4) && call_target(t, "exit")) {
            if (bits == 64) {
                lv_add(&out, "    mov eax, 231", file, ln);
                lv_add(&out, "    syscall", file, ln);
            } else {
                lv_add(&out, "    mov ebx, [esp]", file, ln);
                lv_add(&out, "    mov eax, 252", file, ln);
                lv_add(&out, "    int 0x80", file, ln);
            }
            done = 1;
        }
        if (!done) lv_add(&out, lv->v[i].text, file, ln);
    }
    for (int i = 0; i < lv->n; i++) free(lv->v[i].text);
    free(lv->v);
    *lv = out;
    for (int i = 0; i < nsl; i++) free(sl[i].name);
    free(sl);
}

static int main_wants_env(const LineVec *lv)
{
    int in = 0;
    for (int i = 0; i < lv->n; i++) {
        const char *t = line_body(lv->v[i].text);
        if (!in) {
            if (!strncmp(t, "main:", 5) || !strncmp(t, "main :", 6)) in = 1;
            continue;
        }
        if (is_cmd(t, "call") || is_cmd(t, "ret")) return 0;
        if (strstr(t, "rdx") || strstr(t, "edx") || strstr(t, "environ") || strstr(t, "envp")) return 1;
    }
    return 0;
}

void libc_run(LineVec *lv, int kernel, int type, int defbits)
{
    if (kernel != K_LINUX || (type != T_BIN && type != T_ELF)) return;
    int bits = target_bits(lv, defbits);
    if (bits != 64 && bits != 32) return;
    Set gots = {0}, defs = {0}, refs = {0};
    int changed = 0;
    rewrite_got(lv, &gots, &changed);
    for (int i = 0; i < gots.n; i++) {
        char b[256];
        snprintf(b, sizeof b, "section .data");
        lv_add(lv, b, "<libc>", 1);
        snprintf(b, sizeof b, "__cly_got_%s: %s %s", gots.v[i], bits == 64 ? "dq" : "dd", gots.v[i]);
        lv_add(lv, b, "<libc>", 2);
    }
    collect_defs(lv, &defs);
    inline_calls(lv, &defs, bits);
    Lib L;
    lib_load(&L, bits == 64 ? libc_src64 : libc_src32);
    collect_refs(lv, &L, &defs, &refs);
    int main_def = has_def(&defs, "main");
    int has_entry = has_def(&defs, "_start") || has_def(&defs, "start") || has_def(&defs, "..start");
    int want_stub = main_def && !has_entry;
    if (!refs.n && !want_stub) return;
    strip_extern(lv, &L, &defs);
    for (int i = 0; i < refs.n; i++) {
        int bi;
        if (is_public_name(&L, refs.v[i], strlen(refs.v[i]), &bi)) need(&L, bi);
    }
    Str out = {0};
    for (int i = 0; i < L.n; i++) {
        if (!L.b[i].used) continue;
        sadd(&out, L.b[i].internal ? "" : "");
        sadd(&out, "section .text\n");
        sadd(&out, L.b[i].body);
    }
    for (int i = 0; i < refs.n; i++) {
        int bi;
        if (!is_public_name(&L, refs.v[i], strlen(refs.v[i]), &bi)) continue;
        if (L.b[bi].direct) continue;
        sfmt(&out, "section .text\n%s equ __cly_%s\n", refs.v[i], L.b[bi].names[0]);
    }
    if (want_stub) {
        if (bits == 64)
            sfmt(&out, "section .text\n_start:\n    mov edi, [rsp]\n    lea rsi, [rsp+8]\n%s    call main\n    mov edi, eax\n    push 231\n    pop rax\n    syscall\n", main_wants_env(lv) ? "    lea rdx, [rsi+rdi*8+8]\n" : "");
        else
            sadd(&out, "section .text\n_start:\n    mov eax, [esp]\n    lea ecx, [esp+4]\n    lea edx, [ecx+eax*4+4]\n    and esp, -16\n    sub esp, 4\n    push edx\n    push ecx\n    push eax\n    call main\n    mov ebx, eax\n    mov eax, 252\n    int 0x80\n");
    }
    char *s = sget(&out);
    int ln = 1;
    for (char *p = s; *p;) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        lv_add(lv, p, "<libc>", ln++);
        if (!nl) break;
        p = nl + 1;
    }
    free(s);
}
