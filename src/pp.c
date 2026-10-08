#include "cly.h"
#include <time.h>

typedef struct SMac {
    char *name;
    int casei, isfunc, nparams, variadic;
    char **params;
    char *body;
    struct SMac *next;
} SMac;

typedef struct MMac {
    char *name;
    int casei, minp, maxp, greedy;
    char **defaults;
    int ndef;
    LineVec body;
    struct MMac *next;
} MMac;

typedef struct Ctx {
    char *name;
    int id;
    int stk;
    int argoff, locoff;
} Ctx;

typedef struct { int parent, active, taken, hadelse; } Cond;

typedef struct Inv {
    char **args;
    int nargs;
    int id;
    char *name;
    int exitmac;
} Inv;

typedef struct Src {
    LineVec *lv;
    int i;
    Inv *inv;
} Src;

typedef struct Active { SMac *m; struct Active *up; } Active;

typedef struct {
    HT smacs, smacs_ci, mmacs, mmacs_ci;
    Ctx ctx[256];
    int nctx, ctxcounter;
    Cond conds[256];
    int ncond;
    int maccounter;
    PPOpts *opts;
    LineVec *out;
    int exitrep;
    int depth;
    const char *curfile;
    int curline;
    int bits;
    char *sect;
    Inv *curinv;
    const char *lo_src, *lo_file;
    int lo_on, lo_delta;
    int ok;
    int quiet;
} PP;

static PP P;

static char **name_pool;
static int name_pool_n;

static const char *intern(const char *s)
{
    for (int i = 0; i < name_pool_n; i++) if (!strcmp(name_pool[i], s)) return name_pool[i];
    name_pool = xrealloc(name_pool, sizeof(char *) * (size_t)(name_pool_n + 1));
    name_pool[name_pool_n] = xstrdup(s);
    return name_pool[name_pool_n++];
}

static char *lower(const char *s)
{
    char *r = xstrdup(s);
    for (char *p = r; *p; p++) *p = (char)tolower((unsigned char)*p);
    return r;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

static int qlen_at(const char *p, char *kind)
{
    unsigned char c = (unsigned char)p[0];
    if (c == '"' || c == '\'' || c == '`') { *kind = (char)c; return 1; }
    if (c == 0xE2 && (unsigned char)p[1] == 0x80) {
        unsigned char d = (unsigned char)p[2];
        if (d == 0x9C || d == 0x9D) { *kind = '"'; return 3; }
        if (d == 0x98 || d == 0x99) { *kind = '\''; return 3; }
    }
    return 0;
}

static const char *skip_quoted(const char *p)
{
    char k;
    int l = qlen_at(p, &k);
    if (!l) return p + 1;
    p += l;
    while (*p) {
        char k2;
        int l2 = qlen_at(p, &k2);
        if (k == '`' && *p == '\\' && p[1]) { p += 2; continue; }
        if (l2 && k2 == k) return p + l2;
        p++;
    }
    return p;
}

static void strip_comment(char *s)
{
    const char *p = s;
    while (*p) {
        char k;
        if (qlen_at(p, &k)) { p = skip_quoted(p); continue; }
        if (*p == ';') { s[p - s] = 0; return; }
        p++;
    }
}

static SMac *smac_find_in(HT *h, const char *key, int nargs, int isfunc_call)
{
    SMac *m = ht_get(h, key);
    for (; m; m = m->next) {
        if (isfunc_call) { if (m->isfunc && (m->nparams == nargs || (m->variadic && nargs >= m->nparams - 1))) return m; }
        else if (!m->isfunc) return m;
    }
    return NULL;
}

static char *ctx_key(const char *name)
{
    const char *p = name;
    int up = 0;
    if (p[0] == '%' && p[1] == '$') {
        p += 2;
        while (*p == '$') { up++; p++; }
        int idx = P.nctx - 1 - up;
        if (idx < 0) { err("`%%$' used in the absence of a context"); return NULL; }
        Str s = {0};
        sfmt(&s, "%%$%d:%s", P.ctx[idx].id, p);
        return sget(&s);
    }
    return xstrdup(name);
}

static SMac *smac_lookup(const char *name, int nargs, int isfunc_call)
{
    char *key = ctx_key(name);
    if (!key) return NULL;
    SMac *m = smac_find_in(&P.smacs, key, nargs, isfunc_call);
    if (!m && key[0] != '%') {
        char *lk = lower(key);
        m = smac_find_in(&P.smacs_ci, lk, nargs, isfunc_call);
        free(lk);
    }
    free(key);
    return m;
}

static void smac_free(SMac *m)
{
    free(m->name);
    free(m->body);
    for (int i = 0; i < m->nparams; i++) free(m->params[i]);
    free(m->params);
    free(m);
}

static void smac_undef(const char *name)
{
    char *key = ctx_key(name);
    if (!key) return;
    for (int ci = 0; ci < 2; ci++) {
        HT *h = ci ? &P.smacs_ci : &P.smacs;
        char *k = ci ? lower(key) : xstrdup(key);
        SMac *m = ht_get(h, k);
        if (m) { ht_del(h, k); while (m) { SMac *n = m->next; smac_free(m); m = n; } }
        free(k);
    }
    free(key);
}

static void smac_define(const char *name, int casei, int isfunc, char **params, int nparams, int variadic, const char *body)
{
    char *key = ctx_key(name);
    if (!key) return;
    HT *h = casei ? &P.smacs_ci : &P.smacs;
    char *k = casei ? lower(key) : xstrdup(key);
    SMac *m = xcalloc(1, sizeof *m);
    m->name = xstrdup(key);
    m->casei = casei;
    m->isfunc = isfunc;
    m->nparams = nparams;
    m->variadic = variadic;
    m->params = params;
    m->body = xstrdup(body);
    SMac *old = ht_get(h, k);
    SMac *head = NULL, **tail = &head;
    for (SMac *o = old; o; ) {
        SMac *n = o->next;
        if (o->isfunc == isfunc && (!isfunc || o->nparams == nparams)) smac_free(o);
        else { o->next = NULL; *tail = o; tail = &o->next; }
        o = n;
    }
    m->next = head;
    ht_put(h, k, m);
    free(k);
    free(key);
}

static void define_simple(const char *name, const char *body)
{
    smac_define(name, 0, 0, NULL, 0, 0, body);
}

static void expand_text(const char *in, Str *out, Active *act);

static int is_active(Active *a, SMac *m);
static SMac *smac_lookup(const char *name, int nargs, int isfunc_call);

static char *paste_operand(const char *tok, size_t n, Active *act)
{
    char *t = xstrndup(tok, n);
    SMac *m = ident_start((unsigned char)t[0]) ? smac_lookup(t, 0, 0) : NULL;
    if (!m || is_active(act, m)) return t;
    Str o = {0};
    expand_text(t, &o, act);
    free(t);
    char *r = trim(sget(&o));
    return xstrdup(r);
}

static void paste_tokens(Str *s, Active *act)
{
    if (!s->s) return;
    char *src = s->s;
    if (!strstr(src, "%+")) return;
    Str r = {0};
    const char *p = src;
    while (*p) {
        char k;
        if (qlen_at(p, &k)) {
            const char *e = skip_quoted(p);
            saddn(&r, p, (size_t)(e - p));
            p = e;
            continue;
        }
        if (p[0] == '%' && p[1] == '+' && !isdigit((unsigned char)p[2])) {
            while (r.n && (r.s[r.n - 1] == ' ' || r.s[r.n - 1] == '\t')) r.s[--r.n] = 0;
            size_t e = r.n;
            while (e && ident_char((unsigned char)r.s[e - 1])) e--;
            if (e < r.n) {
                char *lt = paste_operand(r.s + e, r.n - e, act);
                r.n = e;
                r.s[e] = 0;
                sadd(&r, lt);
                free(lt);
            }
            p += 2;
            while (*p == ' ' || *p == '\t') p++;
            const char *q = p;
            while (ident_char((unsigned char)*q)) q++;
            if (q > p) {
                char *rt = paste_operand(p, (size_t)(q - p), act);
                sadd(&r, rt);
                free(rt);
                p = q;
            }
            continue;
        }
        sadc(&r, *p++);
    }
    free(s->s);
    *s = r;
}

static int parse_call_args(const char **pp, char ***args, int *nargs)
{
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '(') return 0;
    p++;
    int cap = 4, n = 0;
    char **a = xmalloc(sizeof(char *) * (size_t)cap);
    Str cur = {0};
    int depth = 0;
    int ended = 0;
    while (*p) {
        char k;
        if (qlen_at(p, &k)) {
            const char *e = skip_quoted(p);
            saddn(&cur, p, (size_t)(e - p));
            p = e;
            continue;
        }
        if (*p == '{') {
            int d2 = 1;
            p++;
            while (*p && d2) {
                if (*p == '{') d2++;
                else if (*p == '}') { d2--; if (!d2) break; }
                sadc(&cur, *p);
                p++;
            }
            if (*p == '}') p++;
            continue;
        }
        if (*p == '(') depth++;
        if (*p == ')') {
            if (depth == 0) { p++; ended = 1; break; }
            depth--;
        }
        if (*p == ',' && depth == 0) {
            if (n == cap) { cap *= 2; a = xrealloc(a, sizeof(char *) * (size_t)cap); }
            a[n++] = xstrdup(trim(sget(&cur)));
            sclear(&cur);
            p++;
            continue;
        }
        sadc(&cur, *p++);
    }
    if (!ended) { free(cur.s); for (int i = 0; i < n; i++) free(a[i]); free(a); return -1; }
    char *t = trim(sget(&cur));
    if (n > 0 || *t) {
        if (n == cap) { cap *= 2; a = xrealloc(a, sizeof(char *) * (size_t)cap); }
        a[n++] = xstrdup(t);
    }
    free(cur.s);
    *pp = p;
    *args = a;
    *nargs = n;
    return 1;
}

