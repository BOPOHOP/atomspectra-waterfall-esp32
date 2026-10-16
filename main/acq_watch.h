#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Сторож набора (S-C02 14.09). После перезагрузки прибора набор встаёт, а RX-сторож молчит:
 * FTDI шлёт 2-байтовый статус. Одного повторного -sta достаточно (проверено на плате).
 * Только при намерении RUN от самого шлюза: после -sto или TCP-клиента — молчит. */
#define ACQ_INTENT_UNKNOWN 0u
#define ACQ_INTENT_RUN     1u
#define ACQ_INTENT_STOP    2u
#define ACQ_WATCH_MS       20000u  /* порог тишины и шаг повтора */

/* Только разности штампов (переполнение uint32 мс безопасно); гистограмма старше открытия — от открытия. */
static inline bool acq_watch_resend_due(uint8_t intent, uint32_t now_ms, uint32_t open_ts_ms,
                                        uint32_t hist_ts_ms, uint32_t last_resend_ts_ms)
{
    if (intent != ACQ_INTENT_RUN) return false;
    uint32_t open_age = now_ms - open_ts_ms, hist_age = now_ms - hist_ts_ms;
    if (hist_age > open_age) hist_age = open_age;
    if (hist_age < ACQ_WATCH_MS) return false;
    return now_ms - last_resend_ts_ms >= ACQ_WATCH_MS;
}

/* 1.2.37: возраст последней принятой гистограммы, мс (заголовок X-Hist-Age-Ms). -1 — ни одной с загрузки (штамп 0, как в
 * acq_watch_resend_due). Разность uint32 — переполнение мс безопасно; потолок INT32_MAX, чтобы значение не стало отрицательным. */
static inline int32_t acq_hist_age_ms(uint32_t now_ms, uint32_t hist_ts_ms)
{
    if (hist_ts_ms == 0) return -1;
    uint32_t age = now_ms - hist_ts_ms;
    return age > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)age;
}
