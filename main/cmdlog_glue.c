/* ===== FILE: cmdlog_glue.c ===== */

#ifdef AWF_CMDLOG

#include "cmdlog.h"
#include "cmdlog_glue.h"
#include "atomspectra.h"
#include "boot_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static cmdlog_t s_log;
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_buf;
static bool s_started;

static uint32_t s_prev_time, s_prev_counts, s_prev_base_time, s_prev_base_counts, s_prev_dev_resets, s_prev_session;
static int s_prev_tcp;
static uint32_t s_last_state_log_ms;

static bool s_mutex_valid;

static void state_timer_cb(void *arg) {
    (void)arg;
    uint32_t counts, time_sec, resync_seq;
    uint32_t base_time, base_counts, dev_resets;
    bool tcp = tcp_bridge_client_connected();
    uint32_t session = boot_config_get_session();

    spectrum_get_totals(&counts, &time_sec, &resync_seq);
    spectrum_get_base_info(&base_time, &base_counts, &dev_resets);

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    bool changed = (time_sec != s_prev_time) || (counts != s_prev_counts) ||
                   (base_time != s_prev_base_time) || (base_counts != s_prev_base_counts) ||
                   (dev_resets != s_prev_dev_resets) || (session != s_prev_session) ||
                   (tcp != s_prev_tcp);
    bool force = (now_ms - s_last_state_log_ms) >= 30000;

    if (changed || force) {
        ESP_LOGI("STATE", "time=%u counts=%u base_time=%u base_counts=%u dev_resets=%u session=%u tcp=%d",
                 (unsigned)time_sec, (unsigned)counts, (unsigned)base_time, (unsigned)base_counts,
                 (unsigned)dev_resets, (unsigned)session, (int)tcp);
        s_prev_time = time_sec;
        s_prev_counts = counts;
        s_prev_base_time = base_time;
        s_prev_base_counts = base_counts;
        s_prev_dev_resets = dev_resets;
        s_prev_session = session;
        s_prev_tcp = tcp;
        s_last_state_log_ms = now_ms;
    }
}

uint32_t cmdlog_glue_record(uint8_t src, const char *text, size_t len, int term_nul) {
    if (!s_mutex_valid) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t seq = cmdlog_push(&s_log, uptime_ms, src, text, len, term_nul);
    xSemaphoreGive(s_mutex);

    char buf[48];
    size_t n = (len > 47) ? 47 : len;
    for (size_t i = 0; i < n; i++) {
        char c = text[i];
        buf[i] = (c >= 0x20 && c <= 0x7e) ? c : '.';
    }
    buf[n] = '\0';
    ESP_LOGI("CMDLOG", "seq=%u src=%u nul=%d cmd=\"%s\"", (unsigned)seq, (unsigned)src, term_nul, buf);
    return seq;
}

void cmdlog_glue_result(uint32_t seq, int delivered, int rc) {
    if (!s_mutex_valid) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    cmdlog_set_result(&s_log, seq, delivered, rc);
    xSemaphoreGive(s_mutex);
    ESP_LOGI("CMDLOG", "seq=%u delivered=%d rc=%d", (unsigned)seq, delivered, rc);
}

size_t cmdlog_glue_json(uint32_t since, char *out, size_t cap) {
    if (!s_mutex_valid) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    size_t len = cmdlog_json(&s_log, since, out, cap);
    xSemaphoreGive(s_mutex);
    return len;
}

void cmdlog_glue_start(void) {
    if (s_started) return;
    s_started = true;

    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    s_mutex_valid = (s_mutex != NULL);
    cmdlog_init(&s_log);

    esp_timer_create_args_t args = {
        .callback = state_timer_cb,
        .arg = NULL,
        .name = "cmdlog_state"
    };
    esp_timer_handle_t timer;
    esp_timer_create(&args, &timer);
    esp_timer_start_periodic(timer, 2 * 1000 * 1000);
}

uint8_t cmdlog_glue_src_from_task(void) {
    const char *name = pcTaskGetName(NULL);
    if (name && strncmp(name, "httpd", 5) == 0) return 1;
    return 5;
}

#endif /* AWF_CMDLOG */
