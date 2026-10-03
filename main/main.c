#include "atomspectra.h"
#include "spectrogram.h"
#include "boot_config.h"
#include "session_plan.h"   // 1.2.31: новая сессия платы после Сброса спектра
#include "wf_offload.h"   // #REC-11-A2: автономная выгрузка сегментов водопада
#include "monitor.h"      // #MON-1: серия CPS-мониторинга на плате
#include "ota_github_client.h"   // AWF-5
#include "ota_busy.h"            // AWF-5 P1-фикс
#include "net_time.h"     // #FIELD-5: источник времени (SNTP/браузер/ручной)
#include "debug_log_ring.h"
#include "hist_drop_diag.h"
#include "flash_quiet.h"
#include "http_io_gate.h"  // issue #52: снимок пишется под тем же гейтом, что и «Сохранить»
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "esp_sntp.h"
#include <inttypes.h>
#include <sys/time.h>
#include "esp_timer.h"
#include "esp_ota_ops.h"  // AWF-4: mark_app_valid_cancel_rollback после успешного старта
#include "ota_mark_valid_plan.h"   // D3: годность образа не только по Wi-Fi (host-тест)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"   // #FW-13 фикс №2: ожидание коммита свипа перед autosave

static const char *TAG = "main";

// #FW-23: usb_host_cdc_init() (boot-автозапуск водопада) стартует раньше SNTP —
// started_at мог зафиксироваться near-epoch. На первый успешный sync — пересчитать.
static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    net_time_mark_sntp();      // #FIELD-5 (A2): зафиксировать факт реальной SNTP-синхронизации
    spectrogram_time_synced();
}

// Гейт 1.2.29: int_min (минимум внутренней RAM) падал до 231 Б. Узлы cJSON мелкие
// (< SPIRAM_MALLOC_ALWAYSINTERNAL) и шли во внутреннюю RAM, которой нужен Wi-Fi (#FW-50):
// JSON всех ответов — в PSRAM, внутренняя — запасной путь. Отказы аллокации — счётчик.
static uint32_t s_cjson_spill, s_alloc_fail_n, s_alloc_fail_size, s_alloc_fail_caps;
static __thread bool t_cjson_try;   // промах PSRAM у cJSON — не отказ: есть запасной путь
static void *cjson_psram_malloc(size_t sz)
{
    t_cjson_try = true;
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    t_cjson_try = false;
    if (!p) {   // F-7: уход во внутреннюю RAM считаем отдельно
        __atomic_fetch_add(&s_cjson_spill, 1, __ATOMIC_RELAXED);
        p = heap_caps_malloc(sz, MALLOC_CAP_DEFAULT);
    }
    return p;
}
static void alloc_failed_cb(size_t size, uint32_t caps, const char *fn)
{
    (void)fn;
    if (t_cjson_try) return;   // колбэк зовётся в контексте той же задачи
    __atomic_fetch_add(&s_alloc_fail_n, 1, __ATOMIC_RELAXED);   // F-7: два ядра
    __atomic_store_n(&s_alloc_fail_size, (uint32_t)size, __ATOMIC_RELAXED);
    __atomic_store_n(&s_alloc_fail_caps, caps, __ATOMIC_RELAXED);
}
void mem_diag_get(uint32_t *n, uint32_t *last_size, uint32_t *last_caps, uint32_t *cjson_spill)
{
    *n = __atomic_load_n(&s_alloc_fail_n, __ATOMIC_RELAXED);
    *last_size = __atomic_load_n(&s_alloc_fail_size, __ATOMIC_RELAXED);   // пара size/caps — последнего
    *last_caps = __atomic_load_n(&s_alloc_fail_caps, __ATOMIC_RELAXED);   // отказа, без гарантии пары
    *cjson_spill = __atomic_load_n(&s_cjson_spill, __ATOMIC_RELAXED);
}

static void init_sntp(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
}

