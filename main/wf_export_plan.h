// #FW-19 (sweep-A, 1.2.28): чистая логика экспорта n42 полной истории водопада —
// разбор шапки сегмента ASWF, поля строки, время начала строки и ПЛАН выдачи
// (порядок сегментов с flash и участков кольца PSRAM). Раньше экспорт брал только
// кольцо PSRAM (WF_RING_ROWS_DEFAULT = 256 строк), хотя полная история лежит в
// сегментах на flash. Правила разбора шапки повторяют seg_detect_stride /
// seg_detect_version / seg_payload_offset (spectrogram.c) — те же, по которым
// boot-реконсиляция считает строки реестра. Время — по ASWF_FORMAT.md:248-261
// (timestamp строки, если > 0, иначе started_at + сумма длительностей).

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spectrogram.h"

typedef struct {
    int      version;       /* 1..9; default 2 */
    uint32_t stride;        /* row stride; default WF_ROW_BYTES */
    uint32_t interval_sec;  /* 0 if absent */
    int64_t  started_at;    /* 0 if absent */
} wf_seg_hdr_info_t;

static inline bool wf_hdr_find_num(const char *hdr, const char *key, long long *out) {
    if (!hdr || !key || !out) return false;

    size_t klen = strlen(key);
    if (klen + 3 >= sizeof(char[48])) return false;

    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);

    const char *p = strstr(hdr, pat);
    if (!p) return false;

    p += strlen(pat);
    while (*p == ' ') p++;

    char *end;
    long long val = strtoll(p, &end, 10);
    if (end == p) return false;

    *out = val;
    return true;
}

static inline void wf_seg_hdr_parse(const char *hdr, wf_seg_hdr_info_t *out) {
    out->version = 2;
    out->stride = WF_ROW_BYTES;
    out->interval_sec = 0;
    out->started_at = 0;

    if (!hdr) return;

    long long val;
    if (wf_hdr_find_num(hdr, "version", &val)) {
        if (val >= 1 && val <= 9) out->version = (int)val;
    }
    if (wf_hdr_find_num(hdr, "row_stride", &val)) {
        if (val >= WF_ROW_BYTES && val <= WF_ROW_BYTES + 128) out->stride = (uint32_t)val;
    }
    if (wf_hdr_find_num(hdr, "interval_sec", &val)) {
        if (val >= 0 && val <= 0xFFFFFFFF) out->interval_sec = (uint32_t)val;
    }
    if (wf_hdr_find_num(hdr, "started_at", &val)) {
        out->started_at = (int64_t)val;
    }
}

static inline long wf_seg_payload_offset(int version, uint32_t hlen) {
    long off = 8 + (long)hlen;
    if (version >= 3) off += WF_BASELINE_BYTES;
    return off;
}

static inline uint32_t wf_seg_rows_in(long file_size, long payload_off, uint32_t stride) {
    if (stride == 0 || file_size <= payload_off) return 0;
    return (uint32_t)((file_size - payload_off) / stride);
}

static inline bool wf_row_has_dur(uint32_t stride) {
    return stride >= WF_ROW_BYTES + WF_DUR_BYTES;
}

static inline bool wf_row_has_ts(int version, uint32_t stride) {
    return version >= 3 && stride >= WF_ROW_BYTES + WF_DUR_BYTES + WF_TS_BYTES;
}

static inline uint32_t wf_row_dur(const uint8_t *row, uint32_t stride) {
    if (!wf_row_has_dur(stride)) return 0;
    uint16_t dur = (uint16_t)row[WF_ROW_BYTES] | ((uint16_t)row[WF_ROW_BYTES + 1] << 8);
    return (uint32_t)dur;
}

static inline uint32_t wf_row_ts(const uint8_t *row, int version, uint32_t stride) {
    if (!wf_row_has_ts(version, stride)) return 0;
    size_t off = WF_ROW_BYTES + WF_DUR_BYTES;
    uint32_t ts = (uint32_t)row[off] |
                  ((uint32_t)row[off + 1] << 8) |
                  ((uint32_t)row[off + 2] << 16) |
                  ((uint32_t)row[off + 3] << 24);
    return ts;
}

static inline uint32_t wf_exp_eff_dur(uint32_t dur, uint32_t interval_sec) {
    return (dur != 0) ? dur : interval_sec;
}

static inline int64_t wf_exp_row_start(uint32_t ts, int64_t started_at, uint64_t cum_before) {
    if (ts > 0) return (int64_t)ts;
    return started_at + (int64_t)cum_before;
}

#define WF_EXP_STEP_SEG  1   /* a = segment idx, b unused (0) */
#define WF_EXP_STEP_RING 2   /* ring rows with global index in [a, b) */