static int is_active(Active *act, SMac *m)
{
    for (; act; act = act->up) if (act->m == m) return 1;
    return 0;
}

static void expand_smac_body(SMac *m, char **args, int nargs, Str *out, Active *act)
{
    Str sub = {0};
    const char *b = m->body;
    while (*b) {
        char k;
        if (qlen_at(b, &k)) {
            const char *e = skip_quoted(b);
            saddn(&sub, b, (size_t)(e - b));
            b = e;
            continue;
        }
        if (ident_start((unsigned char)*b) || (*b == '$' && ident_start((unsigned char)b[1]))) {
            const char *s = b;
            if (*b == '$') b++;
            while (ident_char((unsigned char)*b)) b++;
            size_t n = (size_t)(b - s);
            int found = -1;
            for (int i = 0; i < m->nparams; i++)
                if (strlen(m->params[i]) == n && !strncmp(m->params[i], s, n)) { found = i; break; }
            if (m->variadic && n == 11 && !strncmp(s, "__VA_ARGS__", 11)) found = m->nparams - 1;
            if (found >= 0) {
                if (m->variadic && found == m->nparams - 1) {
                    for (int i = found; i < nargs; i++) {
                        if (i > found) sadd(&sub, ", ");
                        sadd(&sub, args[i]);
                    }
                } else if (found < nargs) sadd(&sub, args[found]);
            } else saddn(&sub, s, n);
            continue;
        }
        if (isdigit((unsigned char)*b)) {
            while (isalnum((unsigned char)*b) || *b == '_' || *b == '.') sadc(&sub, *b++);
            continue;
        }
        sadc(&sub, *b++);
    }
    Active a = { m, act };
    paste_tokens(&sub, &a);
    expand_text(sget(&sub), out, &a);
    free(sub.s);
}

static int try_eval(const char *s, Val *v);
static char **split_top_commas(const char *s, int *n);
static char *unquote_str(const char *tok);
static void quote_into(Str *o, const char *txt);
static char *expand_dup(const char *in);

static int pp_function(const char *name, const char *inner, Str *out)
{
    static const char *names[] = { "abs", "cond", "count", "eval", "num", "sel", "str", "strcat", "strlen", "substr", "tok", NULL };
    int known = 0;
    for (int i = 0; names[i]; i++) if (!strcmp(names[i], name)) known = 1;
    if (!known) return 0;
    char *ex = expand_dup(inner);
    int n;
    char **a = split_top_commas(ex, &n);
    Val v;
    int handled = 1;
    if (!strcmp(name, "abs")) {
        if (n == 1 && try_eval(a[0], &v) && val_isconst(&v)) sfmt(out, "%lld", (long long)(v.n < 0 ? -v.n : v.n));
        else { err("non-constant expression in `%%abs'"); }
    } else if (!strcmp(name, "eval")) {
        if (n == 1 && try_eval(a[0], &v) && val_isconst(&v)) sfmt(out, "%lld", (long long)v.n);
        else err("non-constant expression in `%%eval'");
    } else if (!strcmp(name, "cond")) {
        if (n >= 2 && try_eval(a[0], &v) && val_isconst(&v)) {
            if (v.n) sadd(out, a[1]);
            else if (n >= 3) sadd(out, a[2]);
        } else if (n < 2) handled = 0;
        else err("non-constant expression in `%%cond'");
    } else if (!strcmp(name, "count")) {
        sfmt(out, "%d", n);
    } else if (!strcmp(name, "sel")) {
        if (n >= 1 && try_eval(a[0], &v) && val_isconst(&v)) {
            if (v.n >= 1 && v.n < n) sadd(out, a[v.n]);
            else warn("%%sel(%lld) is not a valid selector", (long long)v.n);
        } else err("non-constant expression in `%%sel'");
    } else if (!strcmp(name, "num")) {
        if (n >= 1 && try_eval(a[0], &v) && val_isconst(&v)) {
            int width = 0, base = 10;
            Val w;
            if (n >= 2 && try_eval(a[1], &w) && val_isconst(&w)) width = (int)w.n;
            if (n >= 3 && try_eval(a[2], &w) && val_isconst(&w)) base = (int)w.n;
            if (base < 2 || base > 16) base = 10;
            u64 u = (u64)v.n;
            if (base != 10 && v.n < 0 && width > 0 && width <= 8) u &= (width * 4 >= 32 && base == 16) ? 0xFFFFFFFFull : u;
            if (base == 16 && v.n < 0 && width == 8) u &= 0xFFFFFFFFull;
            char buf[80];
            int k = 0;
            if (!u) buf[k++] = '0';
            while (u) { buf[k++] = "0123456789abcdef"[u % (u64)base]; u /= (u64)base; }
            Str t = {0};
            for (int i = k; i < width; i++) sadc(&t, '0');
            while (k) sadc(&t, buf[--k]);
            sadc(out, '\'');
            sadd(out, sget(&t));
            sadc(out, '\'');
            free(t.s);
        } else err("non-constant expression in `%%num'");
    } else if (!strcmp(name, "str")) {
        Str t = {0};
        for (int i = 0; i < n; i++) {
            if (i) sadd(&t, ", ");
            sadd(&t, a[i]);
        }
        const char *src = sget(&t);
        Str norm = {0};
        int sp = 0;
        for (const char *q = src; *q; q++) {
            if (*q == ' ' || *q == '\t') { sp = 1; continue; }
            if (sp && norm.n) sadc(&norm, ' ');
            sp = 0;
            sadc(&norm, *q);
        }
        if (!strchr(sget(&norm), '\'')) { sadc(out, '\''); sadd(out, sget(&norm)); sadc(out, '\''); }
        else quote_into(out, sget(&norm));
        free(t.s);
        free(norm.s);
    } else if (!strcmp(name, "strcat")) {
        Str t = {0};
        for (int i = 0; i < n; i++) {
            char *u = unquote_str(a[i]);
            sadd(&t, u);
            free(u);
        }
        sadc(out, '\'');
        sadd(out, sget(&t));
        sadc(out, '\'');
        free(t.s);
    } else if (!strcmp(name, "strlen")) {
        char *u = unquote_str(a[0]);
        sfmt(out, "%zu", strlen(u));
        free(u);
    } else if (!strcmp(name, "substr")) {
        char *u = unquote_str(a[0]);
        i64 len = (i64)strlen(u);
        i64 st = 1, cnt = -1;
        if (n >= 2 && try_eval(a[1], &v) && val_isconst(&v)) st = v.n;
        if (n >= 3 && try_eval(a[2], &v) && val_isconst(&v)) cnt = v.n;
        if (st < 1 || st > len + 1) { st = 1; cnt = -1; }
        if (cnt < 0 || st - 1 + cnt > len) cnt = len - (st - 1);
        Str t = {0};
        saddn(&t, u + st - 1, (size_t)cnt);
        sadc(out, '\'');
        sadd(out, sget(&t));
        sadc(out, '\'');
        free(t.s);
        free(u);
    } else if (!strcmp(name, "tok")) {
        char k;
        const char *q = a[0];
        if (qlen_at(q, &k) && *skip_quoted(q) == 0) {
            char *u = unquote_str(a[0]);
            sadd(out, u);
            free(u);
        } else sadd(out, a[0]);
    }
    for (int i = 0; i < n; i++) free(a[i]);
    free(a);
    free(ex);
    return handled;
}

