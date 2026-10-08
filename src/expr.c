#include "cly.h"
#include <math.h>

ExprHooks g_hooks;

typedef struct {
    const char *p;
    int flags;
    int err;
} EP;

void val_const(Val *v, i64 n)
{
    memset(v, 0, sizeof *v);
    v->n = n;
    v->pos = v->neg = -1;
}

int val_isconst(const Val *v)
{
    return !v->unk && v->pos < 0 && v->neg < 0 && !v->ext && !v->hasreg;
}

int ident_start(int c) { return isalpha(c) || c == '_' || c == '.' || c == '?' || c == '@' || c == '#' || c == '~'; }
int ident_char(int c) { return isalnum(c) || c == '_' || c == '$' || c == '#' || c == '@' || c == '~' || c == '.' || c == '?'; }

static void sk(EP *e) { while (*e->p == ' ' || *e->p == '\t') e->p++; }

static int lowc(int c) { return tolower((unsigned char)c); }

static int digval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 99;
}

int parse_number_tok(const char *s, const char **end, i64 *out, int *isfloat)
{
    const char *p = s;
    *isfloat = 0;
    int base = 0;
    if (*p == '$' && digval(p[1]) < 10) {
        p++;
        base = 16;
    } else if (*p == '0' && (lowc(p[1]) == 'x' || lowc(p[1]) == 'h') && digval(p[2]) < 16) {
        p += 2; base = 16;
    } else if (*p == '0' && (lowc(p[1]) == 'b' || lowc(p[1]) == 'y') && (p[2] == '0' || p[2] == '1') ) {
        const char *q = p + 2;
        while (*q == '0' || *q == '1' || *q == '_') q++;
        if (!ident_char(*q) || *q == '.') { p += 2; base = 2; }
    } else if (*p == '0' && (lowc(p[1]) == 'o' || lowc(p[1]) == 'q') && digval(p[2]) < 8) {
        p += 2; base = 8;
    } else if (*p == '0' && (lowc(p[1]) == 'd' || lowc(p[1]) == 't') && digval(p[2]) < 10) {
        p += 2; base = 10;
    }
    if (base) {
        u64 v = 0;
        const char *q = p;
        while (*q == '_' || digval(*q) < base) {
            if (*q != '_') v = v * (u64)base + (u64)digval(*q);
            q++;
        }
        if (ident_char(*q) && *q != '.') return 0;
        *end = q;
        *out = (i64)v;
        return 1;
    }
    if (!isdigit((unsigned char)*p)) return 0;
    const char *q = p;
    while (isalnum((unsigned char)*q) || *q == '_') q++;
    int hexf = 0;
    if (q > p) {
        int ok = 1;
        for (const char *t = p; t < q - 1; t++)
            if (digval(*t) >= 16 && *t != '_') ok = 0;
        if (ok && lowc(q[-1]) == 'h' && q - p >= 2) hexf = 1;
    }
    if (hexf) {
        u64 v = 0;
        for (const char *t = p; t < q - 1; t++)
            if (*t != '_') v = v * 16 + (u64)digval(*t);
        *end = q;
        *out = (i64)v;
        return 1;
    }
    if (q > p) {
        char last = (char)lowc(q[-1]);
        int b = 0;
        if (last == 'b' || last == 'y') b = 2;
        else if (last == 'o' || last == 'q') b = 8;
        else if (last == 'd' || last == 't') b = 10;
        if (b) {
            int ok = 1;
            for (const char *t = p; t < q - 1; t++)
                if (*t != '_' && digval(*t) >= b) ok = 0;
            if (ok && q - p >= 2) {
                u64 v = 0;
                for (const char *t = p; t < q - 1; t++)
                    if (*t != '_') v = v * (u64)b + (u64)digval(*t);
                *end = q;
                *out = (i64)v;
                return 1;
            }
        }
    }
    q = p;
    int allnum = 1;
    while (isdigit((unsigned char)*q) || *q == '_') q++;
    if (*q == '.' && (isdigit((unsigned char)q[1]) || !ident_char(q[1]) || lowc(q[1]) == 'e')) {
        q++;
        while (isdigit((unsigned char)*q) || *q == '_') q++;
        *isfloat = 1;
    }
    if ((lowc(*q) == 'e') && (isdigit((unsigned char)q[1]) || ((q[1] == '+' || q[1] == '-') && isdigit((unsigned char)q[2])))) {
        q += 2;
        while (isdigit((unsigned char)*q)) q++;
        *isfloat = 1;
    }
    if (*isfloat) {
        *end = q;
        *out = 0;
        return 1;
    }
    q = p;
    u64 v = 0;
    while (isdigit((unsigned char)*q) || *q == '_') {
        if (*q != '_') v = v * 10 + (u64)(*q - '0');
        q++;
    }
    (void)allnum;
    if (ident_char(*q) && *q != '.') return 0;
    *end = q;
    *out = (i64)v;
    return 1;
}

