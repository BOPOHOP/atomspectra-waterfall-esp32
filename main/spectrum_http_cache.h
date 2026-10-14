#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "atomspectra.h"

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

// Send meta JSON without bins (cps, total, calib, …).
esp_err_t spectrum_http_send_meta(httpd_req_t *req);

// 1.2.36 #MX-14: то же тело meta + хвост SMA ,"sma":..,"sma_w":..,"sma_n":..} (окно считает monitor_window(), вне мьютекса кэша).
// Ответ чанками (httpd_resp_send_chunk): без Content-Length. Заголовки X-Spectrum-Render-Count / Cache-Control те же.
esp_err_t spectrum_http_send_meta_sma(httpd_req_t *req, uint64_t sum_counts, uint32_t sum_dur, uint32_t n);
