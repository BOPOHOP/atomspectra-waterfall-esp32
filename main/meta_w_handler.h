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
    // pass2 З-1 (совместимость с 3c73979): без w ЛЮБОЙ длины и с w не целым ("1e+21", "-5", "12.5") - прежний meta без SMA
    // (200, байты те же); 414 - только когда "w=" виден целиком, а значение упирается в конец буфера q (meta_w_parse -> META_W_CUT;
    // значение, кончающееся ровно на последнем байте буфера, неотличимо от более длинного - тоже 414, pass3 З-2). Обрезка на "&w"
    // ("&width=", "&wait=", "&wq") - w не найден, 200 без SMA (pass3 С-1). Строка длиннее буфера приходит обрезанной с
    // ESP_ERR_HTTPD_RESULT_TRUNC (IDF v5.4 httpd_parse.c: strlcpy) и разбирается по уместившейся части.
    char q[META_Q_MAX];
    size_t ql = httpd_req_get_url_query_len(req);
    uint32_t w = 0;
    meta_w_t mw = META_W_NONE;
    if (ql > 0) {
        esp_err_t qe = httpd_req_get_url_query_str(req, q, sizeof(q));
        if (qe == ESP_OK || qe == ESP_ERR_HTTPD_RESULT_TRUNC) mw = meta_w_parse(q, qe != ESP_OK, &w);
    }
    if (mw == META_W_CUT) { httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "w: query too long"); return ESP_FAIL; }
    if (mw == META_W_OK) {
        uint64_t sc = 0;
        uint32_t sd = 0, n = 0;
        monitor_window(w, &sc, &sd, &n);   // вне s_mtx кэша
        spec_dose_t dz;
        monitor_dose_live(w, &dz);   // #DOSE-A: живая МД того же окна W, что SMA (вне s_mtx кэша)
        return spectrum_http_send_meta_sma_dose(req, sc, sd, n, &dz);
    }
    return spectrum_http_send_meta(req);   // нет w / w не целое - прежний meta.json без SMA
}