static int hexn(const char *p, int n, u32 *v)
{
    u32 r = 0;
    for (int i = 0; i < n; i++) {
        int d = digval(p[i]);
        if (d >= 16) return 0;
        r = r * 16 + (u32)d;
    }
    *v = r;
    return 1;
}

static void put_utf8(Str *o, u32 c)
{
    if (c < 0x80) sadc(o, (int)c);
    else if (c < 0x800) { sadc(o, (int)(0xC0 | (c >> 6))); sadc(o, (int)(0x80 | (c & 63))); }
    else if (c < 0x10000) { sadc(o, (int)(0xE0 | (c >> 12))); sadc(o, (int)(0x80 | ((c >> 6) & 63))); sadc(o, (int)(0x80 | (c & 63))); }
    else { sadc(o, (int)(0xF0 | (c >> 18))); sadc(o, (int)(0x80 | ((c >> 12) & 63))); sadc(o, (int)(0x80 | ((c >> 6) & 63))); sadc(o, (int)(0x80 | (c & 63))); }
}

static int quote_at(const char *p, int *len, char *kind)
{
    unsigned char c = (unsigned char)p[0];
    if (c == '"' || c == '\'' || c == '`') { *len = 1; *kind = (char)c; return 1; }
    if (c == 0xE2 && (unsigned char)p[1] == 0x80) {
        unsigned char d = (unsigned char)p[2];
        if (d == 0x9C || d == 0x9D) { *len = 3; *kind = '"'; return 1; }
        if (d == 0x98 || d == 0x99) { *len = 3; *kind = '\''; return 1; }
    }
    return 0;
}

int parse_charconst(const char **pp, Str *out)
{
    const char *p = *pp;
    int ql;
    char kind;
    if (!quote_at(p, &ql, &kind)) return 0;
    p += ql;
    for (;;) {
        if (!*p) { err("unterminated string"); *pp = p; return 1; }
        int cl;
        char k2;
        if (quote_at(p, &cl, &k2) && (kind == '`' ? k2 == '`' : (k2 == kind || (kind == '"' && k2 == '"') || (kind == '\'' && k2 == '\'')))) {
            if (kind == '`' && 0) break;
            p += cl;
            break;
        }
        if (kind == '`' && *p == '\\') {
            p++;
            switch (*p) {
            case 'a': sadc(out, 7); p++; break;
            case 'b': sadc(out, 8); p++; break;
            case 't': sadc(out, 9); p++; break;
            case 'n': sadc(out, 10); p++; break;
            case 'v': sadc(out, 11); p++; break;
            case 'f': sadc(out, 12); p++; break;
            case 'r': sadc(out, 13); p++; break;
            case 'e': sadc(out, 27); p++; break;
            case '\\': sadc(out, '\\'); p++; break;
            case '\'': sadc(out, '\''); p++; break;
            case '"': sadc(out, '"'); p++; break;
            case '`': sadc(out, '`'); p++; break;
            case '?': sadc(out, '?'); p++; break;
            case 'x': {
                u32 v = 0;
                int n = 0;
                p++;
                while (n < 2 && digval(*p) < 16) { v = v * 16 + (u32)digval(*p); p++; n++; }
                sadc(out, (int)v);
                break;
            }
            case 'u': {
                u32 v = 0;
                p++;
                if (hexn(p, 4, &v)) { p += 4; put_utf8(out, v); }
                break;
            }
            case 'U': {
                u32 v = 0;
                p++;
                if (hexn(p, 8, &v)) { p += 8; put_utf8(out, v); }
                break;
            }
            default:
                if (*p >= '0' && *p <= '7') {
                    u32 v = 0;
                    int n = 0;
                    while (n < 3 && *p >= '0' && *p <= '7') { v = v * 8 + (u32)(*p - '0'); p++; n++; }
                    sadc(out, (int)(v & 255));
                } else if (*p) {
                    sadc(out, *p);
                    p++;
                }
            }
            continue;
        }
        sadc(out, *p);
        p++;
    }
    *pp = p;
    return 1;
}

