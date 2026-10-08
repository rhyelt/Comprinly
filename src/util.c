#include "cly.h"

Pos g_pos;
int g_errors, g_warnings, g_nowarn, g_quiet;

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "cly: out of memory\n"); exit(2); }
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p) { fprintf(stderr, "cly: out of memory\n"); exit(2); }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) { fprintf(stderr, "cly: out of memory\n"); exit(2); }
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s);
    char *r = xmalloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}

char *xstrndup(const char *s, size_t n)
{
    char *r = xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

static void sgrow(Str *s, size_t need)
{
    if (s->n + need + 1 > s->cap) {
        size_t c = s->cap ? s->cap : 64;
        while (c < s->n + need + 1) c *= 2;
        s->s = xrealloc(s->s, c);
        s->cap = c;
    }
}

void saddn(Str *s, const char *t, size_t n)
{
    sgrow(s, n);
    memcpy(s->s + s->n, t, n);
    s->n += n;
    s->s[s->n] = 0;
}

void sadd(Str *s, const char *t) { saddn(s, t, strlen(t)); }

void sadc(Str *s, int c)
{
    sgrow(s, 1);
    s->s[s->n++] = (char)c;
    s->s[s->n] = 0;
}

void sfmt(Str *s, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof buf) {
        char *big = xmalloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        saddn(s, big, (size_t)n);
        free(big);
    } else if (n > 0) {
        saddn(s, buf, (size_t)n);
    }
}

char *sget(Str *s)
{
    sgrow(s, 0);
    if (!s->s) { s->s = xmalloc(1); s->cap = 1; }
    s->s[s->n] = 0;
    return s->s;
}

void sclear(Str *s)
{
    s->n = 0;
    if (s->s) s->s[0] = 0;
}

static void bgrow(Bytes *b, size_t need)
{
    if (need > ((size_t)1 << 28) || b->n + need > ((size_t)1 << 28)) {
        fprintf(stderr, "cly: error: output is larger than 256 MB, check your times/resb/org values\n");
        exit(1);
    }
    if (b->n + need > b->cap) {
        size_t c = b->cap ? b->cap : 256;
        while (c < b->n + need) c *= 2;
        b->p = xrealloc(b->p, c);
        b->cap = c;
    }
}

void badd(Bytes *b, const void *d, size_t n)
{
    if (!n) return;
    bgrow(b, n);
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

void bad1(Bytes *b, u8 c)
{
    bgrow(b, 1);
    b->p[b->n++] = c;
}

void badle(Bytes *b, u64 v, int n)
{
    for (int i = 0; i < n; i++) bad1(b, i < 8 ? (u8)(v >> (8 * i)) : (u8)((i64)v < 0 ? 0xff : 0));
}

void bputle(Bytes *b, size_t off, u64 v, int n)
{
    for (int i = 0; i < n; i++) b->p[off + i] = i < 8 ? (u8)(v >> (8 * i)) : 0;
}

void bfill(Bytes *b, u8 c, size_t n)
{
    if (!n) return;
    bgrow(b, n);
    memset(b->p + b->n, c, n);
    b->n += n;
}

void bpad(Bytes *b, size_t align, u8 fill)
{
    if (align <= 1) return;
    size_t r = b->n % align;
    if (r) bfill(b, fill, align - r);
}

char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1L << 30)) { fclose(f); return NULL; }
    char *buf = xmalloc((size_t)n + 1);
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[r] = 0;
    if (len) *len = r;
    return buf;
}

#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#endif

int write_file(const char *path, const void *data, size_t len, int exec)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (len && fwrite(data, 1, len, f) != len) { fclose(f); return -1; }
    fclose(f);
#ifndef _WIN32
    if (exec) chmod(path, 0755);
#else
    (void)exec;
#endif
    return 0;
}

