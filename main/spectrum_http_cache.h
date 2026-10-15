#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "atomspectra.h"
#include "dose_api.h"       // #DOSE-A: spec_dose_t

// Shared 2s spectrum snapshot for LIVE HTTP (JSON / binary / meta).
// Single-flight refresh: N concurrent tabs share one render.

#define SPECTRUM_HTTP_CACHE_TTL_US  (2000000LL)  // 2 s

void spectrum_http_cache_init(void);

// Refresh cache if stale; returns false if no spectrum yet.
bool spectrum_http_cache_ensure(void);

// #AWF-12b R5 (release-gate-1.2.28-code-rc2.md): форсировать пересборку на
// следующем ensure() — калибровка сменилась (дамп прибора/POST /api/calibration/
// загрузка calib.bin), 2s TTL иначе отдавал бы старый calib_set/calib ещё
// до пары секунд. Безопасно звать откуда угодно (в т.ч. под SPEC_LOCK).
void spectrum_http_cache_invalidate(void);

// Pointers valid until next ensure that rebuilds (call under no long sleep).
// Prefer copying bins/meta for send, or send while holding via helpers below.
const spectrum_data_t *spectrum_http_cache_data(void);

uint32_t spectrum_http_cache_render_count(void);

// 1.2.36 Commit 0: цена последней / максимальной пересборки (снимок + сборка JSON), мкс.
uint32_t spectrum_http_cache_build_us_last(void);
uint32_t spectrum_http_cache_build_us_max(void);
// 1.2.36 B: число ленивых сборок полного spectrum.json (растёт только когда его запрашивают).
uint32_t spectrum_http_cache_json_builds(void);

// Send cached full JSON (builds once per TTL). Adds X-Spectrum-Render-Count.
esp_err_t spectrum_http_send_json(httpd_req_t *req);

// Send bins only (uint32 LE × SPECTRUM_CHANNELS).
esp_err_t spectrum_http_send_bins(httpd_req_t *req);

// #GZ-T (1.2.37-rc90, ТЕСТ): сжатие тел спектра отдельной задачей (main/spectrum_gz.c). Виды тела:
#define SPECTRUM_HTTP_KIND_JSON 0   // полный /api/spectrum.json
#define SPECTRUM_HTTP_KIND_BINS 1   // бинарный /api/spectrum (32 768 Б)
// Крючок обработчика: true — ответ уже отправлен (готовое сжатое тело), *out — результат отправки; false — ответить как раньше.
typedef bool (*spectrum_http_gz_try_fn)(httpd_req_t *req, int kind, esp_err_t *out);
// Уведомление: тело вида kind текущего поколения собрано (зовётся под мьютексом кэша — только уведомить, не ждать).
typedef void (*spectrum_http_gz_note_fn)(int kind);
// Ставит spectrum_gz_init() один раз до httpd_start. NULL (по умолчанию) — путь ответа прежний.
void spectrum_http_cache_set_gz_hooks(spectrum_http_gz_try_fn try_send, spectrum_http_gz_note_fn note);
// Закрепить тело вида kind ТЕКУЩЕГО поколения как читатель кэша (JSON — только если уже собран для текущего поколения):
// пересборка не освободит и не перезапишет его до spectrum_http_cache_unpin(kind). false — тела нет.
bool spectrum_http_cache_pin(int kind, const void **body, size_t *len, uint32_t *gen);
void spectrum_http_cache_unpin(int kind);

// Send meta JSON without bins (cps, total, calib, …).
esp_err_t spectrum_http_send_meta(httpd_req_t *req);

// 1.2.36 #MX-14: то же тело meta + хвост SMA ,"sma":..,"sma_w":..,"sma_n":..} (окно считает monitor_window(), вне мьютекса кэша).
// Ответ чанками (httpd_resp_send_chunk): без Content-Length. Заголовки X-Spectrum-Render-Count / Cache-Control те же.
esp_err_t spectrum_http_send_meta_sma(httpd_req_t *req, uint64_t sum_counts, uint32_t sum_dur, uint32_t n);

// #DOSE-A: то же, что spectrum_http_send_meta_sma(), плюс хвост живой МД ,"dose_rate":<число|null>,"dose_mode":"curve|scalar|none","dose_w":<с>
// после sma_n (один закрывающий '}' в конце). dose == NULL — поведение spectrum_http_send_meta_sma() байт-в-байт.
esp_err_t spectrum_http_send_meta_sma_dose(httpd_req_t *req, uint64_t sum_counts, uint32_t sum_dur, uint32_t n, const spec_dose_t *dose);
