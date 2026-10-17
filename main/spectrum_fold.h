#pragma once
#include "spectrum_meta_fmt.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool spec_channels_valid(int n) {
    return n == 8192 || n == 4096 || n == 2048 || n == 1024;
}

static inline int spec_channels_query_parse(const char *query, int *n) {
    if (query == NULL) return 0;
    const char *p = query;
    while (*p != '\0') {
        if (p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'n' && p[4] == 'n' && p[5] == 'e' && p[6] == 'l' && p[7] == 's' && (p[8] == '=' || p[8] == '&' || p[8] == '\0')) {   /* ключ целиком: "channelsx=" - чужой ключ */
            if (p[8] == '=') {
                const char *v = p + 9;
                size_t len = 0;
                while (v[len] != '\0' && v[len] != '&') len++;
                if (len < 1 || len > 5) return -1;
                for (size_t i = 0; i < len; i++) {
                    if (v[i] < '0' || v[i] > '9') return -1;
                }
                if (v[0] == '0') return -1;
                int val = 0;
                for (size_t i = 0; i < len; i++) val = val * 10 + (v[i] - '0');
                if (!spec_channels_valid(val)) return -1;
                *n = val;
                return 1;
            } else {
                return -1;
            }
        }
        while (*p != '\0' && *p != '&') p++;   /* ключ - только в начале или после '&' */
        if (*p == '&') p++;
    }
    return 0;
}

static inline int spec_fold_m(int n) {
    return SPECTRUM_CHANNELS / n;
}

static inline uint32_t spec_fold_group(const uint32_t *bins, int m, int k) {
    uint64_t sum = 0;
    for (int j = 0; j < m; j++) {
        sum += bins[k * m + j];
    }
    if (sum > UINT32_MAX) return UINT32_MAX;
    return (uint32_t)sum;
}

static inline size_t spec_json_full_fmt_v2(char *buf, size_t cap, const uint32_t *bins, int n, const char *meta, size_t meta_len, size_t a_end, size_t b_start) {
    if (!spec_channels_valid(n) || a_end < 1 || b_start < a_end || b_start > meta_len) {
        if (cap > 0) buf[0] = '\0';
        return 0;
    }
    spec_w_t w = { buf, cap, 0 };
    spec_w_bytes(&w, "{\"bins\":[", 9);
    int m = spec_fold_m(n);
    int ng = SPECTRUM_CHANNELS / m;
    for (int k = 0; k < ng; k++) {
        uint32_t v = spec_fold_group(bins, m, k);
        char tmp[12];
        size_t n_bytes = json_append_uint32_csv(tmp, sizeof tmp, 0, v, k != 0);
        spec_w_bytes(&w, tmp, n_bytes);
    }
    spec_w_bytes(&w, "],", 2);
    spec_w_bytes(&w, meta + 1, a_end - 1);
    spec_w_fmt(&w, ",\"channels\":%d", n);
    spec_w_bytes(&w, meta + b_start, meta_len - b_start);
    if (cap > 0) {
        size_t term = (w.len < cap) ? w.len : cap - 1;
        buf[term] = '\0';
    }
    return w.len;
}