static void expand_text(const char *in, Str *out, Active *act)
{
    Str res = {0};
    const char *p = in;
    while (*p) {
        char k;
        if (qlen_at(p, &k)) {
            const char *e = skip_quoted(p);
            saddn(&res, p, (size_t)(e - p));
            p = e;
            continue;
        }
        if (p[0] == '%' && isalpha((unsigned char)p[1])) {
            const char *q = p + 1;
            while (isalpha((unsigned char)*q)) q++;
            if (*q == '(' && q - p - 1 < 12) {
                char fn[16];
                memcpy(fn, p + 1, (size_t)(q - p - 1));
                fn[q - p - 1] = 0;
                const char *r = q + 1;
                int d = 1;
                while (*r && d) {
                    char kk;
                    if (qlen_at(r, &kk)) { r = skip_quoted(r); continue; }
                    if (*r == '(') d++;
                    else if (*r == ')') d--;
                    r++;
                }
                if (d == 0) {
                    char *inner = xstrndup(q + 1, (size_t)(r - q - 2));
                    Str t = {0};
                    int h = pp_function(fn, inner, &t);
                    free(inner);
                    if (h) {
                        sadd(&res, sget(&t));
                        free(t.s);
                        p = r;
                        continue;
                    }
                    free(t.s);
                }
            }
        }
        if (p[0] == '%' && p[1] == '[') {
            const char *q = p + 2;
            int d = 1;
            while (*q && d) { if (*q == '[') d++; else if (*q == ']') d--; q++; }
            char *inner = xstrndup(p + 2, (size_t)(q - p - 3));
            Str t = {0};
            expand_text(inner, &t, act);
            sadd(&res, sget(&t));
            free(t.s);
            free(inner);
            p = q;
            continue;
        }
        if (p[0] == '%' && p[1] == '!') {
            const char *q = p + 2;
            while (ident_char((unsigned char)*q)) q++;
            char *nm = xstrndup(p + 2, (size_t)(q - p - 2));
            const char *v = getenv(nm);
            sadc(&res, '"');
            if (v) sadd(&res, v);
            sadc(&res, '"');
            free(nm);
            p = q;
            continue;
        }
        if (p[0] == '%' && p[1] == '$') {
            const char *q = p + 2;
            while (*q == '$') q++;
            while (ident_char((unsigned char)*q)) q++;
            char *nm = xstrndup(p, (size_t)(q - p));
            SMac *m = smac_lookup(nm, 0, 0);
            if (m && !is_active(act, m)) {
                expand_smac_body(m, NULL, 0, &res, act);
                p = q;
                free(nm);
                continue;
            }
            const char *q2 = q;
            char **args;
            int na;
            int r = parse_call_args(&q2, &args, &na);
            if (r == 1) {
                m = smac_lookup(nm, na, 1);
                if (m && !is_active(act, m)) {
                    expand_smac_body(m, args, na, &res, act);
                    for (int i = 0; i < na; i++) free(args[i]);
                    free(args);
                    p = q2;
                    free(nm);
                    continue;
                }
                for (int i = 0; i < na; i++) free(args[i]);
                free(args);
            }
            {
                const char *r = p + 2;
                int up = 0;
                while (*r == '$') { up++; r++; }
                int idx = P.nctx - 1 - up;
                if (idx < 0 || r == q) saddn(&res, p, (size_t)(q - p));
                else {
                    char lb[32];
                    snprintf(lb, sizeof lb, "..@%d.", P.ctx[idx].id);
                    sadd(&res, lb);
                    saddn(&res, r, (size_t)(q - r));
                }
            }
            p = q;
            free(nm);
            continue;
        }
        if (isdigit((unsigned char)*p)) {
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') sadc(&res, *p++);
            continue;
        }
        if (*p == '$' && p[1] != '$' && ident_start((unsigned char)p[1])) {
            sadc(&res, *p++);
            while (ident_char((unsigned char)*p)) sadc(&res, *p++);
            continue;
        }
        if (ident_start((unsigned char)*p)) {
            const char *s = p;
            while (ident_char((unsigned char)*p)) p++;
            size_t n = (size_t)(p - s);
            char name[256];
            if (n >= sizeof name) { saddn(&res, s, n); continue; }
            memcpy(name, s, n);
            name[n] = 0;
            if (!strcmp(name, "__FILE__")) { sfmt(&res, "\"%s\"", P.curfile ? P.curfile : ""); continue; }
            if (!strcmp(name, "__LINE__")) { sfmt(&res, "%d", P.curline); continue; }
            if (!strcmp(name, "__BITS__")) { sfmt(&res, "%d", P.bits); continue; }
            if (!strcmp(name, "__SECT__")) { sadd(&res, P.sect ? P.sect : "[section .text]"); continue; }
            SMac *m = smac_lookup(name, 0, 0);
            if (m && !is_active(act, m)) {
                expand_smac_body(m, NULL, 0, &res, act);
                continue;
            }
            const char *q = p;
            char **args;
            int na;
            int r = parse_call_args(&q, &args, &na);
            if (r == 1) {
                m = smac_lookup(name, na, 1);
                if (!m && na == 0) m = smac_lookup(name, 1, 1);
                if (m && !is_active(act, m)) {
                    expand_smac_body(m, args, na, &res, act);
                    for (int i = 0; i < na; i++) free(args[i]);
                    free(args);
                    p = q;
                    continue;
                }
                for (int i = 0; i < na; i++) free(args[i]);
                free(args);
            }
            saddn(&res, s, n);
            continue;
        }
        sadc(&res, *p++);
    }
    paste_tokens(&res, act);
    sadd(out, sget(&res));
    free(res.s);
}

static char *expand_dup(const char *in)
{
    Str o = {0};
    expand_text(in, &o, NULL);
    return sget(&o);
}

static int src_next(Src *s, SLine *out);

static void out_line(const char *text, const char *file, int line)
{
    lv_add(P.out, text, file, line);
    const char *t = text;
    while (*t == ' ' || *t == '\t') t++;
    if (!strncasecmp(t, "bits", 4) && isspace((unsigned char)t[4])) {
        int b = atoi(t + 5);
        if (b == 16 || b == 32 || b == 64) P.bits = b;
    } else if (!strncasecmp(t, "[bits", 5)) {
        int b = atoi(t + 6);
        if (b == 16 || b == 32 || b == 64) P.bits = b;
    } else if (!strncasecmp(t, "use16", 5)) P.bits = 16;
    else if (!strncasecmp(t, "use32", 5)) P.bits = 32;
    if (!strncasecmp(t, "section", 7) || !strncasecmp(t, "segment", 7) || !strncasecmp(t, "absolute", 8)) {
        Str s = {0};
        sfmt(&s, "[%s]", t);
        free(P.sect);
        P.sect = xstrdup(sget(&s));
        free(s.s);
    } else if (!strncasecmp(t, "[section", 8) || !strncasecmp(t, "[segment", 8) || !strncasecmp(t, "[absolute", 9)) {
        free(P.sect);
        P.sect = xstrdup(t);
    }
}

static int cur_active(void)
{
    return P.ncond == 0 ? 1 : P.conds[P.ncond - 1].active;
}

static void process_src(Src *s);

static MMac *mmac_find(const char *name, int nargs)
{
    for (int ci = 0; ci < 2; ci++) {
        HT *h = ci ? &P.mmacs_ci : &P.mmacs;
        char *k = ci ? lower(name) : xstrdup(name);
        MMac *m = ht_get(h, k);
        free(k);
        for (; m; m = m->next) {
            if (nargs >= m->minp && (m->maxp < 0 || nargs <= m->maxp || m->greedy)) return m;
        }
    }
    return NULL;
}

static int mmac_exists_name(const char *name)
{
    for (int ci = 0; ci < 2; ci++) {
        HT *h = ci ? &P.mmacs_ci : &P.mmacs;
        char *k = ci ? lower(name) : xstrdup(name);
        MMac *m = ht_get(h, k);
        free(k);
        if (m) return 1;
    }
    return 0;
}

static int split_margs(const char *s, char ***args, int *nargs, MMac *greedy_for)
{
    int cap = 8, n = 0;
    char **a = xmalloc(sizeof(char *) * (size_t)cap);
    Str cur = {0};
    const char *p = s;
    int have = 0;
    while (*p) {
        if (greedy_for && n == greedy_for->maxp - 1 && greedy_for->greedy) {
            const char *rest = p;
            while (*rest == ' ' || *rest == '\t') rest++;
            if (n == cap) { cap *= 2; a = xrealloc(a, sizeof(char *) * (size_t)cap); }
            a[n++] = xstrdup(trim(xstrdup(rest)));
            *args = a;
            *nargs = n;
            free(cur.s);
            return 1;
        }
        char k;
        if (qlen_at(p, &k)) {
            const char *e = skip_quoted(p);
            saddn(&cur, p, (size_t)(e - p));
            p = e;
            have = 1;
            continue;
        }
        if (*p == '{') {
            int d = 1;
            p++;
            have = 1;
            while (*p && d) {
                if (*p == '{') d++;
                else if (*p == '}') { d--; if (!d) break; }
                sadc(&cur, *p);
                p++;
            }
            if (*p == '}') p++;
            continue;
        }
        if (*p == ',') {
            if (n == cap) { cap *= 2; a = xrealloc(a, sizeof(char *) * (size_t)cap); }
            a[n++] = xstrdup(trim(sget(&cur)));
            sclear(&cur);
            p++;
            have = 1;
            continue;
        }
        sadc(&cur, *p++);
        have = 1;
    }
    char *t = trim(sget(&cur));
    if (n > 0 || *t || have) {
        if (*t || n > 0) {
            if (n == cap) { cap *= 2; a = xrealloc(a, sizeof(char *) * (size_t)cap); }
            a[n++] = xstrdup(t);
        }
    }
    free(cur.s);
    *args = a;
    *nargs = n;
    return 1;
}

static const char *cc_names[] = { "o", "no", "b", "c", "nae", "ae", "nb", "nc", "e", "z", "ne", "nz", "be", "na", "a", "nbe", "s", "ns", "p", "pe", "np", "po", "l", "nge", "ge", "nl", "le", "ng", "g", "nle", NULL };
static const char *cc_inv[] = { "no", "o", "ae", "nc", "ae", "b", "b", "c", "ne", "nz", "e", "z", "a", "a", "be", "be", "ns", "s", "s", "np", "pe", "pe", "p", "ge", "l", "l", "g", "g", "le", "le" };

static const char *cc_invert(const char *cc)
{
    for (int i = 0; cc_names[i]; i++) if (!strcasecmp(cc_names[i], cc)) return cc_inv[i];
    return NULL;
}

