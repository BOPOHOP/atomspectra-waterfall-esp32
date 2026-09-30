#!/usr/bin/env bash
# #AUD-F01 + разбор F01: мутанты классовой проверки esp_restart()/prepare_reboot и связок F-08..F-10.
# Каждый мутант (копия main/ во временном каталоге) обязан дать ровно 1 строку WIRING FAIL; baseline — 0.
set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0
mut() {   # mut <name> <file> <sed-expr> ; "baseline" — без правки
    rm -rf "$T/main" "$T/web"; cp -r ../../main "$T/main"; cp -r ../../web "$T/web"   # web/ — для need ../web/index.html
    if [ "$1" != baseline ]; then cp "$T/main/$2" "$T/o"; sed -i "$3" "$T/main/$2"
        cmp -s "$T/o" "$T/main/$2" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }; fi
    local n; n=$(bash wiring_check.sh "$T/main" | grep -c 'WIRING FAIL')
    local want=1; [ "$1" = baseline ] && want=0
    echo "== $1: $n FAIL (need $want)"; [ "$n" -eq "$want" ] || RC=1
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
mut M24_barrier_set  spectrogram.c        '/s_wf_busy = true;    \/\* до проверки/{n;d}'
mut M25_barrier_wait spectrogram.c        '$!N;s/ *__sync_synchronize();\n\( *for (int i = 0; i < 200\)/\1/;P;D'
mut M26_css_log      ../web/index.html    's/\.row + \.row, #log + \.row{/.row + .row{/'
# #OTA-VR: повтор проверки образа в обоих путях OTA, отказ esp_ota_end() окончателен
mut M27_web_no_retry web_server.c         's/err = ota_set_boot_verified(update);/err = ESP_OK;/'
mut M28_web_direct   web_server.c         '$a static void mut28(const esp_partition_t *p) { esp_ota_set_boot_partition(p); }'
mut M29_gh_no_retry  ota_github_client.c  's/if (ota_set_boot_verified(update) != ESP_OK) {/if (0) {/'
mut M30_gh_end_soft  ota_github_client.c  's/if (esp_ota_end(ota) != ESP_OK) { install_fail/if (esp_ota_end(ota) != ESP_OK \&\& 0) { install_fail/'
mut M31_retry_once   ota_busy.c           's/\&b, 3, ESP_ERR_OTA_VALIDATE_FAILED/\&b, 1, ESP_ERR_OTA_VALIDATE_FAILED/'
mut M32_web_end_soft web_server.c         '/err = esp_ota_end(ota);/{n;s/if (err != ESP_OK) {/if (0) {/}'
# Разбор pass3 F-02 / F-09
mut M33_busy_clear   spectrogram.c        '/s_wf_busy = false;   \/\* LK-16/d'
mut M34_ref_pending  spectrogram.c        '/        if (s_ref_pending) {/{n;d}'
mut M35_alias        spectrogram.c        's/    s_pre_rst_bins = s_ref_bins;/    s_pre_rst_bins = NULL;/'
mut M36_order        spectrogram.c        '/memcpy(s_pre_rst_bins, s_prev, WF_CHANNELS \* sizeof(uint32_t));/d; /        if (s_ref_pending) {/i\            memcpy(s_pre_rst_bins, s_prev, WF_CHANNELS * sizeof(uint32_t));'
mut M37_offload_503  web_waterfall.c      '/if (!out) httpd_resp_set_status(req, "503 Service Unavailable");/d'
exit $RC