static void v_unk(Val *v)
{
    val_const(v, 0);
    v->unk = 1;
}

static const char *skipws_e(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

static int is_reg_val(const Val *v) { return v->hasreg; }

static int v_add(Val *a, const Val *b, int sign)
{
    if (a->unk || b->unk) {
        a->unk = 1;
        return 1;
    }
    for (int i = 0; i < b->nrseq; i++) {
        int f = 0;
        for (int j = 0; j < a->nrseq; j++) if (a->rseq[j] == b->rseq[i]) f = 1;
        if (!f && a->nrseq < 4) a->rseq[a->nrseq++] = b->rseq[i];
    }
    if (a->hasreg || b->hasreg) {
        for (int i = 0; i < VR_N; i++) a->r[i] = (signed char)(a->r[i] + sign * b->r[i]);
        a->hasreg = 0;
        for (int i = 0; i < VR_N; i++) if (a->r[i]) a->hasreg = 1;
    }
    if (b->ext) {
        if (sign > 0) {
            if (a->ext) { err("cannot add two external symbols"); return 0; }
            a->ext = b->ext;
        } else {
            if (a->ext == b->ext) a->ext = NULL;
            else { err("invalid subtraction of external symbol"); return 0; }
        }
    }
    a->n += sign * b->n;
    int bp = b->pos, bn = b->neg;
    if (sign < 0) { int t = bp; bp = bn; bn = t; }
    if (bp >= 0) {
        if (a->neg == bp) a->neg = -1;
        else if (a->pos < 0) a->pos = bp;
        else { err("invalid combination of relocatable values"); return 0; }
    }
    if (bn >= 0) {
        if (a->pos == bn) a->pos = -1;
        else if (a->neg < 0) a->neg = bn;
        else { err("invalid combination of relocatable values"); return 0; }
    }
    return 1;
}

static int need_const(const Val *a, const Val *b, const char *op)
{
    if ((a->unk) || (b && b->unk)) return 1;
    if (!val_isconst(a) || (b && !val_isconst(b))) {
        err("operator `%s' requires constant operands", op);
        return 0;
    }
    return 1;
}

static int cmp_prec(void) { return 0; }

static int parse_expr(EP *e, Val *v);

static int parse_float_text(EP *e, char *buf, size_t sz)
{
    const char *p = e->p;
    size_t n = 0;
    if (*p == '-' || *p == '+') { buf[n++] = *p++; }
    if (*p == '0' && lowc(p[1]) == 'x') {
        buf[n++] = *p++;
        buf[n++] = *p++;
        while ((isxdigit((unsigned char)*p) || *p == '.' || *p == '_') && n < sz - 2) { if (*p != '_') buf[n++] = *p; p++; }
        if (lowc(*p) == 'p') {
            buf[n++] = *p++;
            if (*p == '+' || *p == '-') buf[n++] = *p++;
            while (isdigit((unsigned char)*p) && n < sz - 2) buf[n++] = *p++;
        }
    } else {
        while ((isdigit((unsigned char)*p) || *p == '.' || *p == '_') && n < sz - 2) { if (*p != '_') buf[n++] = *p; p++; }
        if (lowc(*p) == 'e') {
            buf[n++] = *p++;
            if (*p == '+' || *p == '-') buf[n++] = *p++;
            while (isdigit((unsigned char)*p) && n < sz - 2) buf[n++] = *p++;
        }
    }
    buf[n] = 0;
    e->p = p;
    return (int)n;
}

static int parse_primary(EP *e, Val *v)
{
    sk(e);
    const char *p = e->p;
    if (*p == '(') {
        e->p++;
        if (!parse_expr(e, v)) return 0;
        sk(e);
        if (*e->p != ')') { err("expecting `)' in expression"); return 0; }
        e->p++;
        return 1;
    }
    int ql;
    char qk;
    if (quote_at(p, &ql, &qk)) {
        Str s = {0};
        const char *q = p;
        parse_charconst(&q, &s);
        e->p = q;
        size_t n = s.n;
        if (n > 8) { warn("character constant too long, truncated"); n = 8; }
        u64 r = 0;
        for (size_t i = 0; i < n; i++) r |= (u64)(u8)s.s[i] << (8 * i);
        val_const(v, (i64)r);
        free(s.s);
        return 1;
    }
    if (isdigit((unsigned char)*p) || (*p == '$' && digval(p[1]) < 10 && isdigit((unsigned char)p[1]))) {
        const char *end;
        i64 n;
        int isf;
        if (parse_number_tok(p, &end, &n, &isf)) {
            if (isf) {
                u8 tmp[16];
                char buf[256];
                parse_float_text(e, buf, sizeof buf);
                float_encode(buf, 4, tmp);
                u32 b = (u32)tmp[0] | ((u32)tmp[1] << 8) | ((u32)tmp[2] << 16) | ((u32)tmp[3] << 24);
                val_const(v, (i64)b);
                v->isfloat = 1;
                return 1;
            }
            e->p = end;
            val_const(v, n);
            return 1;
        }
        err("invalid number `%.12s'", p);
        return 0;
    }
    if (*p == '$') {
        if (p[1] == '$') {
            e->p += 2;
            if (e->flags & EF_PP) { err("`$$' not allowed in preprocessor expression"); return 0; }
            g_hooks.sec_start(v);
            return 1;
        }
        if (!ident_start(p[1])) {
            e->p++;
            if (e->flags & EF_PP) { err("`$' not allowed in preprocessor expression"); return 0; }
            g_hooks.cur_loc(v);
            return 1;
        }
        p++;
    }
    if (ident_start(*p) || *p == '$') {
        const char *q = p;
        if (*q == '$') q++;
        if (*q == '.' || ident_start(*q)) {
            q++;
            while (ident_char(*q)) q++;
        }
        char name[256];
        size_t n = (size_t)(q - p);
        if (n >= sizeof name) n = sizeof name - 1;
        memcpy(name, p, n);
        name[n] = 0;
        int escaped = (*e->p == '$' && e->p[1] != '$');
        e->p = q;
        if (!escaped) {
            if (!strcasecmp(name, "seg")) {
                Val t;
                if (!parse_primary(e, &t)) return 0;
                val_const(v, 0);
                v->unk = t.unk;
                v->seg = 1;
                return 1;
            }
            const char *fn = NULL;
            static const char *fnames[] = { "__float8__", "__float16__", "__bfloat16__", "__float32__", "__float64__", "__float80m__", "__float80e__", "__float128l__", "__float128h__", "__infinity__", "__qnan__", "__snan__", "__nan__", "__utf16__", "__utf16le__", "__utf16be__", "__utf32__", "__utf32le__", "__utf32be__", NULL };
            for (int i = 0; fnames[i]; i++) if (!strcasecmp(name, fnames[i])) fn = fnames[i];
            if (!strncasecmp(name, "__?", 3)) {
                char t2[256];
                size_t l2 = strlen(name);
                if (l2 > 5 && !strcmp(name + l2 - 3, "?__")) {
                    snprintf(t2, sizeof t2, "__%.*s__", (int)(l2 - 6), name + 3);
                    for (int i = 0; fnames[i]; i++) if (!strcasecmp(t2, fnames[i])) fn = fnames[i];
                    if (fn) strcpy(name, t2);
                }
            }
            if (fn) {
                u8 tmp[16];
                int nb = 0;
                if (!strcasecmp(fn, "__infinity__") || !strcasecmp(fn, "__qnan__") || !strcasecmp(fn, "__snan__") || !strcasecmp(fn, "__nan__")) {
                    if (!float_special(name, "", tmp, &nb)) { err("bad floating point constant"); return 0; }
                    u64 r = 0;
                    for (int i = 0; i < 4; i++) r |= (u64)tmp[i] << (8 * i);
                    val_const(v, (i64)r);
                    return 1;
                }
                sk(e);
                if (*e->p != '(') { err("`%s' requires an argument", name); return 0; }
                e->p++;
                sk(e);
                char buf[256];
                int ql2;
                char qk2;
                if (quote_at(e->p, &ql2, &qk2)) {
                    Str s = {0};
                    const char *q2 = e->p;
                    parse_charconst(&q2, &s);
                    e->p = q2;
                    sk(e);
                    if (*e->p == ')') e->p++;
                    if (!strncasecmp(fn, "__utf", 5)) {
                        err("`%s' is only valid in data directives", name);
                        free(s.s);
                        return 0;
                    }
                    snprintf(buf, sizeof buf, "%.200s", s.s ? s.s : "");
                    free(s.s);
                } else {
                    const char *st = e->p;
                    if (!strncasecmp(e->p, "__", 2) && ident_start(e->p[2])) {
                        const char *q2 = e->p;
                        while (ident_char(*q2)) q2++;
                        size_t l2 = (size_t)(q2 - e->p);
                        if (l2 >= sizeof buf) l2 = sizeof buf - 1;
                        memcpy(buf, e->p, l2);
                        buf[l2] = 0;
                        e->p = q2;
                    } else {
                        parse_float_text(e, buf, sizeof buf);
                    }
                    sk(e);
                    if (*e->p != ')') { err("expecting `)' after floating point argument"); return 0; }
                    e->p++;
                    (void)st;
                }
                int kind = 0;
                int part = 0;
                if (!strcasecmp(fn, "__float8__")) kind = 1;
                else if (!strcasecmp(fn, "__float16__")) kind = 2;
                else if (!strcasecmp(fn, "__bfloat16__")) kind = 3;
                else if (!strcasecmp(fn, "__float32__")) kind = 4;
                else if (!strcasecmp(fn, "__float64__")) kind = 8;
                else if (!strcasecmp(fn, "__float80m__")) { kind = 10; part = 0; }
                else if (!strcasecmp(fn, "__float80e__")) { kind = 10; part = 1; }
                else if (!strcasecmp(fn, "__float128l__")) { kind = 16; part = 0; }
                else if (!strcasecmp(fn, "__float128h__")) { kind = 16; part = 1; }
                if (!float_encode(buf, kind, tmp)) { err("bad floating point constant `%s'", buf); return 0; }
                u64 r = 0;
                if (kind == 10 && part == 0) for (int i = 0; i < 8; i++) r |= (u64)tmp[i] << (8 * i);
                else if (kind == 10) r = (u64)tmp[8] | ((u64)tmp[9] << 8);
                else if (kind == 16) for (int i = 0; i < 8; i++) r |= (u64)tmp[part * 8 + i] << (8 * i);
                else for (int i = 0; i < kind; i++) r |= (u64)tmp[i] << (8 * i);
                val_const(v, (i64)r);
                return 1;
            }
            const char *ilog[] = { "ilog2e", "ilog2w", "ilog2f", "ilog2c", NULL };
            if ((!strcasecmp(name, "ilog2") || !strcasecmp(name, "__?ilog2?__") || !strcasecmp(name, "__ilog2__")) && *skipws_e(e->p) == '(') strcpy(name, "ilog2e");
            for (int i = 0; ilog[i]; i++) {
                const char *nm = name;
                if (!strncmp(nm, "__?", 3)) nm += 3;
                if (!strncasecmp(nm, ilog[i], 6) && (nm[6] == 0 || !strcmp(nm + 6, "?__") || !strcmp(nm + 6, "__"))) {
                    sk(e);
                    if (*e->p != '(') { err("`%s' requires an argument", name); return 0; }
                    e->p++;
                    Val t;
                    if (!parse_expr(e, &t)) return 0;
                    sk(e);
                    if (*e->p != ')') { err("expecting `)'"); return 0; }
                    e->p++;
                    val_const(v, 0);
                    if (t.unk) { v->unk = 1; return 1; }
                    if (!val_isconst(&t) || t.n <= 0) { err("`%s' requires a positive constant", name); return 0; }
                    u64 x = (u64)t.n;
                    int fl = 63;
                    while (!((x >> fl) & 1)) fl--;
                    int pow2 = (x & (x - 1)) == 0;
                    int r = fl;
                    if (i == 0) { if (!pow2) { err("`ilog2e': argument is not a power of two"); return 0; } }
                    else if (i == 1) { if (!pow2) warn("`ilog2w': argument is not a power of two"); }
                    else if (i == 3) { if (!pow2) r = fl + 1; }
                    v->n = r;
                    return 1;
                }
            }
            int sz;
            int rid = reg_lookup(name, &sz);
            if (rid) {
                if (!(e->flags & EF_EA)) { err("register `%s' not allowed in this expression", name); return 0; }
                int slot = reg_slot(rid);
                if (slot < 0) { err("register `%s' cannot be used in an address", name); return 0; }
                val_const(v, 0);
                v->hasreg = 1;
                v->r[slot] = 1;
                v->rseq[v->nrseq++] = (short)slot;
                return 1;
            }
        }
        if (e->flags & EF_PP) {
            if (!strcmp(name, "__PASS__")) { val_const(v, 3); return 1; }
            err("non-constant in preprocessor expression: `%s'", name);
            return 0;
        }
        if (!g_hooks.sym_lookup(name, v)) {
            v_unk(v);
        }
        return 1;
    }
    err("expression syntax error near `%.20s'", p);
    return 0;
}

static int parse_unary(EP *e, Val *v)
{
    sk(e);
    char c = *e->p;
    if (c == '-' || c == '+' || c == '~' || c == '!') {
        e->p++;
        if (!parse_unary(e, v)) return 0;
        if (v->unk) return 1;
        if (c == '-') {
            Val z;
            val_const(&z, 0);
            if (!v_add(&z, v, -1)) return 0;
            *v = z;
        } else if (c == '~') {
            if (!need_const(v, NULL, "~")) return 0;
            v->n = ~v->n;
        } else if (c == '!') {
            if (!need_const(v, NULL, "!")) return 0;
            v->n = !v->n;
        }
        return 1;
    }
    return parse_primary(e, v);
}

static int parse_mul(EP *e, Val *v)
{
    if (!parse_unary(e, v)) return 0;
    for (;;) {
        sk(e);
        const char *p = e->p;
        int op = 0;
        if (*p == '*') { op = '*'; e->p++; }
        else if (*p == '/' && p[1] == '/') { op = 'D'; e->p += 2; }
        else if (*p == '/') { op = '/'; e->p++; }
        else if (*p == '%' && p[1] == '%') { op = 'M'; e->p += 2; }
        else if (*p == '%' && !isalpha((unsigned char)p[1]) && p[1] != '$' && p[1] != '{' && p[1] != '[' && p[1] != '?' && p[1] != '!' && p[1] != '+') { op = '%'; e->p++; }
        else break;
        Val r;
        if (!parse_unary(e, &r)) return 0;
        if (op == '*' && (v->hasreg || r.hasreg) && !v->unk && !r.unk) {
            if (v->hasreg && r.hasreg) { err("cannot multiply two registers"); return 0; }
            Val *rg = v->hasreg ? v : &r;
            Val *k = v->hasreg ? &r : v;
            if (!val_isconst(k)) { err("register scale must be constant"); return 0; }
            for (int i = 0; i < VR_N; i++) rg->r[i] = (signed char)(rg->r[i] * k->n);
            Val res = *rg;
            res.n = rg->n * k->n;
            *v = res;
            continue;
        }
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "*")) return 0;
        i64 a = v->n, b = r.n;
        switch (op) {
        case '*': v->n = (i64)((u64)a * (u64)b); break;
        case '/': if (!b) { err("division by zero"); return 0; } v->n = (i64)((u64)a / (u64)b); break;
        case 'D': if (!b) { err("division by zero"); return 0; } v->n = a / b; break;
        case '%': if (!b) { err("division by zero"); return 0; } v->n = (i64)((u64)a % (u64)b); break;
        case 'M': if (!b) { err("division by zero"); return 0; } v->n = a % b; break;
        }
    }
    return 1;
}