static void subst_inv(const char *in, Inv *inv, Str *out)
{
    const char *p = in;
    while (*p) {
        char k;
        if (qlen_at(p, &k)) {
            const char *e = skip_quoted(p);
            saddn(out, p, (size_t)(e - p));
            p = e;
            continue;
        }
        if (*p != '%') { sadc(out, *p++); continue; }
        if (p[1] == '%' && p[2] == '%') { sadc(out, '%'); p++; continue; }
        if (p[1] == '%' && (p[2] == '?')) {
            sadd(out, inv->name);
            p += 3;
            continue;
        }
        if (p[1] == '%' && ident_char((unsigned char)p[2])) {
            const char *q = p + 2;
            while (ident_char((unsigned char)*q)) q++;
            sfmt(out, "..@%d.", inv->id);
            saddn(out, p + 2, (size_t)(q - p - 2));
            p = q;
            continue;
        }
        if (p[1] == '?') {
            sadd(out, inv->name);
            p += (p[2] == '?') ? 3 : 2;
            continue;
        }
        if (p[1] == '0' && !isdigit((unsigned char)p[2])) {
            sfmt(out, "%d", inv->nargs);
            p += 2;
            continue;
        }
        if (isdigit((unsigned char)p[1])) {
            int n = 0;
            const char *q = p + 1;
            while (isdigit((unsigned char)*q)) { n = n * 10 + (*q - '0'); q++; }
            if (n >= 1 && n <= inv->nargs) sadd(out, inv->args[n - 1]);
            p = q;
            continue;
        }
        if ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char)p[2])) {
            int n = 0;
            const char *q = p + 2;
            while (isdigit((unsigned char)*q)) { n = n * 10 + (*q - '0'); q++; }
            if (n >= 1 && n <= inv->nargs) {
                const char *cc = inv->args[n - 1];
                if (p[1] == '+') sadd(out, cc);
                else {
                    const char *iv = cc_invert(cc);
                    if (iv) sadd(out, iv); else { err("`%%-%d' used with an invalid condition code", n); sadd(out, cc); }
                }
            }
            p = q;
            continue;
        }
        if (p[1] == '{') {
            const char *q = p + 2;
            int a = 0, b = 0, hasrange = 0, neg1 = 0, neg2 = 0, hasb = 0;
            if (*q == '-') { neg1 = 1; q++; }
            while (isdigit((unsigned char)*q)) { a = a * 10 + (*q - '0'); q++; }
            if (*q == ':') {
                hasrange = 1;
                q++;
                if (*q == '-') { neg2 = 1; q++; }
                while (isdigit((unsigned char)*q)) { b = b * 10 + (*q - '0'); hasb = 1; q++; }
            }
            if (*q == '}') {
                if (neg1) a = inv->nargs - a + 1;
                if (!hasrange) {
                    if (a >= 1 && a <= inv->nargs) sadd(out, inv->args[a - 1]);
                } else {
                    if (!hasb) b = inv->nargs; else if (neg2) b = inv->nargs - b + 1;
                    int first = 1;
                    for (int i = a; i <= b && i <= inv->nargs; i++) {
                        if (i < 1) continue;
                        if (!first) sadd(out, ", ");
                        sadd(out, inv->args[i - 1]);
                        first = 0;
                    }
                }
                p = q + 1;
                continue;
            }
        }
        sadc(out, *p++);
    }
}

static int src_next(Src *s, SLine *out)
{
    if (s->i >= s->lv->n) return 0;
    SLine *l = &s->lv->v[s->i++];
    if (s->inv) {
        Str t = {0};
        subst_inv(l->text, s->inv, &t);
        out->text = sget(&t);
    } else out->text = xstrdup(l->text);
    out->file = l->file;
    out->line = l->line;
    return 1;
}

static void invoke_mmac(MMac *m, char **args, int nargs, const char *name, const char *file, int line)
{
    if (P.depth > 200) { err("macro recursion too deep"); return; }
    Inv inv;
    memset(&inv, 0, sizeof inv);
    int total = nargs;
    if (m->maxp >= 0 && total < m->maxp) total = m->maxp;
    inv.args = xcalloc((size_t)(total + 1), sizeof(char *));
    for (int i = 0; i < total; i++) {
        if (i < nargs) inv.args[i] = xstrdup(args[i]);
        else {
            int di = i - m->minp;
            inv.args[i] = xstrdup(di >= 0 && di < m->ndef ? m->defaults[di] : "");
        }
    }
    inv.nargs = total;
    inv.id = ++P.maccounter;
    inv.name = xstrdup(name);
    Src s = { &m->body, 0, &inv };
    Inv *saved = P.curinv;
    const char *sf = P.curfile;
    int sl = P.curline;
    P.curinv = &inv;
    P.depth++;
    int nc0 = P.ncond;
    process_src(&s);
    if (inv.exitmac) P.ncond = nc0;
    P.depth--;
    P.curinv = saved;
    P.curfile = sf;
    P.curline = sl;
    for (int i = 0; i < total; i++) free(inv.args[i]);
    free(inv.args);
    free(inv.name);
    (void)file;
    (void)line;
}

static int try_mmacro(char *line, const char *file, int line_no)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    const char *s = p;
    if (!ident_start((unsigned char)*p) && *p != '$') return 0;
    while (ident_char((unsigned char)*p)) p++;
    char name[256];
    size_t n = (size_t)(p - s);
    if (n == 0 || n >= sizeof name) return 0;
    memcpy(name, s, n);
    name[n] = 0;
    const char *after = p;
    if (*p == ':' && p[1] != ':') {
        after = p + 1;
        const char *q = after;
        while (*q == ' ' || *q == '\t') q++;
        if (!*q) return 0;
        const char *s2 = q;
        if (!ident_start((unsigned char)*q)) return 0;
        while (ident_char((unsigned char)*q)) q++;
        char n2[256];
        size_t l2 = (size_t)(q - s2);
        if (l2 >= sizeof n2) return 0;
        memcpy(n2, s2, l2);
        n2[l2] = 0;
        if (!mmac_exists_name(n2)) return 0;
        Str lab = {0};
        saddn(&lab, s, (size_t)(p - s + 1));
        out_line(sget(&lab), file, line_no);
        free(lab.s);
        strcpy(name, n2);
        p = q;
    } else if (!mmac_exists_name(name)) {
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (!ident_start((unsigned char)*q)) return 0;
        const char *s2 = q;
        while (ident_char((unsigned char)*q)) q++;
        char n2[256];
        size_t l2 = (size_t)(q - s2);
        if (l2 >= sizeof n2) return 0;
        memcpy(n2, s2, l2);
        n2[l2] = 0;
        if (!mmac_exists_name(n2)) return 0;
        Str lab = {0};
        saddn(&lab, s, (size_t)(p - s));
        sadc(&lab, ':');
        out_line(sget(&lab), file, line_no);
        free(lab.s);
        strcpy(name, n2);
        p = q;
    }
    (void)after;
    char **args;
    int na;
    split_margs(p, &args, &na, NULL);
    MMac *m = mmac_find(name, na);
    if (m && m->greedy) {
        for (int i = 0; i < na; i++) free(args[i]);
        free(args);
        split_margs(p, &args, &na, m);
        m = mmac_find(name, na);
    }
    if (!m) {
        for (int i = 0; i < na; i++) free(args[i]);
        free(args);
        return 0;
    }
    invoke_mmac(m, args, na, name, file, line_no);
    for (int i = 0; i < na; i++) free(args[i]);
    free(args);
    return 1;
}


