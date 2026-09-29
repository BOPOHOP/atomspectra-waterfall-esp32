#!/usr/bin/env bash
# #AUD-F01 + разбор F01: мутанты классовой проверки esp_restart()/prepare_reboot и связок F-08..F-10.
# Каждый мутант (копия main/ во временном каталоге) обязан дать ровно 1 строку WIRING FAIL; baseline — 0.
set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0
mut() {   # mut <name> <file> <sed-expr> ; "baseline" — без правки
    rm -rf "$T/main"; cp -r ../../main "$T/main"
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
exit $RC
