// AWF-5: обновление прошивки с GitHub. См. main/ota_github_client.h.
#include "ota_github_client.h"
#include "ota_github_version.h"
#include "ota_github_parse.h"
#include "ota_github_sha256sums.h"
#include "ota_github_decision.h"
#include "ota_github_redirect.h"
#include "ota_busy.h"
#include "ota_image_check.h"
#include "atomspectra.h"      // wifi_is_connected()
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "ota_gh";
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define OTA_GH_NS      "ota_gh"
#define OTA_GH_API_URL "https://api.github.com/repos/" OTA_GH_REPO_OWNER \
                        "/" OTA_GH_REPO_NAME "/releases?per_page=10"

// Кэш последнего успешного ota_gh_check(): install-задача берёт готовые
// download-URL асета и SHA256SUMS.txt, не парсит JSON заново (report по
// адресам того же релиза, что показан оператору в UI).
typedef struct {
    bool     valid;
    char     asset_url[256];
    char     sums_url[256];
    ota_gh_version_t ver;
} ota_gh_cache_t;

static SemaphoreHandle_t s_lock;            // защищает s_progress и s_cache
static ota_gh_progress_t s_progress;
static ota_gh_cache_t    s_cache;
static TaskHandle_t      s_install_task;    // NULL, когда задача не идёт

void ota_gh_client_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    memset(&s_progress, 0, sizeof(s_progress));
    memset(&s_cache, 0, sizeof(s_cache));
}

bool ota_gh_prerelease_channel_get(void)
{
    nvs_handle_t h;
    if (nvs_open(OTA_GH_NS, NVS_READONLY, &h) != ESP_OK) return false; // нет ns -> default off
    uint8_t v = 0;
    esp_err_t e = nvs_get_u8(h, "prerel", &v);
    nvs_close(h);
    return (e == ESP_OK) && v;
}

esp_err_t ota_gh_prerelease_channel_set(bool enabled)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(OTA_GH_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_u8(h, "prerel", enabled ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

ota_gh_progress_t ota_gh_get_progress(void)
{
    ota_gh_progress_t out;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    out = s_progress;
    if (s_lock) xSemaphoreGive(s_lock);
    return out;
}

static void set_progress(ota_gh_state_t st, uint32_t bytes, uint32_t total, const char *err)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_progress.state = st;
    s_progress.bytes = bytes;
    s_progress.total = total;
    if (err) snprintf(s_progress.error, sizeof(s_progress.error), "%s", err);
    else s_progress.error[0] = '\0';
    if (s_lock) xSemaphoreGive(s_lock);
}

// AWF-5 fix (найдено живым прогоном на плате): esp_http_client_open()+
// fetch_headers() БЕЗ perform() редиректы не обрабатывают -- GitHub отдаёт
// 30x на оба ассета релиза, install падал на первом же скачивании. Решение
// "продолжать/стоп" -- чистая ota_http_redirect_decide() (host-тестируема).
#define OTA_GH_MAX_REDIRECTS 10
static esp_err_t http_open_with_redirects(esp_http_client_handle_t cl, int64_t *out_clen)
{
    for (int hop = 0; ; hop++) {
        esp_err_t err = esp_http_client_open(cl, 0);
        if (err != ESP_OK) return err;
        int64_t clen = esp_http_client_fetch_headers(cl);
        int status = esp_http_client_get_status_code(cl);
        ota_http_redirect_action_t act = ota_http_redirect_decide(status, hop, OTA_GH_MAX_REDIRECTS);
        if (act == OTA_HTTP_REDIRECT_STOP_OK) { *out_clen = clen; return ESP_OK; }
        esp_http_client_close(cl);
        if (act == OTA_HTTP_REDIRECT_STOP_FAIL) return ESP_FAIL;
        // CONTINUE: Location уже распарсен esp-idf при fetch_headers() -- set_redirection
        // переставляет URL клиента на него, дальше открываем заново.
        if (esp_http_client_set_redirection(cl) != ESP_OK) return ESP_FAIL;
    }
}

// Скачивает url целиком в буфер PSRAM (heap_caps_malloc/realloc,
// MALLOC_CAP_SPIRAM), растущий по мере чтения -- на GitHub API
// Content-Length не гарантирован (chunked), на объектах релиза обычно есть,
// но не полагаемся. max_len -- защитный потолок (0 = без потолка, только
// для маленьких файлов типа SHA256SUMS.txt). Всегда завершает буфер '\0'
// (releases JSON и SHA256SUMS.txt -- текстовые форматы, наши парсеры этого
// не требуют, но лишний байт безопасен и упрощает отладочный вывод).
static esp_err_t http_get_alloc(const char *url, char **out_buf, size_t *out_len, size_t max_len)
{
    *out_buf = NULL;
    *out_len = 0;
    esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .max_redirection_count = 10,   // GitHub -> objects.githubusercontent.com
        // fix: живой прогон -- заголовки редиректа github.com (Location с подписью
        // на release-assets.githubusercontent.com + CSP/Vary/HSTS) весят 5207 Б
        // (curl -sI, живой SHA256SUMS.txt firmware-v1.2.26-rc2); .buffer_size ->
        // client->buffer_size_rx malloc'ится ОДИН РАЗ этим размером и используется
        // как верхний предел esp_transport_read() на КАЖДЫЙ вызов внутри цикла
        // esp_http_client_fetch_headers() (esp_http_client.c:1422); с 2048 цикл
        // требовал ~3 сетевых чтения на один редирект вместо одного -- запас
        // с кратным превышением измеренного размера.
        .buffer_size = 8192,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (!cl) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(cl, "User-Agent", "atomspectra-waterfall-esp32");

    int64_t clen;
    esp_err_t err = http_open_with_redirects(cl, &clen);
    if (err != ESP_OK) { esp_http_client_cleanup(cl); return err; }
    size_t cap = (clen > 0) ? (size_t)clen + 1 : 4096;
    if (max_len && cap > max_len) cap = max_len + 1;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) { esp_http_client_close(cl); esp_http_client_cleanup(cl); return ESP_ERR_NO_MEM; }
    size_t len = 0;
    for (;;) {
        if (max_len && len + 1 >= max_len) { free(buf); goto toolarge; }
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            if (max_len && ncap > max_len + 1) ncap = max_len + 1;
            char *nb = heap_caps_realloc(buf, ncap, MALLOC_CAP_SPIRAM);
            if (!nb) { free(buf); goto oom; }
            buf = nb; cap = ncap;
        }
        int rd = esp_http_client_read(cl, buf + len, (int)(cap - 1 - len));
        if (rd < 0) { free(buf); goto ioerr; }
        if (rd == 0) break;
        len += (size_t)rd;
    }
    buf[len] = '\0';
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    *out_buf = buf; *out_len = len;
    return ESP_OK;