static const char *next_tok(const char *p, char *buf, size_t sz)
{
    while (*p == ' ' || *p == '\t') p++;
    size_t n = 0;
    char k;
    if (qlen_at(p, &k)) {
        const char *e = skip_quoted(p);
        while (p < e && n < sz - 1) buf[n++] = *p++;
        buf[n] = 0;
        return p;
    }
    if (*p == '%' && (p[1] == '$' || p[1] == '%')) {
        buf[n++] = *p++;
        while ((*p == '$' || *p == '%') && n < sz - 1) buf[n++] = *p++;
        while (ident_char((unsigned char)*p) && n < sz - 1) buf[n++] = *p++;
        buf[n] = 0;
        return p;
    }
    if (ident_start((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '$') {
        while ((ident_char((unsigned char)*p) || (n == 0 && *p == '$')) && n < sz - 1) buf[n++] = *p++;
        buf[n] = 0;
        return p;
    }
    if (*p) buf[n++] = *p++;
    buf[n] = 0;
    return p;
}

static int try_eval(const char *s, Val *v)
{
    int e0 = g_errors;
    int q0 = g_quiet;
    g_quiet = 1;
    int ok = expr_eval_str(s, v, EF_PP);
    g_quiet = q0;
    int bad = g_errors != e0;
    g_errors = e0;
    return ok && !bad;
}

static int norm_eq(const char *a, const char *b, int ci)
{
    for (;;) {
        while (*a == ' ' || *a == '\t') a++;
        while (*b == ' ' || *b == '\t') b++;
        if (!*a || !*b) return !*a && !*b;
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ci) { ca = tolower(ca); cb = tolower(cb); }
        if (ca != cb) return 0;
        a++;
        b++;
    }
}

static char **split_top_commas(const char *s, int *n)
{
    char **r = NULL;
    int cnt = 0;
    Str cur = {0};
    int depth = 0;
    const char *p = s;
    while (*p) {
        char k;
        if (qlen_at(p, &k)) { const char *e = skip_quoted(p); saddn(&cur, p, (size_t)(e - p)); p = e; continue; }
        if (*p == '(' || *p == '[') depth++;
        if (*p == ')' || *p == ']') depth--;
        if (*p == ',' && depth == 0) {
            r = xrealloc(r, sizeof(char *) * (size_t)(cnt + 1));
            r[cnt++] = xstrdup(trim(sget(&cur)));
            sclear(&cur);
            p++;
            continue;
        }
        sadc(&cur, *p++);
    }
    r = xrealloc(r, sizeof(char *) * (size_t)(cnt + 1));
    r[cnt++] = xstrdup(trim(sget(&cur)));
    free(cur.s);
    *n = cnt;
    return r;
}

static int cond_eval(const char *kind, const char *arg)
{
    if (!strcmp(kind, "")) {
        char *e = expand_dup(arg);
        Val v;
        int ok = expr_eval_str(e, &v, EF_PP);
        free(e);
        if (!ok) return 0;
        return v.n != 0;
    }
    if (!strcmp(kind, "def")) {
        char tok[256];
        next_tok(arg, tok, sizeof tok);
        if (!tok[0]) { err("`%%ifdef' expects a macro name"); return 0; }
        if (!strcmp(tok, "__FILE__") || !strcmp(tok, "__LINE__") || !strcmp(tok, "__BITS__") || !strcmp(tok, "__SECT__")) return 1;
        return smac_lookup(tok, 0, 0) != NULL || (smac_lookup(tok, 0, 1) != NULL) || ht_get(&P.smacs, tok) != NULL;
    }
    if (!strcmp(kind, "macro")) {
        char tok[256];
        const char *p = next_tok(arg, tok, sizeof tok);
        while (*p == ' ') p++;
        if (!tok[0]) return 0;
        if (!*p) return mmac_exists_name(tok);
        int lo = atoi(p), hi = lo;
        const char *d = strchr(p, '-');
        if (d) hi = d[1] == '*' ? -1 : atoi(d + 1);
        for (int ci = 0; ci < 2; ci++) {
            HT *h = ci ? &P.mmacs_ci : &P.mmacs;
            char *k = ci ? lower(tok) : xstrdup(tok);
            MMac *m = ht_get(h, k);
            free(k);
            for (; m; m = m->next) if (m->minp == lo && (hi == m->maxp || (!d && m->maxp == lo))) return 1;
        }
        return 0;
    }
    if (!strcmp(kind, "ctx")) {
        if (P.nctx == 0) return 0;
        char tok[256];
        const char *p = arg;
        for (;;) {
            p = next_tok(p, tok, sizeof tok);
            if (!tok[0]) break;
            if (!strcasecmp(tok, P.ctx[P.nctx - 1].name)) return 1;
            while (*p == ' ' || *p == ',') p++;
        }
        return 0;
    }
    if (!strcmp(kind, "idn") || !strcmp(kind, "idni")) {
        int n;
        char *e = expand_dup(arg);
        char **parts = split_top_commas(e, &n);
        int r = 0;
        if (n == 2) r = norm_eq(parts[0], parts[1], kind[3] == 'i');
        else err("`%%ifidn' expects two comma-separated arguments");
        for (int i = 0; i < n; i++) free(parts[i]);
        free(parts);
        free(e);
        return r;
    }
    if (!strcmp(kind, "id") || !strcmp(kind, "str") || !strcmp(kind, "token") || !strcmp(kind, "num")) {
        char *e = expand_dup(arg);
        char *t = trim(e);
        int r = 0;
        char tok[512];
        const char *rest = next_tok(t, tok, sizeof tok);
        while (*rest == ' ' || *rest == '\t') rest++;
        char k;
        if (!strcmp(kind, "id")) r = tok[0] && !*rest && (ident_start((unsigned char)tok[0]) || tok[0] == '$');
        else if (!strcmp(kind, "str")) r = tok[0] && !*rest && qlen_at(tok, &k) != 0;
        else if (!strcmp(kind, "token")) r = tok[0] && !*rest;
        else {
            Val v;
            r = *t && try_eval(t, &v) && !v.unk;
        }
        free(e);
        return r;
    }
    if (!strcmp(kind, "empty")) {
        char *e = expand_dup(arg);
        int r = !*trim(e);
        free(e);
        return r;
    }
    if (!strcmp(kind, "env")) {
        char tok[256];
        const char *p = arg;
        while (*p == ' ') p++;
        if (*p == '%' && p[1] == '!') p += 2;
        next_tok(p, tok, sizeof tok);
        return getenv(tok) != NULL;
    }
    if (!strcmp(kind, "directive")) {
        char tok[256];
        next_tok(arg, tok, sizeof tok);
        static const char *d[] = { "bits", "section", "segment", "global", "extern", "common", "org", "absolute", "align", "cpu", "default", "import", "export", "static", "group", "uppercase", "struc", "endstruc", "istruc", "iend", "at", "times", "incbin", "equ", "float", "warning", "list", NULL };
        for (int i = 0; d[i]; i++) if (!strcasecmp(d[i], tok)) return 1;
        return 0;
    }
    if (!strcmp(kind, "using") || !strcmp(kind, "usable")) return 0;
    err("unknown conditional `%%if%s'", kind);
    return 0;
}

static int split_cond(const char *dir, const char **kind_out, int *neg, const char *prefix)
{
    size_t pl = strlen(prefix);
    const char *r = dir + pl;
    static const char *bases[] = { "", "def", "macro", "ctx", "idn", "idni", "id", "num", "str", "token", "empty", "env", "directive", "using", "usable", NULL };
    *neg = 0;
    for (int i = 0; bases[i]; i++) if (!strcasecmp(r, bases[i])) { *kind_out = bases[i]; return 1; }
    if (tolower((unsigned char)r[0]) == 'n') {
        for (int i = 0; bases[i]; i++) if (!strcasecmp(r + 1, bases[i])) { *kind_out = bases[i]; *neg = 1; return 1; }
    }
    return 0;
}

static int src_next_raw(Src *s, SLine *out)
{
    if (s->i >= s->lv->n) return 0;
    SLine *l = &s->lv->v[s->i++];
    out->text = xstrdup(l->text);
    out->file = l->file;
    out->line = l->line;
    return 1;
}

static void collect_until(Src *s, const char *open1, const char *open2, const char *close1, const char *close2, LineVec *body, const char *file, int line, const char *what, int raw)
{
    int depth = 1;
    SLine l;
    while (raw ? src_next_raw(s, &l) : src_next(s, &l)) {
        char *t = xstrdup(l.text);
        strip_comment(t);
        char *tt = trim(t);
        char w[64];
        int is_dir = 0;
        if (*tt == '%' && isalpha((unsigned char)tt[1])) {
            const char *q = tt + 1;
            size_t n = 0;
            while (isalnum((unsigned char)*q) && n < sizeof w - 1) w[n++] = *q++;
            w[n] = 0;
            is_dir = 1;
        }
        if (is_dir) {
            if (!strcasecmp(w, open1) || (open2 && !strcasecmp(w, open2))) depth++;
            else if (!strcasecmp(w, close1) || (close2 && !strcasecmp(w, close2))) {
                depth--;
                if (depth == 0) { free(t); free(l.text); return; }
            }
        }
        lv_add(body, l.text, l.file, l.line);
        free(t);
        free(l.text);
    }
    g_pos.file = file;
    g_pos.line = line;
    err("`%%%s' without matching end", what);
}

static void parse_macro_header(const char *rest, int casei, MMac **out)
{
    char name[256];
    const char *p = next_tok(rest, name, sizeof name);
    if (!name[0]) { err("`%%macro' requires a name"); *out = NULL; return; }
    while (*p == ' ' || *p == '\t') p++;
    MMac *m = xcalloc(1, sizeof *m);
    m->name = xstrdup(name);
    m->casei = casei;
    m->minp = 0;
    m->maxp = 0;
    const char *q = p;
    if (isdigit((unsigned char)*q)) {
        m->minp = atoi(q);
        while (isdigit((unsigned char)*q)) q++;
        m->maxp = m->minp;
        if (*q == '-') {
            q++;
            if (*q == '*') { m->maxp = -1; q++; }
            else { m->maxp = atoi(q); while (isdigit((unsigned char)*q)) q++; }
        }
        if (*q == '+') { m->greedy = 1; q++; }
        else if (*q == '-' && q[1] == '*') {}
    } else if (*q && *q != '.') {
        err("`%%macro' requires a parameter count");
    }
    p = q;
    while (*p == ' ' || *p == '\t') p++;
    if (!strncasecmp(p, ".nolist", 7)) { p += 7; while (*p == ' ') p++; }
    if (*p) {
        int n;
        char **parts = split_top_commas(p, &n);
        m->defaults = parts;
        m->ndef = n;
        for (int i = 0; i < n; i++) {
            char *t = parts[i];
            size_t l = strlen(t);
            if (l >= 2 && t[0] == '{' && t[l - 1] == '}') { memmove(t, t + 1, l - 2); t[l - 2] = 0; }
        }
    }
    if (m->greedy && m->maxp < 0) m->maxp = m->minp;
    *out = m;
}

static void add_mmac(MMac *m)
{
    HT *h = m->casei ? &P.mmacs_ci : &P.mmacs;
    char *k = m->casei ? lower(m->name) : xstrdup(m->name);
    MMac *old = ht_get(h, k);
    MMac *head = NULL, **tail = &head;
    for (MMac *o = old; o; ) {
        MMac *n = o->next;
        if (o->minp == m->minp && o->maxp == m->maxp) { free(o->name); }
        else { o->next = NULL; *tail = o; tail = &o->next; }
        o = n;
    }
    m->next = head;
    ht_put(h, k, m);
    free(k);
}

static char *find_include(const char *name, const char *from)
{
    if (file_exists(name) && (name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':'))) return xstrdup(name);
    char *dir = path_dir(from ? from : "");
    char *c = path_join(dir, name);
    free(dir);
    if (file_exists(c)) return c;
    free(c);
    for (int i = 0; i < P.opts->nincdirs; i++) {
        c = path_join(P.opts->incdirs[i], name);
        if (file_exists(c)) return c;
        free(c);
    }
    if (file_exists(name)) return xstrdup(name);
    return NULL;
}

