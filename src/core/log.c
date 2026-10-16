/* Support for -log: timestamped lines for each compile step. */
#include "cly.h"
#include <time.h>

int g_log;
static FILE *lf;
static double t0;

/* Seconds from a monotonic clock, falling back to the wall clock. */
static double now(void)
{
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

/* Starts logging to stderr, or to the file given. Returns 0 if the file cannot be opened. */
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

/* Stops logging and closes the file if one was opened. */
void log_close(void)
{
    if (lf && lf != stderr) fclose(lf);
    lf = NULL;
    g_log = 0;
}

/* Writes one log line tagged with the phase name and the time since logging started. Does nothing when -log is off. */
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