static int parse_add(EP *e, Val *v)
{
    if (!parse_mul(e, v)) return 0;
    for (;;) {
        sk(e);
        char c = *e->p;
        if (c != '+' && c != '-') break;
        e->p++;
        Val r;
        if (!parse_mul(e, &r)) return 0;
        if (!v_add(v, &r, c == '+' ? 1 : -1)) return 0;
    }
    return 1;
}

static int parse_shift(EP *e, Val *v)
{
    if (!parse_add(e, v)) return 0;
    for (;;) {
        sk(e);
        const char *p = e->p;
        int op = 0;
        if (p[0] == '<' && p[1] == '<' && p[2] == '<') { op = 'L'; e->p += 3; }
        else if (p[0] == '>' && p[1] == '>' && p[2] == '>') { op = 'A'; e->p += 3; }
        else if (p[0] == '<' && p[1] == '<') { op = 'l'; e->p += 2; }
        else if (p[0] == '>' && p[1] == '>') { op = 'r'; e->p += 2; }
        else break;
        Val r;
        if (!parse_add(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "<<")) return 0;
        i64 sh = r.n;
        if (sh < 0 || sh > 63) { v->n = (op == 'A' && v->n < 0) ? -1 : 0; continue; }
        if (op == 'l' || op == 'L') v->n = (i64)((u64)v->n << sh);
        else if (op == 'r') v->n = (i64)((u64)v->n >> sh);
        else v->n = v->n >> sh;
    }
    return 1;
}

