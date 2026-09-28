#pragma once
#include <stdint.h>

// P-009 / P-004 (sweep-A, 1.2.28): бюджет ожидания HEAVY-слота (http_io_gate)
// обработчиками httpd. Один источник значения для web_waterfall.c и web_server.c —
// раньше две независимые константы по 250 мс.
//
// КТО держит слот, пока обработчик ждёт. httpd — одна задача (HTTPD_DEFAULT_CONFIG в
// web_server.c), обработчики исполняются строго по очереди: HTTP-запрос не может ждать
// слот, занятый другим HTTP-запросом (замер 2026-08-15: 24/24 параллельных GET сегмента
// дали 200). Ждать приходится ФОНОВЫХ держателей (http_io_gate_try_enter):
// разовый автосейв current.bin (~33 КиБ, fwrite ~0,45 с — docs/bugs/2026-08-12-
// histogram-sweep-drops-autosave.md:20, плюс unlink/fopen tmp 100-160 мс), резервный
// снимок spectrum_backup_save и base.bin — та же atomic_write_snapshot. Одно такое
// удержание (~0,6-0,7 с) длиннее прежних 250 мс -> 503 (P-009, http_heavy_rejects=1).
//
// РЕШЕНИЕ: ждать с запасом x3 над оценкой худшего фонового удержания, но не дольше
// сокетного таймаута httpd минус 1 с. Пока обработчик ждёт, стоит вся задача httpd, но
// ровно столько, сколько фоновый держатель ещё держит слот (xSemaphoreTake возвращается
// сразу после Give), а не весь бюджет. 503 с Retry-After (http_io_gate.c) - страховка.

#define HTTP_GATE_BG_HOLD_EST_MS 700u /* оценка худшего фонового удержания, мс */
#define HTTP_GATE_HOLD_MARGIN_X 3u   /* запас над оценкой */
#define HTTPD_SOCK_TIMEOUT_MS 3000u  /* = config.recv/send_wait_timeout (web_server.c: 3 с) */
#define HTTP_GATE_SOCK_SLACK_MS 1000u /* зазор до сокетного таймаута */

static inline uint32_t http_gate_wait_budget_ms(uint32_t bg_hold_ms, uint32_t margin_x,
                                                uint32_t sock_timeout_ms, uint32_t slack_ms)
{
    uint64_t want = (uint64_t)bg_hold_ms * (uint64_t)margin_x;
    uint32_t cap = (sock_timeout_ms > slack_ms) ? (sock_timeout_ms - slack_ms) : 0u;
    return (want < cap) ? (uint32_t)want : cap;
}

// Итог: min(700*3, 3000-1000) = 2000 мс.
#define HTTP_GATE_WAIT_MS http_gate_wait_budget_ms(HTTP_GATE_BG_HOLD_EST_MS, HTTP_GATE_HOLD_MARGIN_X, HTTPD_SOCK_TIMEOUT_MS, HTTP_GATE_SOCK_SLACK_MS)
