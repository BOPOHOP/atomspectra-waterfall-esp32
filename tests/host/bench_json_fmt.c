// UI-P1: замер до/после для build_json_full-style цикла по SPECTRUM_CHANNELS
// бинов. Старый путь — append_fmt (vsnprintf + realloc-доубливание), новый —
// json_append_uint32_csv в буфер фиксированного размера.
#define _POSIX_C_SOURCE 200809L
#include "json_uint_fmt.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>

#define CHANNELS 8192
static bool old_append_fmt(char **buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        size_t avail = (*cap > *len) ? (*cap - *len) : 0;
        va_start(ap, fmt);
        int n = vsnprintf(*buf + *len, avail, fmt, ap);
        va_end(ap);
        if (n < 0) return false;
        if ((size_t)n < avail) { *len += (size_t)n; return true; }
        size_t need = *len + (size_t)n + 1;
        size_t ncap = *cap ? *cap * 2 : 65536;
        while (ncap < need) ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (!nb) return false;
        *buf = nb; *cap = ncap;
    }
}
static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
int main(void)
{
    uint32_t *bins = malloc(CHANNELS * sizeof(uint32_t));
    unsigned seed = 42;
    for (int i = 0; i < CHANNELS; i++) { seed = seed*1103515245u+12345u; bins[i] = seed; }
    bins[0] = 0; bins[1] = UINT32_MAX;

    const int reps = 200;
    int64_t t0 = now_us();
    for (int r = 0; r < reps; r++) {
        char *buf = NULL; size_t len = 0, cap = 0;
        old_append_fmt(&buf, &len, &cap, "{\"bins\":[");
        for (int i = 0; i < CHANNELS; i++)
            old_append_fmt(&buf, &len, &cap, "%s%" PRIu32, i ? "," : "", bins[i]);
        old_append_fmt(&buf, &len, &cap, "]}");
        free(buf);
    }
    int64_t old_us = (now_us() - t0) / reps;
    size_t newcap = CHANNELS * 11 + 16;
    char *nbuf = malloc(newcap);
    t0 = now_us();
    for (int r = 0; r < reps; r++) {
        size_t p = 0;
        memcpy(nbuf + p, "{\"bins\":[", 9); p += 9;
        for (int i = 0; i < CHANNELS; i++)
            p += json_append_uint32_csv(nbuf, newcap, p, bins[i], i != 0);
        nbuf[p++] = ']'; nbuf[p++] = '}';
    }
    int64_t new_us = (now_us() - t0) / reps;
    printf("UI-P1 bench: old(vsnprintf-loop)=%" PRId64 "us  new(json_append_uint32_csv)=%" PRId64 "us  speedup=%.2fx\n",
           old_us, new_us, new_us ? (double)old_us / (double)new_us : 0.0);
    free(nbuf);
    free(bins);
    return 0;
}
