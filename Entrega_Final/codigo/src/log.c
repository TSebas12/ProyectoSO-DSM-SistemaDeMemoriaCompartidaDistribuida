#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dsm_log.h"

static FILE *logf = NULL;
static int loglevel = 2;

void dsm_log_init(const char *file, int level) {
    loglevel = level;
    if (file) {
        logf = fopen(file, "a");
    }
    if (!logf) logf = stderr;
}

void dsm_log_close(void) {
    if (logf && logf != stderr) fclose(logf);
    logf = NULL;
}

void dsm_log(int level, const char *fmt, ...) {
    if (level > loglevel) return;
    if (!logf) logf = stderr;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    const char *names[] = {"ERROR","WARN","INFO","DEBUG","TRACE"};
    fprintf(logf, "[%s] %s: ", buf, names[level > 4 ? 4 : level]);
    va_list ap; va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fprintf(logf, "\n");
    fflush(logf);
}