static int parse_and(EP *e, Val *v)
{
    if (!parse_shift(e, v)) return 0;
    for (;;) {
        sk(e);
        if (*e->p != '&' || e->p[1] == '&') break;
        e->p++;
        Val r;
        if (!parse_shift(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "&")) return 0;
        v->n &= r.n;
    }
    return 1;
}

static int parse_xor(EP *e, Val *v)
{
    if (!parse_and(e, v)) return 0;
    for (;;) {
        sk(e);
        if (*e->p != '^' || e->p[1] == '^') break;
        e->p++;
        Val r;
        if (!parse_and(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "^")) return 0;
        v->n ^= r.n;
    }
    return 1;
}

static int parse_or(EP *e, Val *v)
{
    if (!parse_xor(e, v)) return 0;
    for (;;) {
        sk(e);
        if (*e->p != '|' || e->p[1] == '|') break;
        e->p++;
        Val r;
        if (!parse_xor(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "|")) return 0;
        v->n |= r.n;
    }
    return 1;
}

static int same_reloc(const Val *a, const Val *b)
{
    return a->pos == b->pos && a->neg == b->neg && a->ext == b->ext;
}

static int parse_cmp(EP *e, Val *v)
{
    (void)cmp_prec;
    if (!parse_or(e, v)) return 0;
    for (;;) {
        sk(e);
        const char *p = e->p;
        int op = 0;
        if (p[0] == '<' && p[1] == '=' && p[2] == '>') { op = 'S'; e->p += 3; }
        else if (p[0] == '<' && p[1] == '>') { op = 'n'; e->p += 2; }
        else if (p[0] == '<' && p[1] == '=') { op = 'l'; e->p += 2; }
        else if (p[0] == '>' && p[1] == '=') { op = 'g'; e->p += 2; }
        else if (p[0] == '=' && p[1] == '=') { op = 'e'; e->p += 2; }
        else if (p[0] == '!' && p[1] == '=') { op = 'n'; e->p += 2; }
        else if (p[0] == '<' && p[1] != '<') { op = '<'; e->p++; }
        else if (p[0] == '>' && p[1] != '>') { op = '>'; e->p++; }
        else if (p[0] == '=') { op = 'e'; e->p++; }
        else break;
        Val r;
        if (!parse_or(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (v->hasreg || r.hasreg) { err("registers cannot be compared"); return 0; }
        i64 a, b;
        if (same_reloc(v, &r)) { a = v->n; b = r.n; }
        else if (!need_const(v, &r, "comparison")) return 0;
        else { a = v->n; b = r.n; }
        i64 res = 0;
        switch (op) {
        case '<': res = a < b; break;
        case '>': res = a > b; break;
        case 'l': res = a <= b; break;
        case 'g': res = a >= b; break;
        case 'e': res = a == b; break;
        case 'n': res = a != b; break;
        case 'S': res = a < b ? -1 : (a > b ? 1 : 0); break;
        }
        val_const(v, res);
    }
    return 1;
}

static int parse_land(EP *e, Val *v)
{
    if (!parse_cmp(e, v)) return 0;
    for (;;) {
        sk(e);
        if (!(e->p[0] == '&' && e->p[1] == '&')) break;
        e->p += 2;
        Val r;
        if (!parse_cmp(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "&&")) return 0;
        v->n = (v->n != 0) && (r.n != 0);
    }
    return 1;
}

static int parse_lxor(EP *e, Val *v)
{
    if (!parse_land(e, v)) return 0;
    for (;;) {
        sk(e);
        if (!(e->p[0] == '^' && e->p[1] == '^')) break;
        e->p += 2;
        Val r;
        if (!parse_land(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "^^")) return 0;
        v->n = (v->n != 0) != (r.n != 0);
    }
    return 1;
}

static int parse_lor(EP *e, Val *v)
{
    if (!parse_lxor(e, v)) return 0;
    for (;;) {
        sk(e);
        if (!(e->p[0] == '|' && e->p[1] == '|')) break;
        e->p += 2;
        Val r;
        if (!parse_lxor(e, &r)) return 0;
        if (v->unk || r.unk) { v->unk = 1; continue; }
        if (!need_const(v, &r, "||")) return 0;
        v->n = (v->n != 0) || (r.n != 0);
    }
    return 1;
}

static int parse_wrt(EP *e, Val *v)
{
    if (!parse_lor(e, v)) return 0;
    for (;;) {
        sk(e);
        const char *p = e->p;
        if ((p[0] == 'w' || p[0] == 'W') && (p[1] == 'r' || p[1] == 'R') && (p[2] == 't' || p[2] == 'T') && !ident_char(p[3])) {
            e->p += 3;
            Val r;
            if (!parse_lor(e, &r)) return 0;
            v->wrt = 1;
            continue;
        }
        break;
    }
    return 1;
}

static int parse_expr(EP *e, Val *v)
{
    if (!parse_wrt(e, v)) return 0;
    sk(e);
    if (*e->p == '?' && !(e->flags & EF_EA)) {
        const char *q = e->p + 1;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == 0 || *q == ',' || *q == ')' || *q == ';') return 1;
        e->p++;
        Val a, b;
        if (!parse_expr(e, &a)) return 0;
        sk(e);
        if (*e->p != ':') { err("expecting `:' in conditional expression"); return 0; }
        e->p++;
        if (!parse_expr(e, &b)) return 0;
        if (v->unk) { *v = a; v->unk = 1; return 1; }
        if (!val_isconst(v)) { err("condition of `?:' must be constant"); return 0; }
        *v = v->n ? a : b;
    }
    return 1;
}

int expr_parse(const char **pp, Val *out, int flags)
{
    EP e;
    e.p = *pp;
    e.flags = flags;
    e.err = 0;
    int ec = g_errors;
    memset(out, 0, sizeof *out);
    out->pos = out->neg = -1;
    int ok = parse_expr(&e, out);
    *pp = e.p;
    if (!ok || g_errors != ec) return 0;
    (void)is_reg_val;
    return 1;
}

int expr_eval_str(const char *s, Val *out, int flags)
{
    const char *p = s;
    if (!expr_parse(&p, out, flags)) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p && *p != ';') {
        err("junk after expression: `%.20s'", p);
        return 0;
    }
    return 1;
}
