#pragma once
/* З-3 (release-gate-1.2.36-code-pass2): обработчик GET /api/spectrum/meta.json вынесен из web_server.c в заголовок, чтобы
 * host-тест склейки tests/host/test_glue_meta_w.c исполнял САМ обработчик (коды ответа, байты тела, окно SMA) с заглушками IDF.
 * Подключается ровно одним .c прошивки (web_server.c). */
#include <stdint.h>
#include "esp_http_server.h"
#include "meta_w_parse.h"
#include "monitor.h"
#include "spectrum_http_cache.h"

static esp_err_t meta_w_handle(httpd_req_t *req)
{
    // #PERF-1: meta without 8192 bins (system/monitor tiles)
    // 1.2.36 #MX-14: ?w=<секунд наборного времени> - к meta добавляется SMA окна W из кольца платы (ответ идёт чанками);
    // без w - прежняя отправка с Content-Length, байты те же. З-5 (gate pass1): разбор meta_w_parse.h - длинный запрос 414,
    // w не из одних цифр 400 (раньше оба случая молча отдавали meta без SMA, а "-5"/"+5"/" 5" принимались).
    char q[META_Q_MAX];
    size_t ql = httpd_req_get_url_query_len(req);
    if (ql >= sizeof(q)) { httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "query too long"); return ESP_FAIL; }
    uint32_t w = 0;
    meta_w_t mw = (ql > 0 && httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) ? meta_w_parse(q, &w) : META_W_NONE;
    if (mw == META_W_BAD) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "w: decimal seconds"); return ESP_FAIL; }
    if (mw == META_W_OK) {
        uint64_t sc = 0;
        uint32_t sd = 0, n = 0;
        monitor_window(w, &sc, &sd, &n);   // вне s_mtx кэша
        return spectrum_http_send_meta_sma(req, sc, sd, n);
    }
    return spectrum_http_send_meta(req);
}
