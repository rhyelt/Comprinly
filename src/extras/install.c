/* Installing Comprinly as a command (-global, -unglobal, -uninstall), self update (-update) and the first run hint. */
#include "cly.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#define isatty _isatty
#define PSEP '\\'
#else
#include <unistd.h>
#include <limits.h>
#define PSEP '/'
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#define REPO_API "https://api.github.com/repos/rhyelt/Comprinly/releases/latest"
#define REPO_PAGE "https://github.com/rhyelt/Comprinly/releases"

#ifdef _WIN32
#define EXE_NAME "cly.exe"
#else
#define EXE_NAME "cly"
#endif

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int isdir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

static void make_dir(const char *p)
{
#ifdef _WIN32
    _mkdir(p);
#else
    mkdir(p, 0755);
#endif
}

static void make_dirs(const char *path)
{
    char *t = xstrdup(path);
    for (char *q = t + 1; *q; q++) {
        if (*q == '/' || *q == '\\') {
            char c = *q;
            *q = 0;
            if (!isdir(t)) make_dir(t);
            *q = c;
        }
    }
    if (!isdir(t)) make_dir(t);
    free(t);
}

static char *join(const char *a, const char *b)
{
    size_t n = strlen(a);
    char *r = xmalloc(n + strlen(b) + 2);
    strcpy(r, a);
    if (n && a[n - 1] != '/' && a[n - 1] != '\\') { r[n++] = PSEP; }
    strcpy(r + n, b);
    return r;
}

static char *self_path(void)
{
#ifdef _WIN32
    char buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (!n || n >= sizeof buf) return NULL;
    return xstrdup(buf);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz)) return NULL;
    char *rp = realpath(buf, NULL);
    return rp ? rp : xstrdup(buf);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return NULL;
    buf[n] = 0;
    return xstrdup(buf);
#endif
}

static const char *home_dir(void)
{
    const char *h = getenv("HOME");
#ifdef _WIN32
    if (!h || !*h) h = getenv("USERPROFILE");
#endif
    return h && *h ? h : NULL;
}

static char *config_dir(void)
{
#ifdef _WIN32
    const char *a = getenv("APPDATA");
    if (!a || !*a) a = home_dir();
    return a ? join(a, "Comprinly") : NULL;
#else
    const char *x = getenv("XDG_CONFIG_HOME");
    if (x && *x) return join(x, "comprinly");
    const char *h = home_dir();
    if (!h) return NULL;
    char *c = join(h, ".config");
    char *r = join(c, "comprinly");
    free(c);
    return r;
#endif
}

static char *dir_of(const char *p)
{
    char *r = xstrdup(p);
    char *s = strrchr(r, '/');
    char *b = strrchr(r, '\\');
    if (b && (!s || b > s)) s = b;
    if (!s) { free(r); return xstrdup("."); }
    if (s == r) s[1] = 0; else *s = 0;
    return r;
}

static int same_path(const char *a, const char *b)
{
#ifdef _WIN32
    return !_stricmp(a, b);
#else
    return !strcmp(a, b);
#endif
}

static int writable_dir(const char *d)
{
    if (!isdir(d)) return 0;
#ifdef _WIN32
    char *t = join(d, ".cly_probe");
    FILE *f = fopen(t, "wb");
    if (f) { fclose(f); remove(t); }
    free(t);
    return f != NULL;
#else
    return access(d, W_OK) == 0;
#endif
}