oom:    esp_http_client_close(cl); esp_http_client_cleanup(cl); return ESP_ERR_NO_MEM;
ioerr:  esp_http_client_close(cl); esp_http_client_cleanup(cl); return ESP_FAIL;
toolarge: esp_http_client_close(cl); esp_http_client_cleanup(cl); return ESP_ERR_INVALID_SIZE;
}

static void fmt_version(const ota_gh_version_t *v, char *out, size_t cap)
{
    if (v->has_rc) snprintf(out, cap, "%" PRIu32 ".%" PRIu32 ".%" PRIu32 "-rc%" PRIu32,
                             v->major, v->minor, v->patch, v->rc);
    else snprintf(out, cap, "%" PRIu32 ".%" PRIu32 ".%" PRIu32, v->major, v->minor, v->patch);
}

// Текущая версия прошивки (esp_app_desc_t.version, из PROJECT_VER на этапе
// сборки -- main/CMakeLists.txt/CMakeLists.txt не хардкодят её) в формате
// нашего парсера ("firmware-v" + версия).
static bool current_version(ota_gh_version_t *out)
{
    const esp_app_desc_t *d = esp_app_get_description();
    if (!d) return false;
    char tag[80];
    snprintf(tag, sizeof(tag), "firmware-v%s", d->version);
    return ota_gh_version_parse_tag(tag, out);
}