void app_main(void)
{
    ESP_LOGI(TAG, "AtomSpectra Gateway starting...");
    cJSON_Hooks cj_hooks = { .malloc_fn = cjson_psram_malloc, .free_fn = free };
    cJSON_InitHooks(&cj_hooks);   // до первого cJSON: узлы и буферы печати — в PSRAM
    heap_caps_register_failed_alloc_callback(alloc_failed_cb);

    wifi_manager_init();

    // #FW-2/#FW-3: настройки поведения при старте платы (NVS, по умолчанию всё OFF).
    // #FW-44: весь USB-стек (boot_config → spectrum → spectrogram → usb_host_cdc)
    // поднят ДО AP-проверки. Раньше свежая плата (wifi не настроен) уходила в
    // немой while(1){vTaskDelay} ещё до usb_host_cdc_init() → USB Host не стартовал,
    // консоль давала лишь 3 boot-строки и молчала навсегда. Теперь USB работает
    // независимо от WiFi — спектрометр определяется и диагностируется в AP-режиме.
    boot_config_t bc;
    boot_config_load(&bc);
    ESP_LOGI(TAG, "boot-config: as_spec=%d as_wf=%d clr_spec=%d clr_wf=%d",
             bc.autostart_spectrum, bc.autostart_waterfall, bc.clear_spectrum, bc.clear_waterfall);
    // issue #52: номер сессии платы — ровно один инкремент за загрузку. Снимки
    // этой сессии получат новое имя, поэтому после пропадания питания они не
    // смешиваются со снятыми до него. Инкремент отложен до spectrum_init()
    // (нужен смонтированный LittleFS, чтобы узнать максимум по уже лежащим
    // снимкам) — см. ниже, после spectrum_init().
    sess_sched_t ss = {0};   // 1.2.31: номер сессии и планировщик снимков (session_plan.h)

    flash_quiet_init();
    spectrum_init();
    // issue #52: теперь LittleFS смонтирован — можно взять максимум по снимкам и
    // не дать счётчику сессий откатиться назад после стирания NVS.
    ss.sess = boot_config_bump_session(spectrum_backup_max_session());
    if (ss.sess == 0)
        ESP_LOGE(TAG, "backups disabled this boot: no usable session number");
    spectrum_restore_autosave();
    spectrum_restore_base();   // AWF-3: база — после D, до clr_spec (тот сам чистит обе)
    spectrum_load_calibration();
    // #FW-3: очистка накопленного спектра при старте — после restore, до того как
    // спектрограмма снимет baseline. -rst прибору пошлётся на первом USB-коннекте.
    // Н-3.3 (release-gate 1.2.29): метка reset.mark 'P' — Сброс до перезагрузки, а
    // -rst до прибора не дошёл. Сброс повторяется, как FW-3: гейт #58 взведён,
    // -rst уйдёт на первом коннекте.
    bool reset_mark = spectrum_reset_mark_undelivered();
    if (bc.clear_spectrum || reset_mark) {
        spectrum_reset_undelivered();
        spectrum_autosave_consume_abort();
        usb_host_cdc_request_rst();   // FW-3 и метка 'P': -rst на первом коннекте
        ESP_LOGW(TAG, "%s: accumulated spectrum cleared on boot",
                 bc.clear_spectrum ? "FW-3" : "reset.mark");
    }
    ss.seen_req = spectrum_session_req();   // очистка при загрузке уже покрыта бампом загрузки
    // #FW-50: PSRAM log ring — after spectrum_init, before spectrogram (reserve before WF).
    debug_log_ring_boot();
    spectrogram_init();
    // #FW-3: очистка водопада при старте — ДО spectrogram_restore(), иначе restore
    // возобновит прежнюю запись из сохранённого состояния.
    if (bc.clear_waterfall) {
        int clr = spectrogram_clear();
        if (clr != 0)
            ESP_LOGW(TAG, "FW-3/FW-65: waterfall clear on boot failed (%d)", clr);
        else
            ESP_LOGW(TAG, "FW-3: waterfall cleared on boot");
    }
    spectrogram_restore();   // #REC-6: возобновить запись после ребута/сбоя питания
    // #FW-2: передать флаги автозапуска в USB-модуль ДО его инициализации.
    usb_host_cdc_set_autostart(bc.autostart_spectrum, bc.autostart_waterfall, bc.clear_spectrum);
    usb_host_cdc_init();

    // #FW-44/#FIELD-1: setup-портал (свежая плата без wifi-конфига) — диагностический
    // цикл, сеть-зависимые подсистемы не поднимаем (setup-httpd держится внутри
    // wifi_manager). USB Host уже поднят выше → логируем реальную диагностику вместо
    // прежней немоты. Выход — только перезагрузка после сохранения wifi/выбора режима.
    net_run_mode_t net_mode = wifi_manager_mode();
    if (net_mode == NET_MODE_SETUP) {
        ESP_LOGI(TAG, "Setup captive portal active, waiting for WiFi config");
        while (1) {
            const spectrum_data_t *sp = spectrum_get_current();
            ESP_LOGI(TAG, "SETUP: connect SSID=AtomSpectra-Setup -> http://192.168.4.1 | USB:%s spectrometer:%s counts:%" PRIu32,
                usb_host_cdc_is_connected() ? "OK" : "--",
                usb_host_cdc_spectrometer_dead() ? "DEAD"
                    : (usb_host_cdc_is_connected() ? "streaming" : "--"),
                sp->total_counts);
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    // #FIELD-1: рабочий сетевой стек — общий для Indoor (STA) и Outdoor (полевой AP).
    ota_busy_init();         // AWF-5 P1-фикс: до обоих OTA-путей
    ota_gh_client_init();   // AWF-5: до web_server_init — эндпоинты уже могут читать s_progress
    web_server_init();
    monitor_init();      // #MON-1: кольцо серии CPS (6 ч в PSRAM) + задача-подписчик коммитов
    tcp_bridge_init();
    if (net_mode == NET_MODE_STA) {
        wf_offload_init();   // #REC-11-A2: аплоадер сегментов — только Indoor (есть сеть/приёмник)
        init_sntp();          // SNTP-время — только Indoor (есть интернет)
    } else {
        // #FIELD-1: Outdoor (полевой AP) — интернета/приёмника нет: SNTP и offload
        // не поднимаем. Время платы приходит от браузера телефона (#FIELD-5,
        // POST /api/time); данные водопада копятся на flash, забираются дома.
        ESP_LOGI(TAG, "FIELD-1: Outdoor AP — SNTP & offload disabled (time via browser)");
    }

    ESP_LOGI(TAG, "All subsystems initialized");

    // #FW-13 фикс №2: autosave (32.8 КБ LittleFS = freeze кэша обоих ядер) фазово
    // привязывается к коммиту свипа — запись уходит в тихое USB-окно (~0.5 с) сразу
    // после конца burst, а не в случайную фазу, где рвала приём FTDI.
    SemaphoreHandle_t autosave_sig = xSemaphoreCreateBinary();
    if (autosave_sig) spectrum_add_commit_listener(autosave_sig);

    int info_tick = 0, autosave_tick = 0;
    // issue #52: планировщик резервных снимков. Срок следующего снимка — в
    // микросекундах монотонного таймера, а не в числе итераций (тело цикла
    // блокирующее, длительность итерации плавает). Счётчик снимков живёт в RAM:
    // после перезагрузки нумерация начинается заново, но имя файла несёт ещё и
    // номер сессии — коллизии нет.
    // seq, fail_streak и due_us (0 = срок ещё не назначен) живут в ss (session_plan.h).
    int      backup_cfg_tick = 6;        // 6 = перечитать настройки на первом же тике
    boot_config_t backup_cfg = bc;       // стартуем от прочитанного на boot
    // AWF-2a (#2): проверка возврата из fallback Field AP — раз в 15 тиков
    // (10с*15=150с, ~2.5 мин); функция сама no-op вне Field AP/при клиентах.
    int wifi_return_tick = 0;
    // AWF-4: rollback-подтверждение образа после Wi-Fi OTA. httpd уже поднят
    // (web_server_init() выше, безусловно); ждём именно Wi-Fi (в Outdoor/Field
    // AP wifi_is_connected() истинно, когда есть клиент, — тоже валидный "жив").
    bool ota_valid_marked = false;
    uint32_t seconds_since_boot = 0;   // D3: тик main() — доказательство, что цикл жив
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        seconds_since_boot += 10;
        // D3 (P1, verify-awf4-2026-09-27.md): было только wifi_is_connected() —
        // в Field/Outdoor AP без клиентов никогда true (wifi_manager.c:602-604),
        // образ никогда не подтверждался. httpd уже поднят безусловно (см.
        // комментарий выше) -> httpd_up=true; N=30с — main/ota_mark_valid_plan.h.
        if (!ota_valid_marked &&
            ota_mark_valid_should_fire(wifi_is_connected(), usb_host_cdc_is_connected(),
                                        /*httpd_up=*/true, seconds_since_boot, 30)) {
            ota_valid_marked = true;
            esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
            if (e != ESP_OK)
                ESP_LOGW(TAG, "AWF-4: mark_app_valid_cancel_rollback: %s", esp_err_to_name(e));
        }
        if (++wifi_return_tick >= 15) {
            wifi_return_tick = 0;
            wifi_manager_try_return_to_sta();
        }
        spectrum_base_save_retry_tick();   // F4: повтор отложенной base.bin
        const spectrum_data_t *sp = spectrum_get_current();
        ESP_LOGI(TAG, "USB:%s WiFi:%s TCP:%s counts:%" PRIu32 " cpu:%u%%",
            usb_host_cdc_is_connected() ? "OK" : "--",
            wifi_is_connected() ? "OK" : "--",
            tcp_bridge_client_connected() ? "OK" : "--",
            sp->total_counts, (unsigned)sp->cpu_load);
        usb_host_cdc_log_pkt_stats();   // #FW-53: shproto good/bad, тот же тик 10 с

        // #FW-13: период -inf 30 с → 30 мин (оператор 2026-07-03). Единственное
        // динамичное поле ответа — температура; каждый -inf вклинивает 404-байтный
        // текстовый ответ в поток свипов, чаще незачем.
        if (usb_host_cdc_is_connected() && ++info_tick >= 180) {
            info_tick = 0;
            usb_host_send_text_command("-inf");
        }
        if (++autosave_tick >= HIST_DROP_E3_AUTOSAVE_TICKS) {
            autosave_tick = 0;
            spectrum_autosave_consume_abort();
#if HIST_DROP_I3_SLICED
                /* #FW-8 F1a: sliced write in post-commit quiet windows.
                 * Offline / silent USB → one-shot full write (no 1 Hz burst).
                 * After fail_streak≥5 → force one-shot (one hist-drop) instead of
                 * unbounded deferral.
                 * http_io_gate is taken only around fopen/fwrite/rename inside
                 * begin/pump/one-shot — not across commit waits (AUD-ASW126 #6). */
                if (ota_busy_is_busy()) {
                    // М6 (release-gate-firmware-v1.2.28-code.md): OTA держит
                    // ota_busy весь приём — begin()/autosave() ниже и так
                    // молча откажут. Раньше это шло в note_fail() и раздувало
                    // fail_streak чужой причиной. Тик пропущен — не отказ.
                    ESP_LOGI(TAG, "LittleFS autosave tick skipped: OTA in progress");
                } else if (!usb_host_cdc_is_connected() || spectrum_autosave_fail_streak() >= 5) {
                    if (spectrum_autosave_in_progress())
                        spectrum_autosave_abort_keep();   // П-6: current.bin не трогать
                    spectrum_autosave_consume_abort();
                    hist_drop_diag_autosave_begin(true);
                    int64_t t0 = esp_timer_get_time();
                    spectrum_autosave();
                    int64_t dt_us = esp_timer_get_time() - t0;
                    hist_drop_diag_autosave_end();
                    ESP_LOGI(TAG, "LittleFS autosave took %lld us (%s)",
                             (long long)dt_us,
                             usb_host_cdc_is_connected() ? "force one-shot" : "offline one-shot");
                } else {
                    /* Begin must land in quiet (fopen ~100–160 ms). Retry a few
                     * commits if headroom was already gone. Resume after yield. */
                    int pumps = 0;
                    int wait_fail = 0;
                    int begin_tries = 0;
                    while (begin_tries < 4) {
                        begin_tries++;
                        if (autosave_sig && !spectrum_autosave_in_progress()) {
                            if (xSemaphoreTake(autosave_sig, pdMS_TO_TICKS(1500)) != pdTRUE) {
                                if (++wait_fail >= 3) break;
                                continue;
                            }
                        }
                        wait_fail = 0;
                        if (spectrum_autosave_begin()) break;
                        if (spectrum_autosave_in_progress()) break; /* yielded, snap held */
                    }
                    if (!spectrum_autosave_in_progress()) {
                        spectrum_autosave_note_fail();
                        ESP_LOGI(TAG, "LittleFS autosave begin deferred (no quiet headroom)");
                    }
                    while (spectrum_autosave_in_progress()) {
                        bool wait_timed_out = false;
#if HIST_DROP_DIAG && HIST_DROP_E4_NO_WAIT_COMMIT
                        wait_timed_out = true;
#else
                        if (autosave_sig) {
                            /* Do NOT Take(0)-clear here: a Give may have arrived
                             * during the previous pump's flash work. Clearing it
                             * forces a 1.5 s wait that times out if sweeps were
                             * dropped by that same freeze → abort mid-file. */
                            wait_timed_out =
                                (xSemaphoreTake(autosave_sig, pdMS_TO_TICKS(1500)) != pdTRUE);
                        }
#endif
                        if (wait_timed_out) {
                            if (++wait_fail >= 3) {
                                /* Soft yield: keep tmp+offset for next cycle. */
                                spectrum_autosave_yield();
                                spectrum_autosave_note_fail();
                                ESP_LOGI(TAG,
                                         "LittleFS autosave yielded (commit wait_timeout x3, USB live)");
                                break;
                            }
                            ESP_LOGW(TAG, "autosave wait_timeout (retry %d)", wait_fail);
                            continue;
                        }
                        wait_fail = 0;
                        if (!spectrum_autosave_begin()) {
                            /* Could not reopen after yield — try next tick. */
                            spectrum_autosave_note_fail();
                            break;
                        }
                        hist_drop_diag_autosave_begin(false);
                        spectrum_autosave_pump();
                        hist_drop_diag_autosave_end();
                        pumps++;
                    }
                    if (pumps > 0 && !spectrum_autosave_in_progress())
                        ESP_LOGI(TAG, "LittleFS autosave sliced pumps=%d", pumps);
                }
#else
                bool wait_timed_out = false;
#if HIST_DROP_DIAG && HIST_DROP_E4_NO_WAIT_COMMIT
                wait_timed_out = true;
#else
                if (autosave_sig) {
                    xSemaphoreTake(autosave_sig, 0);
                    wait_timed_out = (xSemaphoreTake(autosave_sig, pdMS_TO_TICKS(1500)) != pdTRUE);
                }
#endif
                if (wait_timed_out && usb_host_cdc_is_connected()) {
                    spectrum_autosave_note_fail();
                    ESP_LOGI(TAG, "LittleFS autosave skipped (commit wait_timeout, USB live)");
                } else {
                    hist_drop_diag_autosave_begin(wait_timed_out);
                    int64_t t0 = esp_timer_get_time();
                    spectrum_autosave();
                    int64_t dt_us = esp_timer_get_time() - t0;
                    hist_drop_diag_autosave_end();
                    ESP_LOGI(TAG, "LittleFS autosave took %lld us%s",
                             (long long)dt_us,
                             wait_timed_out ? " (wait_timeout)" : "");
                }
#endif
        }
        // #WF-1: отложенная запись калибровки (s_calib_dirty). Внутри сама берёт
        // SPEC_LOCK только на снапшот; flash-запись — вне лока и вне CDC/httpd.
        spectrum_save_calibration();

        // 1.2.31: Сброс спектра -> новая сессия; NVS пишет только эта задача, <=1 записи за тик.
        uint32_t req_now = spectrum_session_req();
        if (session_need_bump(&ss, req_now)) {
            uint32_t prev = ss.sess;
            session_apply_bump(&ss, req_now, boot_config_bump_session(ss.sess));
            if (ss.sess != prev) ESP_LOGW(TAG, "spectrum reset: board session #%" PRIu32, ss.sess);
            else ESP_LOGE(TAG, "spectrum reset: session bump failed, staying #%" PRIu32, ss.sess);
        }

        // issue #52: резервный снимок раз в bk_h часов.
        //
        // Период отсчитывается по ЧАСАМ (esp_timer_get_time), а не по числу
        // итераций: тело цикла блокирующее (autosave ждёт коммитов свипа до
        // нескольких секунд), поэтому vTaskDelay(10 с) — нижняя граница шага, и
        // счёт итерациями растягивал бы «24 часа» на неизвестную величину вверх.
        //
        // Настройки перечитываются НЕ каждый тик: boot_config_load() — это девять
        // обращений к NVS (открытие раздела флеша + мьютекс, общий с Wi-Fi), а
        // менять их могут только через Web UI. Раз в минуту достаточно, чтобы
        // правка в UI применялась без перезагрузки.
        if (ss.sess != 0 && ++backup_cfg_tick >= 6) {
            backup_cfg_tick = 0;
            boot_config_load(&backup_cfg);
        }
        if (ss.sess != 0 && backup_cfg.backup_keep > 0) {
            const int64_t now_us = esp_timer_get_time();
            const int64_t period_us = backup_cfg.backup_test_minutes
                    ? (int64_t)backup_cfg.backup_hours * 60 * 1000000LL      // минуты (стенд)
                    : (int64_t)backup_cfg.backup_hours * 3600 * 1000000LL;   // часы
            if (ss.due_us == 0)
                ss.due_us = now_us + period_us;   // первый снимок — через период
            if (now_us >= ss.due_us) {
                const spectrum_data_t *bsp = spectrum_get_current();
                if (!usb_host_cdc_is_connected()) {
                    // Прибор отключён: спектр восстановлен из current.bin и БОЛЬШЕ НЕ
                    // МЕНЯЕТСЯ. Снимки были бы побайтовыми копиями и вытеснили бы
                    // ротацией те, ради которых фича и делается. Ждём следующий период.
                    ss.due_us = now_us + period_us;
                    ESP_LOGI(TAG, "backup: skipped, analyzer not connected");
                } else if (!bsp->valid || bsp->total_time_sec == 0) {
                    ss.due_us = now_us + period_us;
                    ESP_LOGI(TAG, "backup: skipped, no valid spectrum yet");
                } else if (http_io_gate_try_enter()) {
                    int rc = spectrum_backup_save(ss.sess, ss.seq + 1,
                                                  backup_cfg.backup_keep, ss.seen_req);
                    http_io_gate_leave();
                    if (rc == 0) {
                        ss.seq++;
                        ss.fail_streak = 0;
                        ss.due_us = now_us + period_us;
                    } else if (rc == -5) {
                        ;   // Сброс между решением и снимком: следующий тик откроет сессию, срок не трогаем
                    } else if (++ss.fail_streak >= 5) {
                        // Пять отказов подряд — причина устойчивая (раздел полон,
                        // сломана ФС). Ждём целый период вместо попытки раз в 10 с:
                        // каждая стоит обхода каталога и строки в журнале.
                        ss.fail_streak = 0;
                        ss.due_us = now_us + period_us;
                        ESP_LOGE(TAG, "backup: save failed rc=%d 5x in a row — waiting full period",
                                 rc);
                    } else {
                        ESP_LOGW(TAG, "backup: save failed rc=%d (%d in a row), retry next tick",
                                 rc, ss.fail_streak);
                    }
                }
                // Гейт занят — ничего не меняем: срок остаётся просроченным,
                // на следующем тике попробуем снова.
            }
        } else if (backup_cfg.backup_keep == 0) {
            ss.due_us = 0;      // выключено — период начнём заново при включении
        }
    }
}