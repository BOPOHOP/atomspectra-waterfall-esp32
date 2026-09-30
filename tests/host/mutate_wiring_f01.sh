#!/usr/bin/env bash
# #AUD-F01 + разбор F01: мутанты классовой проверки esp_restart()/prepare_reboot и связок F-08..F-10.
# Каждый мутант (копия main/ во временном каталоге) обязан дать ровно 1 строку WIRING FAIL; baseline — 0.
set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0
mut() {   # mut <name> <file> <sed-expr> ; "baseline" — без правки
    rm -rf "$T/main" "$T/web"; cp -r ../../main "$T/main"; cp -r ../../web "$T/web"   # web/ — для need ../web/index.html
    if [ "$1" != baseline ]; then cp "$T/main/$2" "$T/o"; sed -i "$3" "$T/main/$2"
        cmp -s "$T/o" "$T/main/$2" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }; fi
    local out n; out=$(bash wiring_check.sh "$T/main"); n=$(grep -c 'WIRING FAIL' <<<"$out")
    local want=1; [ "$1" = baseline ] && want=0
    # 4-й аргумент (pass3 F-03 / pass4 P3-3): подстрока сообщения — краснеть обязана ИМЕННО эта проверка
    local tag=""; if [ -n "${4:-}" ] && ! grep 'WIRING FAIL' <<<"$out" | grep -qF -- "$4"; then tag=" WRONG CHECK (want: $4)"; RC=1; fi
    echo "== $1: $n FAIL (need $want)$tag"; [ "$n" -eq "$want" ] || RC=1
}
mut baseline         -                    ''
mut M1_manual_ota    web_server.c         '/spectrogram_prepare_reboot();$/d'
mut M2_done_first    ota_github_client.c  '/set_progress(OTA_GH_ST_DONE, received/d; /AWF-5: installed/i\    set_progress(OTA_GH_ST_DONE, received, received, NULL);'
mut M3_commented     wifi_manager.c       '0,/    spectrogram_prepare_reboot();/s//    \/\/ spectrogram_prepare_reboot();/'
mut M4_if_restart    tcp_bridge.c         '$a static void mut4(int x) { if (x) esp_restart(); }'
mut M5_space_paren   tcp_bridge.c         '$a static void mut5(void) { esp_restart (); }'
mut M6_f09_site      web_server.c         '0,/if (!json) httpd_resp_set_status/{/if (!json) httpd_resp_set_status/d}'
mut M7_f08_abort     ota_github_client.c  '/spectrum_autosave_abort_keep();/d'
mut M8_f10_short     wf_offload.c         '/if (short_rd) { result = -16; goto done; }/d'
mut M9_d12_timeout   spectrogram.c        's/pdMS_TO_TICKS(i ? 1000 : 5000)/portMAX_DELAY/'
mut M10_rst_capture  spectrogram.c        '/memcpy(s_pre_rst_bins, s_prev, WF_CHANNELS \* sizeof(uint32_t));/d'
mut M11_rst_keep     spectrogram.c        's/bool keep = s_pre_rst_valid \&\& wf_rst_keeps_data(/bool keep = false \&\& wf_rst_keeps_data(/'
mut M12_prev_valid   spectrogram.c        's/s_prev_valid = s_wf_snap->valid \&\& wf_base_zero();   \/\*/s_prev_valid = true \&\& wf_base_zero();   \/\*/'
mut M13_cjson_hooks  main.c               '/cJSON_InitHooks(&cj_hooks);/d'
mut M14_alloc_cb     main.c               '/heap_caps_register_failed_alloc_callback(alloc_failed_cb);/d'
mut M15_alloc_field  web_server.c         '/"alloc_fail", af_n/d'
mut M16_tcp_rx_stack tcp_bridge.c         's/"tcp_rx",  5120/"tcp_rx",  4096/'
mut M17_tcp_rx_buf   tcp_bridge.c         's/    enum { RX_BUF = 1024 };/    uint8_t buf[1024];/'
mut M18_base0        spectrogram.c        '0,/ \&\& wf_base_zero();/s// ;/'
mut M19_f4_stale     spectrogram.c        '/if (rs == s_wf_resync_seen) s_pre_rst_valid = false;/d'
mut M20_busy_wait    spectrogram.c        's/i < 200 \&\& s_wf_busy; i++/i < 10; i++/'
mut M21_busy_set     spectrogram.c        '/s_wf_busy = true;    \/\* до проверки recording/d'
mut M22_cjson_try    main.c               '/if (t_cjson_try) return;/d'
mut M23_rx_delete    tcp_bridge.c         's/retry in 1 s"); vTaskDelay(pdMS_TO_TICKS(1000)); }/"); vTaskDelete(NULL); return; }/'
mut M24_barrier_set  spectrogram.c        '/s_wf_busy = true;    \/\* до проверки/{n;d}' 'Dekker barrier'
mut M25_barrier_wait spectrogram.c        '$!N;s/ *__sync_synchronize();\n\( *for (int i = 0; i < 200\)/\1/;P;D' 'Dekker barrier'
mut M26_css_log      ../web/index.html    's/\.row + \.row, #log + \.row{/.row + .row{/' '#log + .row{'
# #OTA-VR: повтор проверки образа в обоих путях OTA, отказ esp_ota_end() окончателен
mut M27_web_no_retry web_server.c         's/err = ota_set_boot_verified(update);/err = ESP_OK;/' "line 'err = ota_set_boot_verified(update);'"
mut M28_web_direct   web_server.c         '$a static void mut28(const esp_partition_t *p) { esp_ota_set_boot_partition(p); }' "'esp_ota_set_boot_partition(' x1"
mut M29_gh_no_retry  ota_github_client.c  's/if (ota_set_boot_verified(update) != ESP_OK) {/if (0) {/' "line 'if (ota_set_boot_verified(update) != ESP_OK) {'"
mut M30_gh_end_soft  ota_github_client.c  's/if (esp_ota_end(ota) != ESP_OK) { install_fail/if (esp_ota_end(ota) != ESP_OK \&\& 0) { install_fail/' 'ota_end_failed'
mut M31_retry_once   ota_busy.c           's/\&b, 3, ESP_ERR_OTA_VALIDATE_FAILED/\&b, 1, ESP_ERR_OTA_VALIDATE_FAILED/' 'ota_boot_retry_pure(set_boot_cb'
mut M32_web_end_soft web_server.c         '/err = esp_ota_end(ota);/{n;s/if (err != ESP_OK) {/if (0) {/}' 'esp_ota_end() failure is not final'
# Разбор pass3 F-02 / F-09
mut M33_busy_clear   spectrogram.c        '/s_wf_busy = false;   \/\* LK-16/d' 's_wf_busy = false;'
mut M34_ref_pending  spectrogram.c        '/        if (s_ref_pending) {/{n;d}' 's_ref_pending not cleared'
mut M35_alias        spectrogram.c        's/    s_pre_rst_bins = s_ref_bins;/    s_pre_rst_bins = NULL;/' 's_pre_rst_bins = s_ref_bins;'
mut M36_order        spectrogram.c        '/memcpy(s_pre_rst_bins, s_prev, WF_CHANNELS \* sizeof(uint32_t));/d; /        if (s_ref_pending) {/i\            memcpy(s_pre_rst_bins, s_prev, WF_CHANNELS * sizeof(uint32_t));' 'pre-reset capture precedes'
mut M37_offload_503  web_waterfall.c      '/if (!out) httpd_resp_set_status(req, "503 Service Unavailable");/d' 'web_waterfall.c'
# pass4 P2-1 / P3-1: сам вызов IDF и пауза в ota_busy.c; вызов закомментирован; отказ esp_ota_end без return; тело отказа GitHub-ветки
mut M38_setboot_noop ota_busy.c          's/return esp_ota_set_boot_partition(b->p);/return 0;/' 'return esp_ota_set_boot_partition(b->p);'
mut M39_no_pause    ota_busy.c           's/if (b->n++) vTaskDelay(pdMS_TO_TICKS(200));/b->n++;/' 'vTaskDelay(pdMS_TO_TICKS(200));'
mut M40_web_comment web_server.c         's/    err = ota_set_boot_verified(update);/    \/\/ err = ota_set_boot_verified(update);/' "line 'err = ota_set_boot_verified(update);'"
mut M41_end_no_ret  web_server.c         '/"ota_end: invalid image");/{n;d}' 'esp_ota_end() failure is not final'
mut M42_gh_fail_nop ota_github_client.c  's/install_fail(0, "set_boot_partition_failed"); goto done;/;/' 'set_boot_partition_failed'
# #RST-TAIL (1.2.30): вызов перед -rst в трёх точках, порядок до передачи, решение wf_task, отдача done, пробуждение
mut M43_tail_web_del    web_server.c     '/^    (void)spectrogram_flush_tail(1200);/d' "(void)spectrogram_flush_tail(1200);'"
mut M44_tail_tcp_del    tcp_bridge.c     '/^        if (saw_rst) (void)spectrogram_flush_tail(1200);/d' 'if (saw_rst) (void)spectrogram_flush_tail(1200);'
mut M45_tail_usb_del    usb_host_cdc.c   '/^    if (cmd_is_device_reset(cmd0)) (void)spectrogram_flush_tail(1200);/d' 'if (cmd_is_device_reset(cmd0)) (void)spectrogram_flush_tail(1200);'
mut M46_tail_web_cmt    web_server.c     's/^    (void)spectrogram_flush_tail(1200);/    \/\/ (void)spectrogram_flush_tail(1200);/' "(void)spectrogram_flush_tail(1200);'"
mut M47_tail_web_after  web_server.c     '/^    (void)spectrogram_flush_tail(1200);/d; /bool sent = usb_host_cdc_send(pkt.data/a\    (void)spectrogram_flush_tail(1200);' 'must precede usb_host_cdc_send in handle_reset'
mut M48_tail_noforce    spectrogram.c    's/wf_tail_should_row(tail_force, now_time/wf_tail_should_row(false, now_time/' 'wf_tail_should_row(tail_force'
mut M49_tail_nodone     spectrogram.c    '/^        if (s_tail_inflight) { s_tail_inflight = false;/d' 's_tail_inflight = false; if (s_tail_done)'
mut M50_tail_nowake     spectrogram.c    '/^    xSemaphoreGive(s_commit_sig);            \/\/ разбудить wf_task вне очереди/d' 'разбудить wf_task вне очереди'
# S-01 (1.2.30): CSRF на тяжёлых GET — плата (3 обработчика) и страницы (токен в заголовке)
mut M51_s01_check_del   web_server.c     '0,/    if (!csrf_check(req)) return ESP_FAIL;   \/\/ S-01 (1.2.30)/{/    if (!csrf_check(req)) return ESP_FAIL;   \/\/ S-01 (1.2.30)/d}' '// S-01 (1.2.30)'
mut M52_s01_window_del  web_waterfall.c  '/    if (!web_csrf_check(req)) return ESP_FAIL;   \/\/ S-01 (1.2.30)/d' 'web_csrf_check(req)) return ESP_FAIL;   // S-01'
mut M53_s01_ui_nohdr    ../web/waterfall.html 's/hf.call(window,"\/api\/waterfall\/window",{headers:{"X-CSRF-Token":csrfToken}})/hf.call(window,"\/api\/waterfall\/window")/' '/api/waterfall/window'
mut M54_s01_ui_plain    ../web/system.html    's/await gget("\/api\/ota\/github\/check")/await fetch("\/api\/ota\/github\/check")/' 'gget("/api/ota/github/check")'
# LK-07 (1.2.30): проверка GitHub в фоне — обработчик зовёт async (не sync), задача стартует, состояние переходит в DONE
mut M55_lk07_sync       web_server.c          's/    ota_gh_check_async(resp, sizeof(resp));/    ota_gh_check(resp, sizeof(resp));/' 'ota_gh_check_async(resp, sizeof(resp));'
mut M56_lk07_nodecide   ota_github_client.c   's/    ota_chk_act_t act = ota_chk_decide(s_chk_state, age_ms, OTA_GH_CHECK_KEEP_MS);/    ota_chk_act_t act = OTA_CHK_ACT_START;/' 'ota_chk_decide(s_chk_state'
mut M57_lk07_nodone     ota_github_client.c   '/^    s_chk_state = OTA_CHK_DONE;/d' 's_chk_state = OTA_CHK_DONE;'
# LK-05 (1.2.30): предел ожидания прибора в httpd — константа 1000 мс, оба цикла на ней
mut M58_lk05_const      web_server.c          's/#define SETTINGS_RAW_WAIT_MS 1000/#define SETTINGS_RAW_WAIT_MS 2000/' '#define SETTINGS_RAW_WAIT_MS 1000'
mut M59_lk05_loop       web_server.c          '0,/    for (int waited = 0; waited < SETTINGS_RAW_WAIT_MS; waited += 50) {/s//    for (int waited = 0; waited < 2000; waited += 50) {/' 'waited < SETTINGS_RAW_WAIT_MS'
# WP5 (1.2.30): журнал потоком — счётчик байт, куски через план, нет копии всего кольца
mut M60_wp5_nocount     debug_log_ring.c      '/^    s_bytes_total += len;/d' 's_bytes_total += len;'
mut M61_wp5_noplan      debug_log_ring.c      's/? dbglog_chunk_plan(s_bytes_total, s_used, want, end, DBGLOG_CHUNK, \&off, \&len) : DBGLOG_CHUNK_OVERWRITTEN;/? DBGLOG_CHUNK_OK : DBGLOG_CHUNK_OVERWRITTEN;/' 'dbglog_chunk_plan(s_bytes_total'
mut M62_wp5_wholecopy   debug_log_ring.c      's/    char \*chunk = malloc(DBGLOG_CHUNK);/    char *chunk = malloc(s_cap);/' 'char *chunk = malloc(DBGLOG_CHUNK);'
# LK-09 (1.2.30): проверка «клиент ещё в реестре» перед отправкой кадра и предел очереди
mut M63_lk09_noalive    web_waterfall.c       '/    if (!alive) { free(a); return; }/d' 'if (!alive) { free(a); return; }'
mut M64_lk09_always     web_waterfall.c       's/    if (!alive) { free(a); return; }/    if (false) { free(a); return; }/' 'if (!alive) { free(a); return; }'
mut M65_lk09_inflight   web_waterfall.c       's/#define WS_INFLIGHT_MAX  4 /#define WS_INFLIGHT_MAX  8 /' '#define WS_INFLIGHT_MAX  4'
exit $RC