esp_err_t ota_gh_check(char *out_json, size_t out_cap)
{
    ota_gh_version_t cur;
    bool have_cur = current_version(&cur);
    char cur_str[32];
    if (have_cur) fmt_version(&cur, cur_str, sizeof(cur_str));
    else snprintf(cur_str, sizeof(cur_str), "?");

    if (!wifi_is_connected()) {
        snprintf(out_json, out_cap,
            "{\"current\":\"%s\",\"latest\":\"\",\"newer\":false,"
            "\"installable\":false,\"reason\":\"no_internet\",\"html_url\":\"\"}", cur_str);
        return ESP_OK;
    }

    char *buf; size_t len;
    esp_err_t err = http_get_alloc(OTA_GH_API_URL, &buf, &len, 512 * 1024);
    if (err != ESP_OK) {
        snprintf(out_json, out_cap,
            "{\"current\":\"%s\",\"latest\":\"\",\"newer\":false,"
            "\"installable\":false,\"reason\":\"network_error\",\"html_url\":\"\"}", cur_str);
        return ESP_OK;
    }

    bool want_pre = ota_gh_prerelease_channel_get();
    const char *os, *oe;
    ota_gh_version_t best;
    bool found = ota_gh_releases_pick_best(buf, len, want_pre, &os, &oe, &best);
    if (!found) {
        free(buf);
        snprintf(out_json, out_cap,
            "{\"current\":\"%s\",\"latest\":\"\",\"newer\":false,"
            "\"installable\":false,\"reason\":\"no_releases\",\"html_url\":\"\"}", cur_str);
        return ESP_OK;
    }

    ota_gh_version_t min_ver;
    ota_gh_version_parse_tag(OTA_GH_MIN_INSTALLABLE_TAG, &min_ver);  // литерал, всегда валиден
    ota_gh_decision_t dec = ota_gh_decide(&best, &cur, have_cur, &min_ver);  // P2-фикс: host-тестируемо
    bool newer = dec.newer, installable = dec.installable;
    const char *reason = dec.reason;

    char latest_str[32];
    fmt_version(&best, latest_str, sizeof(latest_str));
    // #AWF-6 (оператор): ссылка на страницу релиза рядом с результатом проверки в UI.
    size_t html_url_len = 0;
    const char *html_url = ota_gh__string_field(os, oe, "\"html_url\"", &html_url_len);
    char html_url_buf[200] = "";
    if (html_url) {
        size_t n = html_url_len < sizeof(html_url_buf) - 1 ? html_url_len : sizeof(html_url_buf) - 1;
        memcpy(html_url_buf, html_url, n); html_url_buf[n] = '\0';
    }
    size_t asset_len, sums_len;
    const char *asset_url = ota_gh_find_asset_url(os, oe, "atomspectra_gw.bin", &asset_len);
    const char *sums_url  = ota_gh_find_asset_url(os, oe, "SHA256SUMS.txt", &sums_len);
    if (installable && (!asset_url || !sums_url)) {
        installable = false;
        reason = "release_missing_assets";
    }

    if (installable && s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        size_t n1 = asset_len < sizeof(s_cache.asset_url) - 1 ? asset_len : sizeof(s_cache.asset_url) - 1;
        size_t n2 = sums_len  < sizeof(s_cache.sums_url)  - 1 ? sums_len  : sizeof(s_cache.sums_url)  - 1;
        memcpy(s_cache.asset_url, asset_url, n1); s_cache.asset_url[n1] = '\0';
        memcpy(s_cache.sums_url,  sums_url,  n2); s_cache.sums_url[n2]  = '\0';
        s_cache.ver = best;
        s_cache.valid = true;
        xSemaphoreGive(s_lock);
    }
    free(buf);

    snprintf(out_json, out_cap,
        "{\"current\":\"%s\",\"latest\":\"%s\",\"newer\":%s,"
        "\"installable\":%s,\"reason\":\"%s\",\"html_url\":\"%s\"}",
        cur_str, latest_str, newer ? "true" : "false",
        installable ? "true" : "false", reason, html_url_buf);
    return ESP_OK;
}

static void install_fail(esp_ota_handle_t ota, const char *reason)
{
    if (ota) esp_ota_abort(ota);
    ota_busy_release(OTA_BUSY_GITHUB);  // P1-фикс: no-op, если acquire ещё не звали
    set_progress(OTA_GH_ST_ERROR, 0, 0, reason);
}