static void split_text_lines(char *text, const char *file, LineVec *lv)
{
    int line = 1;
    char *p = text;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    Str cur = {0};
    int startline = 1;
    while (*p) {
        if (*p == '\r') { p++; continue; }
        if (*p == '\\' && (p[1] == '\n' || (p[1] == '\r' && p[2] == '\n'))) {
            p += (p[1] == '\n') ? 2 : 3;
            line++;
            continue;
        }
        if (*p == '\n') {
            lv_add(lv, sget(&cur), file, startline);
            sclear(&cur);
            p++;
            line++;
            startline = line;
            continue;
        }
        sadc(&cur, *p++);
    }
    if (cur.n) lv_add(lv, sget(&cur), file, startline);
    free(cur.s);
}

static void do_include(const char *name, const char *from)
{
    if (P.depth > 64) { err("include nesting too deep"); return; }
    char *path = find_include(name, from);
    if (!path) { err("unable to open include file `%s'", name); return; }
    size_t len;
    char *text = read_file(path, &len);
    if (!text) { err("unable to read include file `%s'", path); free(path); return; }
    const char *fn = intern(path);
    LineVec lv = {0};
    split_text_lines(text, fn, &lv);
    free(text);
    Src s = { &lv, 0, NULL };
    const char *sf = P.curfile;
    int sl = P.curline;
    Inv *sinv = P.curinv;
    P.curinv = NULL;
    P.depth++;
    process_src(&s);
    P.depth--;
    P.curinv = sinv;
    P.curfile = sf;
    P.curline = sl;
    free(path);
}

static char *unquote_str(const char *tok)
{
    Str s = {0};
    const char *p = tok;
    char k;
    if (!qlen_at(p, &k)) return xstrdup(tok);
    parse_charconst(&p, &s);
    return sget(&s);
}

static void quote_into(Str *o, const char *txt)
{
    if (!strchr(txt, '"')) { sadc(o, '"'); sadd(o, txt); sadc(o, '"'); }
    else if (!strchr(txt, '\'')) { sadc(o, '\''); sadd(o, txt); sadc(o, '\''); }
    else {
        sadc(o, '`');
        for (const char *p = txt; *p; p++) { if (*p == '`' || *p == '\\') sadc(o, '\\'); sadc(o, *p); }
        sadc(o, '`');
    }
}

static i64 type_size(const char *t)
{
    if (!strcasecmp(t, "byte")) return 1;
    if (!strcasecmp(t, "word")) return 2;
    if (!strcasecmp(t, "dword")) return 4;
    if (!strcasecmp(t, "qword")) return 8;
    if (!strcasecmp(t, "tword")) return 10;
    if (!strcasecmp(t, "oword")) return 16;
    if (!strcasecmp(t, "yword")) return 32;
    return 0;
}

static void do_arg_local(const char *rest, int islocal)
{
    if (P.nctx == 0) { err("`%%%s' used outside a context", islocal ? "local" : "arg"); return; }
    Ctx *c = &P.ctx[P.nctx - 1];
    if (!c->stk) { c->stk = 2; c->argoff = 8; c->locoff = 0; }
    int n;
    char **parts = split_top_commas(rest, &n);
    const char *basereg = c->stk == 2 ? "ebp" : "bp";
    int width = c->stk == 2 ? 4 : 2;
    for (int i = 0; i < n; i++) {
        char *colon = strchr(parts[i], ':');
        char name[256];
        i64 sz = width;
        if (colon) {
            *colon = 0;
            sz = type_size(trim(colon + 1));
            if (!sz) { err("unknown type `%s'", trim(colon + 1)); sz = width; }
        }
        snprintf(name, sizeof name, "%s", trim(parts[i]));
        i64 slot = (sz + width - 1) / width * width;
        char body[64];
        if (islocal) {
            c->locoff += (int)slot;
            snprintf(body, sizeof body, "%s-%d", basereg, c->locoff);
            Str ls = {0};
            sfmt(&ls, "%d", c->locoff);
            smac_define("%$localsize", 0, 0, NULL, 0, 0, sget(&ls));
            free(ls.s);
        } else {
            snprintf(body, sizeof body, "%s+%d", basereg, c->argoff);
            c->argoff += (int)slot;
        }
        smac_define(name, 0, 0, NULL, 0, 0, body);
        free(parts[i]);
    }
    free(parts);
}

static void define_from_text(const char *rest, int casei, int expand_body)
{
    const char *p = rest;
    while (*p == ' ' || *p == '\t') p++;
    const char *s = p;
    if (p[0] == '%' && p[1] == '$') { p += 2; while (*p == '$') p++; }
    while (ident_char((unsigned char)*p) || (p == s && *p == '$')) p++;
    size_t n = (size_t)(p - s);
    if (!n) { err("`%%define' expects a macro name"); return; }
    char *name = xstrndup(s, n);
    if (*p == '(') {
        const char *q = p + 1;
        int cap = 4, np = 0, variadic = 0;
        char **params = xmalloc(sizeof(char *) * (size_t)cap);
        Str cur = {0};
        while (*q && *q != ')') {
            if (*q == ',') {
                if (np == cap) { cap *= 2; params = xrealloc(params, sizeof(char *) * (size_t)cap); }
                params[np++] = xstrdup(trim(sget(&cur)));
                sclear(&cur);
            } else sadc(&cur, *q);
            q++;
        }
        if (*q != ')') { err("missing `)' in macro parameter list"); free(name); free(cur.s); free(params); return; }
        char *last = trim(sget(&cur));
        if (*last || np > 0) {
            if (np == cap) { cap *= 2; params = xrealloc(params, sizeof(char *) * (size_t)cap); }
            params[np++] = xstrdup(last);
        }
        free(cur.s);
        if (np > 0 && !strcmp(params[np - 1], "...")) { variadic = 1; free(params[np - 1]); params[np - 1] = xstrdup("__VA_ARGS__"); }
        q++;
        while (*q == ' ' || *q == '\t') q++;
        char *body = xstrdup(q);
        trim(body);
        if (expand_body) { char *e = expand_dup(body); free(body); body = e; }
        smac_define(name, casei, 1, params, np, variadic, body);
        free(body);
    } else {
        while (*p == ' ' || *p == '\t') p++;
        char *body = xstrdup(p);
        trim(body);
        if (expand_body) { char *e = expand_dup(body); free(body); body = e; }
        smac_define(name, casei, 0, NULL, 0, 0, body);
        free(body);
    }
    free(name);
}

