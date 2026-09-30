#pragma once

/* #AUD-DUP1 (P0, Codeaudit 29.09): опора первой строки водопада после перезагрузки. Штатная перезагрузка сохраняет опору последней записанной строки (spectrogram_prepare_reboot → /storage/wf_ref.bin); загрузка берёт её, иначе — принудительный reference resync (опора = первый живой снимок, строки нет). Чистые решения — host-тестируемы. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WF_REF_MAGIC 0x31464552u /* 'REF1' */

typedef struct {
    uint32_t magic;
    uint32_t channels;
    uint32_t boot_session;  /* boot_config_get_session() of the boot that saved it */
    uint32_t prev_total;    /* s_prev_total at the last written row */
    uint32_t prev_time;     /* s_prev_time (device total_time_sec) at the last written row */
    uint32_t sum;           /* wf_ref_checksum() over the fields above and the bins */
} wf_ref_hdr_t;

typedef enum { WF_REF_USE_FILE = 0, WF_REF_FORCE_RESYNC = 1 } wf_ref_choice_t;

static inline uint32_t wf_ref_fnv_u32(uint32_t hash, uint32_t v)
{
    hash ^= (v >> 0) & 0xFF;
    hash *= 16777619u;
    hash ^= (v >> 8) & 0xFF;
    hash *= 16777619u;
    hash ^= (v >> 16) & 0xFF;
    hash *= 16777619u;
    hash ^= (v >> 24) & 0xFF;
    hash *= 16777619u;
    return hash;
}

static inline uint32_t wf_ref_checksum(const wf_ref_hdr_t *h, const uint32_t *bins, uint32_t channels)
{
    uint32_t hash = 2166136261u;
    hash = wf_ref_fnv_u32(hash, h->magic);
    hash = wf_ref_fnv_u32(hash, h->channels);
    hash = wf_ref_fnv_u32(hash, h->boot_session);
    hash = wf_ref_fnv_u32(hash, h->prev_total);
    hash = wf_ref_fnv_u32(hash, h->prev_time);
    for (uint32_t i = 0; i < channels; ++i) {
        hash = wf_ref_fnv_u32(hash, bins[i]);
    }
    return hash;
}

static inline wf_ref_choice_t wf_ref_pick(const wf_ref_hdr_t *h, const uint32_t *bins, size_t file_bytes, uint32_t channels, uint32_t cur_session)
{
    if (h == NULL) {
        return WF_REF_FORCE_RESYNC;
    }
    if (file_bytes != sizeof(wf_ref_hdr_t) + (size_t)channels * sizeof(uint32_t)) {
        return WF_REF_FORCE_RESYNC;
    }
    if (h->magic != WF_REF_MAGIC) {
        return WF_REF_FORCE_RESYNC;
    }
    if (h->channels != channels) {
        return WF_REF_FORCE_RESYNC;
    }
    if (h->boot_session == 0) {
        return WF_REF_FORCE_RESYNC;
    }
    if (h->boot_session + 1u != cur_session) {
        return WF_REF_FORCE_RESYNC;
    }
    if (bins == NULL) {
        return WF_REF_FORCE_RESYNC;
    }
    if (h->sum != wf_ref_checksum(h, bins, channels)) {
        return WF_REF_FORCE_RESYNC;
    }
    return WF_REF_USE_FILE;
}

static inline uint16_t wf_row_delta(uint32_t live, uint32_t prev, bool reset)
{
    int64_t d = (int64_t)live - (reset ? 0 : (int64_t)prev);
    if (d < 0) {
        return 0;
    }
    if (d > 65535) {
        return 65535;
    }
    return (uint16_t)d;
}

static inline const uint32_t *wf_ref_first_prev(wf_ref_choice_t c, const uint32_t *ref_bins, const uint32_t *live_bins)
{
    /* старый код брал снимок автосейва — отсюда дубль */
    if (c == WF_REF_USE_FILE) {
        return ref_bins;
    }
    return live_bins;
}

static inline bool wf_ref_first_writes_row(wf_ref_choice_t c)
{
    return c == WF_REF_USE_FILE;
}

/* #AUD-RST: Сброс не подтверждён прибором (таймаут гейта #58), набор прибора опубликован.
 * Если он продолжает опору последней строки до Сброса (ни канал, ни сумма, ни время не
 * убыли) — прибор данные сохранил, и строка пишется против этой опоры. Иначе — перенос
 * опоры без строки (У-3): против чужого набора строка дала бы скачок. Строка по этому
 * правилу никогда не больше настоящего прироста. */

static inline bool wf_rst_keeps_data(const uint32_t *pre, uint32_t pre_total, uint32_t pre_time,
                                     const uint32_t *cur, uint32_t cur_total, uint32_t cur_time,
                                     size_t channels)
{
    if (pre == NULL || cur == NULL) {
        return false;
    }

    if (cur_total < pre_total || cur_time < pre_time) {
        return false;
    }

    for (size_t i = 0; i < channels; i++) {
        if (cur[i] < pre[i]) {
            return false;
        }
    }

    return true;
}