int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int file_exists(const char *path)
{
    if (is_dir(path)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

char *path_dir(const char *path)
{
    const char *s = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    if (b && (!s || b > s)) s = b;
    if (!s) return xstrdup("");
    return xstrndup(path, (size_t)(s - path + 1));
}

char *path_join(const char *dir, const char *name)
{
    size_t n = strlen(dir);
    if (n && dir[n - 1] != '/' && dir[n - 1] != '\\') {
        Str s = {0};
        sadd(&s, dir);
        sadc(&s, '/');
        sadd(&s, name);
        return sget(&s);
    }
    Str s = {0};
    sadd(&s, dir);
    sadd(&s, name);
    return sget(&s);
}

static size_t hash_str(const char *s)
{
    size_t h = 1469598103u;
    while (*s) { h ^= (u8)*s++; h *= 16777619u; }
    return h;
}

static void ht_init(HT *h)
{
    h->nb = 256;
    h->b = xcalloc(h->nb, sizeof(HEnt *));
}

void *ht_get(HT *h, const char *k)
{
    if (!h->b) return NULL;
    for (HEnt *e = h->b[hash_str(k) % h->nb]; e; e = e->next)
        if (!strcmp(e->key, k)) return e->val;
    return NULL;
}

void ht_put(HT *h, const char *k, void *v)
{
    if (!h->b) ht_init(h);
    size_t i = hash_str(k) % h->nb;
    for (HEnt *e = h->b[i]; e; e = e->next)
        if (!strcmp(e->key, k)) { e->val = v; return; }
    HEnt *e = xmalloc(sizeof *e);
    e->key = xstrdup(k);
    e->val = v;
    e->next = h->b[i];
    h->b[i] = e;
    h->cnt++;
}

int ht_del(HT *h, const char *k)
{
    if (!h->b) return 0;
    size_t i = hash_str(k) % h->nb;
    HEnt **pp = &h->b[i];
    while (*pp) {
        if (!strcmp((*pp)->key, k)) {
            HEnt *d = *pp;
            *pp = d->next;
            free(d->key);
            free(d);
            h->cnt--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

void ht_clear(HT *h)
{
    if (!h->b) return;
    for (size_t i = 0; i < h->nb; i++) {
        HEnt *e = h->b[i];
        while (e) { HEnt *n = e->next; free(e->key); free(e); e = n; }
        h->b[i] = NULL;
    }
    h->cnt = 0;
}

#ifdef _WIN32
#include <io.h>
#define ISATTY(fd) _isatty(fd)
#else
#include <unistd.h>
#define ISATTY(fd) isatty(fd)
#endif

#define MAX_ERRORS 25

static int color_mode = -1;

static int use_color(void)
{
    if (color_mode < 0) {
#ifdef _WIN32
        color_mode = 0;
#else
        color_mode = ISATTY(2) && !getenv("NO_COLOR");
#endif
    }
    return color_mode;
}

typedef struct { char *name; char *data; } SrcCache;
static SrcCache g_cache[8];
static int g_ncache;

static const char *src_line(const char *file, int line, size_t *len)
{
    if (!file || file[0] == '<' || line < 1) return NULL;
    char *data = NULL;
    for (int i = 0; i < g_ncache; i++) if (!strcmp(g_cache[i].name, file)) data = g_cache[i].data;
    if (!data) {
        data = read_file(file, NULL);
        if (!data) return NULL;
        if (g_ncache < 8) {
            g_cache[g_ncache].name = xstrdup(file);
            g_cache[g_ncache].data = data;
            g_ncache++;
        }
    }
    const char *p = data;
    for (int n = 1; n < line; n++) {
        p = strchr(p, '\n');
        if (!p) return NULL;
        p++;
    }
    const char *e = p;
    while (*e && *e != '\n' && *e != '\r') e++;
    *len = (size_t)(e - p);
    return p;
}

static void vmsg(const char *kind, const char *fmt, va_list ap)
{
    int col = use_color();
    const char *cs = "", *ce = "";
    if (col) {
        ce = "\033[0m";
        cs = kind[0] == 'w' ? "\033[1;33m" : "\033[1;31m";
    }
    if (g_pos.file) fprintf(stderr, "%s:%d: %s%s:%s ", g_pos.file, g_pos.line, cs, kind, ce);
    else fprintf(stderr, "cly: %s%s:%s ", cs, kind, ce);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    size_t n = 0;
    const char *t = g_pos.file ? src_line(g_pos.file, g_pos.line, &n) : NULL;
    if (t) {
        while (n && (*t == ' ' || *t == '\t')) { t++; n--; }
        if (n) fprintf(stderr, "  %5d | %.*s\n", g_pos.line, (int)(n > 100 ? 100 : n), t);
    }
}

void err(const char *fmt, ...)
{
    if (g_quiet) { g_errors++; return; }
    va_list ap;
    va_start(ap, fmt);
    vmsg("error", fmt, ap);
    va_end(ap);
    g_errors++;
    if (g_errors >= MAX_ERRORS) {
        fprintf(stderr, "cly: too many errors, stopping\n");
        exit(1);
    }
}

void warn(const char *fmt, ...)
{
    if (g_nowarn || g_quiet) return;
    va_list ap;
    va_start(ap, fmt);
    vmsg("warning", fmt, ap);
    va_end(ap);
    g_warnings++;
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("fatal", fmt, ap);
    va_end(ap);
    exit(1);
}

void lv_add(LineVec *l, const char *text, const char *file, int line)
{
    if (l->n >= 8000000) {
        fprintf(stderr, "cly: error: too many source lines generated (runaway macro or %%rep?)\n");
        exit(1);
    }
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof(SLine));
    }
    l->v[l->n].text = xstrdup(text);
    l->v[l->n].file = file;
    l->v[l->n].line = line;
    l->n++;
}

int edit_distance(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la > 40 || lb > 40) return 99;
    int d[42][42];
    for (size_t i = 0; i <= la; i++) d[i][0] = (int)i;
    for (size_t j = 0; j <= lb; j++) d[0][j] = (int)j;
    for (size_t i = 1; i <= la; i++)
        for (size_t j = 1; j <= lb; j++) {
            int c = tolower((unsigned char)a[i - 1]) != tolower((unsigned char)b[j - 1]);
            int m = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < m) m = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + c < m) m = d[i - 1][j - 1] + c;
            d[i][j] = m;
        }
    return d[la][lb];
}