typedef struct { uint8_t kind; uint32_t a; uint32_t b; } wf_exp_step_t;

/*
 * Старые сегменты (предыдущие сессии / до перезагрузки) идут первыми по idx
 * (idx — монотонный порядок создания для сосуществующих файлов), затем
 * сегменты текущей сессии объединяются с непокрытыми участками кольца по
 * глобальному индексу строк, чтобы строки шли хронологически и ни одна строка
 * не была выдана дважды (строки внутри finalized-сегментов берутся с flash,
 * открытый сегмент и еще не сброшенные строки — из RAM).
 */
static inline int wf_exp_plan(const wf_seg_reg_t *reg, int n, uint32_t r0, uint32_t total, wf_exp_step_t *out, int cap) {
    if (!reg || !out || cap <= 0) return -1;

    int count = 0;

    /* Step 1 & 2: Identify usable segments and separate OLD vs CURRENT */
    /* We will process them in two passes without allocation. */

    /* Pass 1: Emit OLD segments (g1 <= g0) sorted by idx ascending */
    {
        uint32_t last_idx = 0;
        bool have_last = false;   /* выставляется ПОСЛЕ выдачи, не во время поиска */

        /* Simple selection sort logic for emission order */
        while (true) {
            int best_i = -1;
            uint32_t best_idx = UINT32_MAX;

            for (int i = 0; i < n; ++i) {
                const wf_seg_reg_t *s = &reg[i];
                if (!s->valid || !s->finalized || s->rows == 0) continue;
                
                /* OLD segment check */
                if (s->g1 > s->g0) continue;

                /* Must be greater than last emitted idx to maintain ascending order */
                if (have_last && s->idx <= last_idx) continue;

                if (s->idx < best_idx) {
                    best_idx = s->idx;
                    best_i = i;
                }
            }

            if (best_i == -1) break; /* No more old segments */

            if (count >= cap) return -1;
            
            out[count].kind = WF_EXP_STEP_SEG;
            out[count].a = reg[best_i].idx;
            out[count].b = 0;
            count++;
            
            last_idx = best_idx;
            have_last = true;
        }
    }

    /* Pass 2: Emit CURRENT segments (g1 > g0) merged with RING gaps */
    {
        uint32_t c = r0;
        /* Последняя выданная пара (g0, idx): следующий кандидат строго больше неё —
           иначе выбор минимума возвращал бы один и тот же сегмент бесконечно. */
        bool have_last = false;
        uint32_t last_g0 = 0, last_idx = 0;

        while (true) {
            int best_i = -1;
            uint64_t best_g0 = UINT64_MAX;
            uint32_t best_idx = UINT32_MAX;

            for (int i = 0; i < n; ++i) {
                const wf_seg_reg_t *s = &reg[i];
                if (!s->valid || !s->finalized || s->rows == 0) continue;
                
                /* CURRENT segment check */
                if (s->g1 <= s->g0) continue;
                if (have_last && (s->g0 < last_g0 ||
                                  (s->g0 == last_g0 && s->idx <= last_idx))) continue;

                /* Selection criteria: ascending g0, ties broken by idx */
                bool is_better = false;
                if (best_i == -1) {
                    is_better = true;
                } else {
                    uint64_t cur_g0 = (uint64_t)s->g0;
                    uint64_t prev_g0 = best_g0;
                    
                    if (cur_g0 < prev_g0) {
                        is_better = true;
                    } else if (cur_g0 == prev_g0) {
                        if (s->idx < best_idx) {
                            is_better = true;
                        }
                    }
                }

                if (is_better) {
                    best_g0 = (uint64_t)s->g0;
                    best_idx = s->idx;
                    best_i = i;
                }
            }

            if (best_i == -1) break; /* No more current segments */

            const wf_seg_reg_t *s = &reg[best_i];
            have_last = true;
            last_g0 = s->g0;
            last_idx = s->idx;

            /* Emit RING gap before this segment if needed */
            uint32_t ring_end = s->g0 < total ? s->g0 : total;
            if (c < ring_end) {
                if (count >= cap) return -1;
                out[count].kind = WF_EXP_STEP_RING;
                out[count].a = c;
                out[count].b = ring_end;
                count++;
            }

            /* Emit SEG step */
            if (count >= cap) return -1;
            out[count].kind = WF_EXP_STEP_SEG;
            out[count].a = s->idx;
            out[count].b = 0;
            count++;

            /* Advance cursor */
            uint32_t new_c = s->g1 > c ? s->g1 : c;
            c = new_c;
        }

        /* Emit final RING gap if needed */
        if (c < total) {
            if (count >= cap) return -1;
            out[count].kind = WF_EXP_STEP_RING;
            out[count].a = c;
            out[count].b = total;
            count++;
        }
    }

    return count;
}