static void handle_directive(char *tt, Src *s, const char *file, int line)
{
    char dir[64];
    const char *p = tt + 1;
    size_t n = 0;
    while ((isalnum((unsigned char)*p) || *p == '_') && n < sizeof dir - 1) dir[n++] = *p++;
    dir[n] = 0;
    const char *rest = p;
    while (*rest == ' ' || *rest == '\t') rest++;
    int active = cur_active();

    if (!strncasecmp(dir, "if", 2) || !strncasecmp(dir, "elif", 4)) {
        int iselif = !strncasecmp(dir, "elif", 4);
        const char *kind;
        int neg;
        if (!split_cond(dir, &kind, &neg, iselif ? "elif" : "if")) {
            if (active || iselif) err("unknown directive `%%%s'", dir);
            if (!iselif) { Cond *c = &P.conds[P.ncond++]; c->parent = active; c->active = 0; c->taken = 1; c->hadelse = 0; }
            return;
        }
        if (!iselif) {
            if (P.ncond >= 255) { err("conditionals nested too deeply"); return; }
            Cond *c = &P.conds[P.ncond++];
            c->parent = active;
            c->hadelse = 0;
            if (!active) { c->active = 0; c->taken = 1; return; }
            int r = cond_eval(kind, rest);
            if (neg) r = !r;
            c->active = r;
            c->taken = r;
        } else {
            if (!P.ncond) { err("`%%elif' without `%%if'"); return; }
            Cond *c = &P.conds[P.ncond - 1];
            if (!c->parent) { c->active = 0; return; }
            if (c->taken) { c->active = 0; return; }
            int r = cond_eval(kind, rest);
            if (neg) r = !r;
            c->active = r;
            c->taken = r;
        }
        return;
    }
    if (!strcasecmp(dir, "else")) {
        if (!P.ncond) { err("`%%else' without `%%if'"); return; }
        Cond *c = &P.conds[P.ncond - 1];
        if (c->hadelse) err("`%%else' after `%%else'");
        c->hadelse = 1;
        if (!c->parent) { c->active = 0; return; }
        if (c->taken) c->active = 0;
        else { c->active = 1; c->taken = 1; }
        return;
    }
    if (!strcasecmp(dir, "endif")) {
        if (!P.ncond) { err("`%%endif' without `%%if'"); return; }
        P.ncond--;
        return;
    }
    if (!active) return;

    if (!strcasecmp(dir, "define") || !strcasecmp(dir, "idefine") || !strcasecmp(dir, "xdefine") || !strcasecmp(dir, "ixdefine")) {
        int ci = dir[0] == 'i' || dir[0] == 'I';
        int xd = tolower((unsigned char)dir[ci]) == 'x';
        define_from_text(rest, ci, xd);
    } else if (!strcasecmp(dir, "undef") || !strcasecmp(dir, "undefalias")) {
        char tok[256];
        next_tok(rest, tok, sizeof tok);
        smac_undef(tok);
    } else if (!strcasecmp(dir, "defalias")) {
        char nw[256], old[256];
        const char *q = next_tok(rest, nw, sizeof nw);
        next_tok(q, old, sizeof old);
        char *ko = ctx_key(old);
        for (int ci = 0; ko && ci < 2; ci++) {
            HT *h = ci ? &P.smacs_ci : &P.smacs;
            char *k = ci ? lower(ko) : xstrdup(ko);
            SMac *m = ht_get(h, k);
            free(k);
            for (; m; m = m->next) {
                char **pc = m->nparams ? xmalloc(sizeof(char *) * (size_t)m->nparams) : NULL;
                for (int i = 0; i < m->nparams; i++) pc[i] = xstrdup(m->params[i]);
                smac_define(nw, m->casei, m->isfunc, pc, m->nparams, m->variadic, m->body);
            }
        }
        free(ko);
    } else if (!strcasecmp(dir, "assign") || !strcasecmp(dir, "iassign")) {
        char name[256];
        const char *q = next_tok(rest, name, sizeof name);
        char *e = expand_dup(q);
        Val v;
        if (expr_eval_str(e, &v, EF_PP)) {
            char buf[32];
            snprintf(buf, sizeof buf, "%lld", (long long)v.n);
            smac_define(name, dir[0] == 'i', 0, NULL, 0, 0, buf);
        }
        free(e);
    } else if (!strcasecmp(dir, "defstr") || !strcasecmp(dir, "idefstr")) {
        char name[256];
        const char *q = next_tok(rest, name, sizeof name);
        char *e = expand_dup(q);
        Str o = {0};
        quote_into(&o, trim(e));
        smac_define(name, dir[0] == 'i', 0, NULL, 0, 0, sget(&o));
        free(o.s);
        free(e);
    } else if (!strcasecmp(dir, "deftok") || !strcasecmp(dir, "ideftok")) {
        char name[256];
        const char *q = next_tok(rest, name, sizeof name);
        char *e = expand_dup(q);
        char *u = unquote_str(trim(e));
        smac_define(name, dir[0] == 'i', 0, NULL, 0, 0, u);
        free(u);
        free(e);
    } else if (!strcasecmp(dir, "strlen")) {
        char name[256];
        const char *q = next_tok(rest, name, sizeof name);
        char *e = expand_dup(q);
        char *u = unquote_str(trim(e));
        char buf[32];
        snprintf(buf, sizeof buf, "%zu", strlen(u));
        smac_define(name, 0, 0, NULL, 0, 0, buf);
        free(u);
        free(e);
    } else if (!strcasecmp(dir, "substr")) {
        char name[256];
        const char *q = next_tok(rest, name, sizeof name);
        char *e = expand_dup(q);
        char tok[1024];
        const char *r = next_tok(e, tok, sizeof tok);
        char *u = unquote_str(tok);
        long len = (long)strlen(u);
        long st = 1, cnt = 1;
        while (*r == ' ' || *r == ',') r++;
        if (*r) {
            Val v;
            char arg1[256];
            const char *r2 = r;
            size_t ai = 0;
            while (*r2 && *r2 != ',' && ai < sizeof arg1 - 1) arg1[ai++] = *r2++;
            arg1[ai] = 0;
            if (try_eval(arg1, &v)) st = (long)v.n;
            if (st < 1) st = 1;
            if (*r2 == ',' && try_eval(r2 + 1, &v)) {
                cnt = (long)v.n;
                if (cnt < 0) cnt = len + cnt + 2 - st;
            }
        }
        if (st > len) cnt = 0;
        if (cnt > len - (st - 1)) cnt = len - (st - 1);
        if (cnt < 0) cnt = 0;
        if (st - 1 > len) st = len + 1;
        char *sub = xstrndup(u + st - 1, (size_t)cnt);
        Str o = {0};
        quote_into(&o, sub);
        smac_define(name, 0, 0, NULL, 0, 0, sget(&o));
        free(o.s);
        free(sub);
        free(u);
        free(e);
    } else if (!strcasecmp(dir, "strcat") || !strcasecmp(dir, "istrcat")) {
        char name[256];
        const char *q = next_tok(rest, name, sizeof name);
        char *e = expand_dup(q);
        int cnt;
        char **parts = split_top_commas(e, &cnt);
        Str cat = {0};
        for (int i = 0; i < cnt; i++) {
            char *u = unquote_str(parts[i]);
            sadd(&cat, u);
            free(u);
            free(parts[i]);
        }
        free(parts);
        Str o = {0};
        quote_into(&o, sget(&cat));
        smac_define(name, 0, 0, NULL, 0, 0, sget(&o));
        free(o.s);
        free(cat.s);
        free(e);
    } else if (!strcasecmp(dir, "macro") || !strcasecmp(dir, "imacro") || !strcasecmp(dir, "rmacro") || !strcasecmp(dir, "irmacro")) {
        int ci = dir[0] == 'i';
        MMac *m;
        parse_macro_header(rest, ci, &m);
        LineVec body = {0};
        collect_until(s, "macro", "imacro", "endmacro", "endm", &body, file, line, "macro", 0);
        if (m) {
            m->body = body;
            add_mmac(m);
        }
    } else if (!strcasecmp(dir, "unmacro") || !strcasecmp(dir, "iunmacro")) {
        char tok[256];
        const char *q = next_tok(rest, tok, sizeof tok);
        int lo = atoi(q), hi = lo;
        const char *d = strchr(q, '-');
        if (d) hi = d[1] == '*' ? -1 : atoi(d + 1);
        for (int ci = 0; ci < 2; ci++) {
            HT *h = ci ? &P.mmacs_ci : &P.mmacs;
            char *k = ci ? lower(tok) : xstrdup(tok);
            MMac *m = ht_get(h, k);
            MMac *head = NULL, **tail = &head;
            for (MMac *o = m; o; ) {
                MMac *nx = o->next;
                if (o->minp == lo && (o->maxp == hi || (!d && o->maxp == lo))) {}
                else { o->next = NULL; *tail = o; tail = &o->next; }
                o = nx;
            }
            if (head) ht_put(h, k, head); else ht_del(h, k);
            free(k);
        }
    } else if (!strcasecmp(dir, "rep")) {
        char *e = expand_dup(rest);
        Val v;
        i64 cnt = 0;
        if (*trim(e) == 0) err("`%%rep' expects a repeat count");
        else if (expr_eval_str(e, &v, EF_PP)) cnt = v.n;
        free(e);
        if (cnt < 0) { err("`%%rep' count must not be negative"); cnt = 0; }
        if (cnt > 1000000) { err("`%%rep' count exceeds the limit of 1000000"); cnt = 0; }
        LineVec body = {0};
        collect_until(s, "rep", NULL, "endrep", NULL, &body, file, line, "rep", 1);
        for (i64 i = 0; i < cnt && P.ok; i++) {
            Src rs = { &body, 0, NULL };
            Inv *si = P.curinv;
            rs.inv = s->inv;
            P.depth++;
            if (P.depth > 400) { err("%%rep nesting too deep"); P.depth--; break; }
            int nc0 = P.ncond;
            process_src(&rs);
            P.depth--;
            P.curinv = si;
            if (P.exitrep) { P.exitrep = 0; P.ncond = nc0; break; }
        }
    } else if (!strcasecmp(dir, "exitrep")) {
        P.exitrep = 1;
    } else if (!strcasecmp(dir, "exitmacro")) {
        if (s->inv) s->inv->exitmac = 1;
        else if (P.curinv) P.curinv->exitmac = 1;
        else err("`%%exitmacro' used outside a macro");
    } else if (!strcasecmp(dir, "rotate")) {
        Inv *inv = s->inv ? s->inv : P.curinv;
        if (!inv) { err("`%%rotate' used outside a macro"); return; }
        char *e = expand_dup(rest);
        Val v;
        i64 r = 1;
        if (*trim(e) && expr_eval_str(e, &v, EF_PP)) r = v.n;
        free(e);
        int na = inv->nargs;
        if (na > 0) {
            r %= na;
            if (r < 0) r += na;
            char **t = xmalloc(sizeof(char *) * (size_t)na);
            for (int i = 0; i < na; i++) t[i] = inv->args[(i + r) % na];
            memcpy(inv->args, t, sizeof(char *) * (size_t)na);
            free(t);
        }
    } else if (!strcasecmp(dir, "include")) {
        char *e = expand_dup(rest);
        char *t = trim(e);
        char *u = unquote_str(t);
        do_include(u, file);
        free(u);
        free(e);
    } else if (!strcasecmp(dir, "pathsearch")) {
        char name[256];
        const char *q = next_tok(rest, name, sizeof name);
        char *e = expand_dup(q);
        char *u = unquote_str(trim(e));
        char *f = find_include(u, file);
        Str o = {0};
        quote_into(&o, f ? f : u);
        smac_define(name, 0, 0, NULL, 0, 0, sget(&o));
        free(o.s);
        free(f);
        free(u);
        free(e);
    } else if (!strcasecmp(dir, "line")) {
        int n = atoi(rest);
        int orig = P.lo_on ? line - P.lo_delta : line;
        if (!P.lo_on) P.lo_src = file;
        P.lo_on = 1;
        P.lo_delta = n - orig;
        const char *q = rest;
        while (*q && !isspace((unsigned char)*q)) q++;
        while (*q == ' ' || *q == '\t') q++;
        if (*q) {
            char *fn = xstrdup(q);
            char *t = trim(fn);
            size_t L = strlen(t);
            if (L >= 2 && (t[0] == '"' || t[0] == '\'') && t[L - 1] == t[0]) { t[L - 1] = 0; t++; }
            P.lo_file = xstrdup(t);
            free(fn);
        }
    } else if (!strcasecmp(dir, "use")) {
        char *e = expand_dup(rest);
        char *nm = trim(e);
        char *sp = nm;
        while (*sp && !isspace((unsigned char)*sp) && *sp != ',') sp++;
        *sp = 0;
        static const char *known[] = { "smartalign", "altreg", "fp", "ifunc", "masm", NULL };
        int ok = 0;
        for (int i = 0; known[i]; i++) if (!strcasecmp(nm, known[i])) ok = 1;
        g_pos.file = file;
        g_pos.line = line;
        if (!ok) err("unable to find package `%s' (known: smartalign, altreg, fp, ifunc)", nm);
        else {
            char buf[80];
            snprintf(buf, sizeof buf, "__cly_use %s", nm);
            out_line(buf, file, line);
        }
        free(e);
    } else if (!strcasecmp(dir, "depend") || !strcasecmp(dir, "pragma") || !strcasecmp(dir, "clear")) {
        if (!strcasecmp(dir, "clear")) {
            ht_clear(&P.smacs);
            ht_clear(&P.smacs_ci);
        }
    } else if (!strcasecmp(dir, "error")) {
        char *e = expand_dup(rest);
        g_pos.file = file;
        g_pos.line = line;
        err("%%error: %s", trim(e));
        free(e);
    } else if (!strcasecmp(dir, "warning")) {
        char *e = expand_dup(rest);
        g_pos.file = file;
        g_pos.line = line;
        warn("%%warning: %s", trim(e));
        free(e);
    } else if (!strcasecmp(dir, "fatal")) {
        char *e = expand_dup(rest);
        g_pos.file = file;
        g_pos.line = line;
        fatal("%%fatal: %s", trim(e));
    } else if (!strcasecmp(dir, "comment")) {
        LineVec body = {0};
        collect_until(s, "comment", NULL, "endcomment", NULL, &body, file, line, "comment", 0);
    } else if (!strcasecmp(dir, "push")) {
        char tok[256];
        next_tok(rest, tok, sizeof tok);
        if (P.nctx >= 255) { err("context stack overflow"); return; }
        Ctx *c = &P.ctx[P.nctx++];
        memset(c, 0, sizeof *c);
        c->name = xstrdup(tok);
        c->id = ++P.ctxcounter;
    } else if (!strcasecmp(dir, "pop")) {
        if (!P.nctx) { err("`%%pop' in the absence of a context"); return; }
        P.nctx--;
    } else if (!strcasecmp(dir, "repl")) {
        if (!P.nctx) { err("`%%repl' in the absence of a context"); return; }
        char tok[256];
        next_tok(rest, tok, sizeof tok);
        free(P.ctx[P.nctx - 1].name);
        P.ctx[P.nctx - 1].name = xstrdup(tok);
    } else if (!strcasecmp(dir, "stacksize")) {
        if (!P.nctx) { err("`%%stacksize' used outside a context"); return; }
        char tok[64];
        next_tok(rest, tok, sizeof tok);
        Ctx *c = &P.ctx[P.nctx - 1];
        if (!strcasecmp(tok, "flat")) { c->stk = 2; c->argoff = 8; }
        else if (!strcasecmp(tok, "small")) { c->stk = 1; c->argoff = 4; }
        else if (!strcasecmp(tok, "large")) { c->stk = 3; c->argoff = 6; }
        else if (!strcasecmp(tok, "flat64")) { c->stk = 2; c->argoff = 16; }
        else err("unknown `%%stacksize' mode `%s'", tok);
        c->locoff = 0;
    } else if (!strcasecmp(dir, "arg")) {
        do_arg_local(rest, 0);
    } else if (!strcasecmp(dir, "local")) {
        do_arg_local(rest, 1);
    } else {
        err("unknown preprocessor directive `%%%s'", dir);
    }
}