// Фоновая задача: любой сбой -> install_fail() (esp_ota_abort, слот
// загрузки НЕ меняется, реболута нет) -- по требованию задачи #AWF-5 п.2.
static void install_task(void *arg)
{
    (void)arg;
    set_progress(OTA_GH_ST_CHECKING, 0, 0, NULL);

    ota_gh_cache_t cache;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    cache = s_cache;
    if (s_lock) xSemaphoreGive(s_lock);
    if (!cache.valid) {
        char tmp[256];
        ota_gh_check(tmp, sizeof(tmp));
        if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
        cache = s_cache;
        if (s_lock) xSemaphoreGive(s_lock);
    }
    if (!cache.valid) { install_fail(0, "no_release_cached"); goto done; }

    set_progress(OTA_GH_ST_DOWNLOADING, 0, 0, NULL);
    char *sums_buf; size_t sums_len;
    if (http_get_alloc(cache.sums_url, &sums_buf, &sums_len, 64 * 1024) != ESP_OK) {
        install_fail(0, "sums_download_failed"); goto done;
    }
    char expect_hex[65];
    bool have_hash = ota_gh_sha256sums_find(sums_buf, sums_len, "atomspectra_gw.bin", expect_hex);
    free(sums_buf);
    if (!have_hash) { install_fail(0, "sha256sums_missing_entry"); goto done; }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) { install_fail(0, "no_ota_partition"); goto done; }
    // P1-фикс (verify-awf5-github-ota-2026-09-27.md разд.2.4): общий замок с
    // ручной /api/ota -- ДО esp_ota_begin, иначе оба пути стирают/пишут один
    // и тот же неактивный слот одновременно (esp_ota_get_next_update_partition
    // не резервирует раздел сама по себе).
    if (!ota_busy_acquire(OTA_BUSY_GITHUB)) { install_fail(0, "ota_busy"); goto done; }
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(update, OTA_SIZE_UNKNOWN, &ota) != ESP_OK) {
        install_fail(0, "ota_begin_failed"); goto done;
    }

    esp_http_client_config_t hc = {
        .url = cache.asset_url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .max_redirection_count = 10,
        .buffer_size = 8192,   // fix -- см. комментарий у http_get_alloc(), тот же механизм
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (!cl) { install_fail(ota, "oom"); goto done; }
    esp_http_client_set_header(cl, "User-Agent", "atomspectra-waterfall-esp32");
    int64_t clen;
    if (http_open_with_redirects(cl, &clen) != ESP_OK) {   // AWF-5 fix -- см. http_open_with_redirects
        esp_http_client_cleanup(cl); install_fail(ota, "asset_http_status"); goto done;
    }
    if (clen > 0 && (size_t)clen > update->size) {
        esp_http_client_close(cl); esp_http_client_cleanup(cl);
        install_fail(ota, "image_too_large_for_partition"); goto done;
    }
    uint8_t *buf = malloc(4096);
    if (!buf) {
        esp_http_client_close(cl); esp_http_client_cleanup(cl);
        install_fail(ota, "oom"); goto done;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    ota_image_walker_t walker;          // #2: переиспользуем структурную сверку /api/ota
    ota_image_walker_init(&walker);
    bool header_checked = false, image_ok = true;
    uint32_t received = 0;
    for (;;) {
        int rd = esp_http_client_read(cl, (char *)buf, 4096);
        if (rd < 0) { image_ok = false; break; }
        if (rd == 0) break;
        if (!header_checked) {
            header_checked = true;
            if (!ota_image_header_is_valid(buf, (size_t)rd, ESP_CHIP_ID_ESP32S3)) {
                image_ok = false; break;
            }
        }
        if (!ota_image_walker_feed(&walker, buf, (size_t)rd)) { image_ok = false; break; }
        mbedtls_sha256_update(&sha, buf, (size_t)rd);
        if (esp_ota_write(ota, buf, rd) != ESP_OK) { image_ok = false; break; }
        received += (uint32_t)rd;
        set_progress(OTA_GH_ST_DOWNLOADING, received, clen > 0 ? (uint32_t)clen : 0, NULL);
    }
    free(buf);
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    if (!image_ok) { mbedtls_sha256_free(&sha); install_fail(ota, "download_or_image_error"); goto done; }
    if (!ota_image_walker_is_complete(&walker)) {
        mbedtls_sha256_free(&sha); install_fail(ota, "incomplete_image"); goto done;
    }

    set_progress(OTA_GH_ST_VERIFYING, received, received, NULL);
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    char got_hex[65];
    for (int i = 0; i < 32; i++) snprintf(got_hex + i * 2, 3, "%02x", digest[i]);
    if (strcasecmp(got_hex, expect_hex) != 0) {
        install_fail(ota, "sha256_mismatch"); goto done;   // ДО esp_ota_set_boot_partition, п.2
    }

    set_progress(OTA_GH_ST_INSTALLING, received, received, NULL);
    if (esp_ota_end(ota) != ESP_OK) { install_fail(0, "ota_end_failed"); goto done; }
    if (esp_ota_set_boot_partition(update) != ESP_OK) {
        install_fail(0, "set_boot_partition_failed"); goto done;
    }
    set_progress(OTA_GH_ST_DONE, received, received, NULL);
    ESP_LOGW(TAG, "AWF-5: installed %s (%" PRIu32 " bytes) from GitHub, rebooting",
             update->label, received);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();

done:
    // P2-фикс (verify-awf5-github-ota-2026-09-27.md разд.2.3): s_install_task
    // читалось/писалось вне s_lock, хотя мьютекс для s_progress/s_cache уже есть.
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_install_task = NULL;
    if (s_lock) xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

esp_err_t ota_gh_install_start(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = (s_install_task != NULL);
    if (s_lock) xSemaphoreGive(s_lock);
    if (busy) return ESP_ERR_INVALID_STATE;  // уже идёт -- идемпотентно

    TaskHandle_t h = NULL;
    BaseType_t ok = xTaskCreatePinnedToCore(install_task, "ota_gh_install", 8192, NULL, 4, &h, 1);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_install_task = h;   // пишем ПОСЛЕ успешного создания, не как out-param гонки с install_task()
    if (s_lock) xSemaphoreGive(s_lock);
    return ESP_OK;
}