static int path_has(const char *d)
{
    const char *p = getenv("PATH");
    if (!p) return 0;
    size_t n = strlen(d);
#ifdef _WIN32
    char sep = ';';
#else
    char sep = ':';
#endif
    while (*p) {
        const char *e = strchr(p, sep);
        size_t l = e ? (size_t)(e - p) : strlen(p);
        while (l > 1 && (p[l - 1] == '/' || p[l - 1] == '\\')) l--;
        if (l == n && !strncmp(p, d, n)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static char *global_dir(int create)
{
    const char *ov = getenv("CLY_GLOBAL_DIR");
    if (ov && *ov) {
        char *d = xstrdup(ov);
        if (create) make_dirs(d);
        if (isdir(d)) return d;
        free(d);
        return NULL;
    }
#ifdef _WIN32
    const char *la = getenv("LOCALAPPDATA");
    if (la && *la) {
        char *a = join(la, "Microsoft");
        char *w = join(a, "WindowsApps");
        free(a);
        if (writable_dir(w)) return w;
        free(w);
        char *b = join(la, "Comprinly");
        char *c = join(b, "bin");
        free(b);
        if (create) make_dirs(c);
        if (isdir(c)) return c;
        free(c);
    }
    return NULL;
#else
    if (writable_dir("/usr/local/bin")) return xstrdup("/usr/local/bin");
    const char *h = home_dir();
    if (!h) return NULL;
    char *a = join(h, ".local");
    char *b = join(a, "bin");
    free(a);
    if (create) make_dirs(b);
    if (isdir(b)) return b;
    free(b);
    return NULL;
#endif
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) return 0;
    char *tmp = xmalloc(strlen(to) + 8);
    sprintf(tmp, "%s.new", to);
    FILE *out = fopen(tmp, "wb");
    if (!out) { fclose(in); free(tmp); return 0; }
    char buf[65536];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    fclose(in);
    if (fclose(out)) ok = 0;
    if (!ok) { remove(tmp); free(tmp); return 0; }
#ifndef _WIN32
    chmod(tmp, 0755);
#endif
#ifdef _WIN32
    remove(to);
#endif
    if (rename(tmp, to)) { remove(tmp); free(tmp); return 0; }
    free(tmp);
    return 1;
}

static int move_file(const char *from, const char *to)
{
#ifdef _WIN32
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) return 1;
    if (!copy_file(from, to)) return 0;
    if (remove(from)) {
        char old[4200];
        snprintf(old, sizeof old, "%s.old", from);
        remove(old);
        MoveFileExA(from, old, MOVEFILE_REPLACE_EXISTING);
    }
    return 1;
#else
    if (!rename(from, to)) return 1;
    if (!copy_file(from, to)) return 0;
    remove(from);
    return 1;
#endif
}

static char *read_small(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char buf[4200];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = 0;
    return n ? xstrdup(buf) : NULL;
}

static void write_small(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(text, f);
    fputc('\n', f);
    fclose(f);
}

static char *installed_copy(void)
{
    char *d = global_dir(0);
    if (d) {
        char *p = join(d, EXE_NAME);
        free(d);
        if (exists(p)) return p;
        free(p);
    }
#ifndef _WIN32
    const char *h = home_dir();
    if (h) {
        char *a = join(h, ".local");
        char *b = join(a, "bin");
        char *p = join(b, EXE_NAME);
        free(a); free(b);
        if (exists(p)) return p;
        free(p);
    }
    if (exists("/usr/local/bin/" EXE_NAME)) return xstrdup("/usr/local/bin/" EXE_NAME);
#else
    const char *la = getenv("LOCALAPPDATA");
    if (la) {
        char *a = join(la, "Microsoft");
        char *b = join(a, "WindowsApps");
        char *p = join(b, EXE_NAME);
        free(a); free(b);
        if (exists(p)) return p;
        free(p);
        a = join(la, "Comprinly");
        b = join(a, "bin");
        p = join(b, EXE_NAME);
        free(a); free(b);
        if (exists(p)) return p;
        free(p);
    }
#endif
    return NULL;
}

int install_global(void)
{
    char *self = self_path();
    if (!self) { fprintf(stderr, "cly: cannot find where this program is\n"); return 1; }
    char *inst = installed_copy();
    if (inst && same_path(inst, self)) {
        printf("Comprinly is already installed globally at %s\n", self);
        return 0;
    }
    char *gd = global_dir(1);
    if (!gd) { fprintf(stderr, "cly: cannot find a folder for global commands\n"); return 1; }
    char *dest = join(gd, EXE_NAME);
    char *cfg = config_dir();
    if (cfg) {
        make_dirs(cfg);
        char *op = join(cfg, "origin");
        write_small(op, self);
        free(op);
    }
    if (!move_file(self, dest)) {
        fprintf(stderr, "cly: cannot move `%s' to `%s': %s\n", self, dest, strerror(errno));
#ifndef _WIN32
        if (strcmp(gd, "/usr/local/bin") == 0) fprintf(stderr, "cly: try again with sudo, or the command will be placed in ~/.local/bin\n");
#endif
        return 1;
    }
    printf("Moved Comprinly to %s\n", dest);
    if (!path_has(gd)) {
#ifdef _WIN32
        printf("Add %s to your PATH, then open a new terminal and run `cly -v`.\n", gd);
#else
        printf("%s is not in your PATH. Add it with:  export PATH=\"%s:$PATH\"\n", gd, gd);
#endif
    } else printf("You can now run `cly` from any folder. `cly -unglobal` moves it back.\n");
    return 0;
}

int install_unglobal(void)
{
    char *self = self_path();
    char *inst = installed_copy();
    char *src = NULL;
    if (self && inst && same_path(inst, self)) src = self;
    else if (inst) src = inst;
    if (!src) { fprintf(stderr, "cly: Comprinly is not installed globally\n"); return 1; }
    char *cfg = config_dir();
    char *origin = NULL;
    char *op = cfg ? join(cfg, "origin") : NULL;
    if (op) origin = read_small(op);
    char *destdir = NULL;
    if (origin) {
        char *d = dir_of(origin);
        if (isdir(d) && writable_dir(d)) destdir = d; else free(d);
    }
    if (!destdir) {
        const char *h = home_dir();
        if (h) {
            char *d = join(h, "Downloads");
            if (!isdir(d)) make_dirs(d);
            if (isdir(d)) destdir = d; else free(d);
        }
    }
    if (!destdir) destdir = xstrdup(".");
    char *dest = join(destdir, EXE_NAME);
    if (!move_file(src, dest)) {
        fprintf(stderr, "cly: cannot move `%s' to `%s': %s\n", src, dest, strerror(errno));
        return 1;
    }
    if (op) remove(op);
    printf("Moved Comprinly back to %s\n", dest);
    return 0;
}

int install_uninstall(void)
{
    char *self = self_path();
    char *inst = installed_copy();
    char *cfg = config_dir();
    printf("This removes Comprinly from this computer:\n");
    if (inst) printf("  %s\n", inst);
    if (self && (!inst || !same_path(self, inst))) printf("  %s\n", self);
    if (cfg && isdir(cfg)) printf("  %s (settings)\n", cfg);
    printf("Continue? [y/N] ");
    fflush(stdout);
    char line[64];
    if (!fgets(line, sizeof line, stdin) || (line[0] != 'y' && line[0] != 'Y')) {
        printf("Nothing was removed.\n");
        return 0;
    }
    int bad = 0;
    if (inst) {
        if (self && same_path(inst, self)) {}
        else if (remove(inst)) { fprintf(stderr, "cly: cannot remove `%s': %s\n", inst, strerror(errno)); bad = 1; }
    }
    if (cfg) {
        char *p = join(cfg, "origin");
        remove(p);
        free(p);
        p = join(cfg, "hint");
        remove(p);
        free(p);
#ifdef _WIN32
        _rmdir(cfg);
#else
        rmdir(cfg);
#endif
    }
    if (self) {
#ifdef _WIN32
        char cmd[9000];
        snprintf(cmd, sizeof cmd, "start \"\" /b cmd /c \"ping -n 3 127.0.0.1 >nul & del /f /q \"%s\"\"", self);
        system(cmd);
#else
        if (remove(self)) { fprintf(stderr, "cly: cannot remove `%s': %s\n", self, strerror(errno)); bad = 1; }
#endif
    }
    if (!bad) printf("Comprinly was uninstalled.\n");
    return bad;
}

void install_hint(void)
{
    if (!isatty(2) || getenv("CLY_NO_HINT")) return;
    char *cfg = config_dir();
    if (!cfg) return;
    char *hp = join(cfg, "hint");
    if (exists(hp)) return;
    char *self = self_path();
    char *inst = installed_copy();
    int already = self && inst && same_path(self, inst);
    if (!already) fprintf(stderr, "tip: run `cly -global` once to install Comprinly as a command you can use from any folder.\n");
    make_dirs(cfg);
    write_small(hp, "shown");
}

static char *run_capture(const char *cmd, size_t *len)
{
    FILE *p = popen(cmd, "r");
    if (!p) return NULL;
    size_t cap = 8192, n = 0;
    char *b = xmalloc(cap);
    size_t r;
    while ((r = fread(b + n, 1, cap - n - 1, p)) > 0) {
        n += r;
        if (cap - n < 2048) { cap *= 2; b = xrealloc(b, cap); }
    }
    int rc = pclose(p);
    b[n] = 0;
    *len = n;
    if (rc != 0 || !n) { free(b); return NULL; }
    return b;
}

static int parse_ver(const char *s, int v[4])
{
    v[0] = v[1] = v[2] = v[3] = 0;
    while (*s && !isdigit((unsigned char)*s)) s++;
    if (!*s) return 0;
    for (int i = 0; i < 4 && *s; i++) {
        v[i] = atoi(s);
        while (isdigit((unsigned char)*s)) s++;
        if (*s == '.') s++; else break;
    }
    return 1;
}

int ver_newer(const char *remote, const char *local)
{
    int a[4], b[4];
    if (!parse_ver(remote, a) || !parse_ver(local, b)) return 0;
    for (int i = 0; i < 4; i++) { if (a[i] != b[i]) return a[i] > b[i]; }
    return 0;
}

static char *json_str(const char *j, const char *key, const char **next)
{
    char pat[80];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(j, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n') p++;
    if (*p != '"') return NULL;
    p++;
    const char *e = p;
    while (*e && *e != '"') { if (*e == '\\' && e[1]) e++; e++; }
    if (next) *next = e;
    return xstrndup(p, (size_t)(e - p));
}

static int url_safe(const char *u)
{
    if (strncmp(u, "https://", 8) && strncmp(u, "http://", 7) && strncmp(u, "file://", 7)) return 0;
    for (; *u; u++) {
        unsigned char c = (unsigned char)*u;
        if (!(isalnum(c) || strchr("._~:/?&=%+-@,#", c))) return 0;
    }
    return 1;
}

static int asset_score(const char *url)
{
    if (!url_safe(url)) return -1;
    const char *n = strrchr(url, '/');
    n = n ? n + 1 : url;
    size_t l = strlen(n);
    if (l > 4 && (!strcmp(n + l - 4, ".zip") || !strcmp(n + l - 3, ".md") || !strcmp(n + l - 4, ".txt") || !strcmp(n + l - 3, ".gz") || !strcmp(n + l - 4, ".sig") || !strcmp(n + l - 7, ".sha256"))) return -1;
    int is64 = sizeof(void *) == 8;
#ifdef _WIN32
    if (l < 4 || _stricmp(n + l - 4, ".exe")) return -1;
    return strstr(n, "cly") || strstr(n, "comprinly") ? 10 : 1;
#else
    if (l > 4 && !strcmp(n + l - 4, ".exe")) return -1;
#ifdef __APPLE__
    if (!strstr(n, "mac") && !strstr(n, "darwin")) return -1;
    return 10;
#else
    if (strstr(n, "mac") || strstr(n, "darwin") || strstr(n, "win")) return -1;
    if (is64 && !strcmp(n, "cly64")) return 20;
    if (!is64 && !strcmp(n, "cly32")) return 20;
    if (is64 && strstr(n, "32") && !strstr(n, "64")) return -1;
    if (!is64 && strstr(n, "64")) return -1;
    if (!strcmp(n, "cly")) return 15;
    if (strstr(n, "linux")) return 12;
    if (strstr(n, "cly")) return 5;
    return 0;
#endif
#endif
}

int install_update(void)
{
    const char *api = getenv("CLY_UPDATE_API");
    if (!api || !*api) api = REPO_API;
    if (!url_safe(api)) { fprintf(stderr, "cly: CLY_UPDATE_API is not a usable address\n"); return 1; }
    char cmd[1500];
    snprintf(cmd, sizeof cmd, "curl -fsL -A cly-update -H \"Accept: application/vnd.github+json\" \"%s\"", api);
    printf("Checking for a new release...\n");
    size_t n = 0;
    char *json = run_capture(cmd, &n);
    if (!json) {
        fprintf(stderr, "cly: could not read the latest release. Check your connection, that curl is installed and that a release exists at %s\n", REPO_PAGE);
        return 1;
    }
    char *tag = json_str(json, "tag_name", NULL);
    if (!tag) { fprintf(stderr, "cly: could not read the latest release from GitHub\n"); return 1; }
    if (!ver_newer(tag, CLY_VERSION)) {
        printf("Comprinly %s is up to date (latest release: %s).\n", CLY_VERSION, tag);
        return 0;
    }
    char *best = NULL;
    int bs = -1;
    const char *scan = json;
    for (;;) {
        const char *k = strstr(scan, "\"browser_download_url\"");
        if (!k) break;
        const char *nx = NULL;
        char *u = json_str(k, "browser_download_url", &nx);
        if (!u) break;
        int sc = asset_score(u);
        if (sc > bs) { bs = sc; free(best); best = u; } else free(u);
        scan = nx ? nx : k + 10;
    }
    printf("A newer release is available: %s (you have %s).\n", tag, CLY_VERSION);
    if (!best || bs < 0) {
        fprintf(stderr, "cly: no download for this system was found in the release. Get it from %s\n", REPO_PAGE);
        return 1;
    }
    printf("Update now? [y/N] ");
    fflush(stdout);
    char line[64];
    if (!fgets(line, sizeof line, stdin) || (line[0] != 'y' && line[0] != 'Y')) {
        printf("Not updating.\n");
        return 0;
    }
    char *self = self_path();
    if (!self) { fprintf(stderr, "cly: cannot find where this program is\n"); return 1; }
    char *tmp = xmalloc(strlen(self) + 16);
    sprintf(tmp, "%s.update", self);
    char dl[9000];
    snprintf(dl, sizeof dl, "curl -fsSL -A cly-update -o \"%s\" \"%s\"", tmp, best);
    if (system(dl) != 0) {
        fprintf(stderr, "cly: the download failed\n");
        remove(tmp);
        return 1;
    }
    size_t fl = 0;
    char *chk = read_file(tmp, &fl);
    int okmagic = chk && fl > 1000 && ((!memcmp(chk, "\x7f" "ELF", 4)) || (chk[0] == 'M' && chk[1] == 'Z') || !memcmp(chk, "\xcf\xfa\xed\xfe", 4));
    free(chk);
    if (!okmagic) {
        fprintf(stderr, "cly: the downloaded file is not a program; the update was cancelled\n");
        remove(tmp);
        return 1;
    }
#ifdef _WIN32
    char old[4200];
    snprintf(old, sizeof old, "%s.old", self);
    remove(old);
    if (!MoveFileExA(self, old, MOVEFILE_REPLACE_EXISTING) || !MoveFileExA(tmp, self, MOVEFILE_REPLACE_EXISTING)) {
        fprintf(stderr, "cly: cannot replace `%s'\n", self);
        MoveFileExA(old, self, MOVEFILE_REPLACE_EXISTING);
        return 1;
    }
#else
    chmod(tmp, 0755);
    if (rename(tmp, self)) {
        fprintf(stderr, "cly: cannot replace `%s': %s\n", self, strerror(errno));
        remove(tmp);
        return 1;
    }
#endif
    printf("Updated to %s.\n", tag);
    return 0;
}

void install_cleanup_old(void)
{
#ifdef _WIN32
    char *self = self_path();
    if (!self) return;
    char old[4200];
    snprintf(old, sizeof old, "%s.old", self);
    remove(old);
#endif
}