static void process_src(Src *s)
{
    SLine l;
    while (P.ok && src_next(s, &l)) {
        if (P.exitrep) { free(l.text); return; }
        if (s->inv && s->inv->exitmac) { free(l.text); return; }
        if (P.lo_on && l.file == P.lo_src) {
            l.line += P.lo_delta;
            if (P.lo_file) l.file = P.lo_file;
        }
        P.curfile = l.file;
        P.curline = l.line;
        g_pos.file = l.file;
        g_pos.line = l.line;
        strip_comment(l.text);
        char *tt = trim(l.text);
        if (*tt == '%' && isalpha((unsigned char)tt[1])) {
            handle_directive(tt, s, l.file, l.line);
            free(l.text);
            continue;
        }
        if (!cur_active() || !*tt) { free(l.text); continue; }
        if (P.depth > 400) { err("preprocessor nesting too deep"); free(l.text); return; }
        if (try_mmacro(tt, l.file, l.line)) { free(l.text); continue; }
        Str o = {0};
        expand_text(tt, &o, NULL);
        char *r = trim(sget(&o));
        if (*r) {
            if (strncmp(r, "%[", 2) == 0) {}
            if (*r == '%' && isalpha((unsigned char)r[1])) {
                char *cp = xstrdup(r);
                handle_directive(cp, s, l.file, l.line);
                free(cp);
            } else if (try_mmacro(r, l.file, l.line)) {
            } else out_line(r, l.file, l.line);
        }
        free(o.s);
        free(l.text);
    }
}

static void define_builtins(PPOpts *o)
{
    define_simple("__NASM_MAJOR__", "2");
    define_simple("__NASM_MINOR__", "16");
    define_simple("__NASM_SUBMINOR__", "01");
    define_simple("__NASM_PATCHLEVEL__", "1");
    define_simple("__NASM_VERSION_ID__", "0x02100100");
    define_simple("__NASM_VER__", "\"2.16.01\"");
    define_simple("__CLY__", "1");
    define_simple("__CLY_MAJOR__", "1");
    define_simple("__CLY_MINOR__", "0");
    define_simple("__CLY_VERSION__", "\"" CLY_VERSION "\"");
    define_simple("__OUTPUT_FORMAT__", o->outfmt ? o->outfmt : "bin");
    define_simple("__PASS__", "3");
    define_simple("__?NASM_MAJOR?__", "2");
    define_simple("__?NASM_MINOR?__", "16");
    define_simple("__?OUTPUT_FORMAT?__", o->outfmt ? o->outfmt : "bin");
    define_simple("__?BITS?__", "32");
    define_simple("__?FILE?__", "\"\"");
    time_t t = time(NULL);
    struct tm *g = gmtime(&t);
    char b[64];
    if (g) {
        snprintf(b, sizeof b, "\"%04d-%02d-%02d\"", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday);
        define_simple("__DATE__", b);
        snprintf(b, sizeof b, "\"%02d:%02d:%02d\"", g->tm_hour, g->tm_min, g->tm_sec);
        define_simple("__TIME__", b);
        snprintf(b, sizeof b, "%04d%02d%02d", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday);
        define_simple("__DATE_NUM__", b);
        snprintf(b, sizeof b, "%02d%02d%02d", g->tm_hour, g->tm_min, g->tm_sec);
        define_simple("__TIME_NUM__", b);
        snprintf(b, sizeof b, "\"%04d-%02d-%02d %02d:%02d:%02d\"", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday, g->tm_hour, g->tm_min, g->tm_sec);
        define_simple("__UTC_DATE_TIME__", b);
        snprintf(b, sizeof b, "\"%04d-%02d-%02d\"", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday);
        define_simple("__UTC_DATE__", b);
        snprintf(b, sizeof b, "\"%02d:%02d:%02d\"", g->tm_hour, g->tm_min, g->tm_sec);
        define_simple("__UTC_TIME__", b);
    }
    snprintf(b, sizeof b, "%lld", (long long)t);
    define_simple("__POSIX_TIME__", b);
    for (int i = 0; i < o->ndefs; i++) {
        char *d = xstrdup(o->defs[i]);
        char *eq = strchr(d, '=');
        char *val = "1";
        if (eq) { *eq = 0; val = eq + 1; }
        smac_define(d, 0, 0, NULL, 0, 0, val);
        free(d);
    }
}

int pp_run(const char *path, PPOpts *o, LineVec *out)
{
    memset(&P, 0, sizeof P);
    P.opts = o;
    P.out = out;
    P.ok = 1;
    P.bits = o->bits ? o->bits : 32;
    define_builtins(o);
    for (int i = 0; i < o->nundefs; i++) smac_undef(o->undefs[i]);
    for (int i = 0; i < o->npreincs; i++) do_include(o->preincs[i], "");
    size_t len;
    char *text = read_file(path, &len);
    if (!text) { err("unable to open input file `%s'", path); return 0; }
    const char *fn = intern(path);
    LineVec lv = {0};
    split_text_lines(text, fn, &lv);
    free(text);
    Src s = { &lv, 0, NULL };
    process_src(&s);
    if (P.ncond) { g_pos.file = fn; g_pos.line = 0; err("expecting `%%endif'"); }
    return g_errors == 0;
}
