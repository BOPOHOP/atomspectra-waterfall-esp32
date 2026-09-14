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

/* Взвод после открытия связи отдельно не нужен: пока гистограмм после открытия не было,
 * возраст считается от открытия (мутационная проверка 14.09 показала, что отдельное
 * условие взвода ничего не меняет). */
static inline bool acq_watch_resend_due(uint8_t intent, uint32_t now_ms, uint32_t open_ts_ms,
                                        uint32_t hist_ts_ms, uint32_t last_resend_ts_ms)
{
    if (intent != ACQ_INTENT_RUN) return false;
    uint32_t hist_age = (hist_ts_ms >= open_ts_ms) ? now_ms - hist_ts_ms : now_ms - open_ts_ms;
    if (hist_age < ACQ_WATCH_MS) return false;
    return last_resend_ts_ms == 0 || now_ms - last_resend_ts_ms >= ACQ_WATCH_MS;
}
