#include "cly.h"
#include <time.h>

int g_log;
static FILE *lf;
static double t0;

static double now(void)
{
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

int log_open(const char *path)
{
    lf = stderr;
    if (path && *path) {
        lf = fopen(path, "w");
        if (!lf) { lf = stderr; return 0; }
    }
    g_log = 1;
    t0 = now();
    return 1;
}

void log_close(void)
{
    if (lf && lf != stderr) fclose(lf);
    lf = NULL;
    g_log = 0;
}

void xlog(const char *phase, const char *fmt, ...)
{
    if (!g_log || !lf) return;
    va_list ap;
    fprintf(lf, "[%9.6f] %-10s ", now() - t0, phase);
    va_start(ap, fmt);
    vfprintf(lf, fmt, ap);
    va_end(ap);
    fputc('\n', lf);
}
