#pragma once
// WP5 (1.2.30): выгрузка журнала отладки потоком по кускам, без копии всего кольца (раньше GET /api/debug/log
// делал malloc(used+1) на ВСЁ кольцо — до 384 КБ). Срез [want_abs, end_abs) снимается под мьютексом один раз;
// каждый кусок копируется отдельно под мьютексом, и перед копированием проверяется, что нужные байты ещё в кольце.
// Абсолютный номер байта = число байт, записанных в кольцо с начала (bytes_total, монотонно). Host-pure.
#include <stddef.h>
#include <stdint.h>

typedef enum { DBGLOG_CHUNK_OK = 0, DBGLOG_CHUNK_OVERWRITTEN = 1, DBGLOG_CHUNK_DONE = 2 } dbglog_chunk_res_t;

// used — байт в кольце сейчас; off — смещение от старейшего байта кольца; len — длина куска (≤ chunk_max).
static inline dbglog_chunk_res_t dbglog_chunk_plan(uint64_t bytes_total, size_t used, uint64_t want_abs,
                                                   uint64_t end_abs, size_t chunk_max, size_t *off, size_t *len)
{
    if (want_abs >= end_abs) return DBGLOG_CHUNK_DONE;
    uint64_t oldest = bytes_total - used;
    if (want_abs < oldest) return DBGLOG_CHUNK_OVERWRITTEN;
    uint64_t n = end_abs - want_abs;
    if (n > chunk_max) n = chunk_max;
    *off = (size_t)(want_abs - oldest);
    *len = (size_t)n;
    return DBGLOG_CHUNK_OK;
}
