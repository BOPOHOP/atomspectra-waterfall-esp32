#!/usr/bin/env bash
# release-gate 1.2.29 (разбор дельты, мутанты MA–MF): host-тесты не компилируют
# spectrum.c/usb_host_cdc.c/tcp_bridge.c/..., поэтому удаление вызова чистой функции
# проходило незамеченным. Проверка: каждая строка-вызов есть в файле ровно N раз.
cd "${1:-$(dirname "$0")/../../main}" || exit 2   # $1 — другой каталог (проверка на копии)
RC=0
need() {   # need <file> <count> <fixed string>
    local n; n=$(grep -cF -- "$3" "$1")
    if [ "$n" -ne "$2" ]; then echo "WIRING FAIL $1: '$3' x$n (need $2)"; RC=1; fi
}
need spectrum.c    1 'spectrum_stat_tag_stamp(&s_stat_stage.tag, s_reset_gen, s_usb_session);'
need spectrum.c    1 'spectrum_stat_tag_usable(&s_stat_stage.tag, s_usb_session);'
need spectrum.c    1 'spectrum_reset_gate_step(&s_reset_gate,'
need spectrum.c    1 'if (spectrum_reset_gate_on_publish(&s_reset_gate, first_valid, reset_confirmed))'
need spectrum.c    1 'if (calib_apply_coeffs(calib_read_is_success(cc == ce, coeffs, CALIB_COEFFS), serial_only)) {'
need usb_host_cdc.c 1 'spectrum_usb_session_bump();'
# C-40/F-06: досылка -rst — атомарный забор под spinlock, возврат без затирания нового, тик на живом соединении
need usb_host_cdc.c 1 'if (!spectrum_reset_still_undelivered(g)) {'
need usb_host_cdc.c 1 'if (!s_rst_pending) {'
need usb_host_cdc.c 1 'if (s_cdc_dev && s_rst_pending) rst_pending_dispatch();'
need usb_host_cdc.c 1 'spectrum_calib_set_serial_only(!spectrum_calibration_is_missing());'
need wifi_manager.c 2 'if (tcp_bridge_client_active(WIFI_RETURN_BRIDGE_IDLE_MS)) {'
need tcp_bridge.c  1 'setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka)) < 0 ||'
need monitor.c     1 'if (!prev_valid || resync != prev_resync) {'
# #HTTP-FS1: pull-ack удаление — только через очередь wf_fs_task, не синхронно в httpd
need web_waterfall.c 1 'int q = spectrogram_seg_delete_async(idx);'
need web_waterfall.c 0 'spectrogram_seg_delete(idx)'
need spectrogram.c 1 'if (s_del_q && xQueueReceive(s_del_q, &dr, 0) == pdTRUE) {'
need spectrogram.c 1 'if (dr.epoch != s_wf_epoch)'
need spectrogram.c 1 'bool fin = (i >= 0 && s_seg_reg[i].finalized);'
# #AUD-DIAG-1 R1/R2: причина сброса, номер загрузки и SHA ELF в /api/status
need web_server.c  1 'cJSON_AddStringToObject(root, "reset_reason", reset_reason_str(rr));'
need web_server.c  1 'cJSON_AddNumberToObject(root, "boot_count", boot_config_get_session());'
need web_server.c  1 'cJSON_AddStringToObject(root, "elf_sha", elf_sha);'
# #AUD-F01 (класс P-016): каждый вызов esp_restart() (*.c/*.h, все подкаталоги, в любом месте строки) —
# среди 3 предыдущих строк КОДА (комментарии и пустые не в счёт) есть spectrogram_prepare_reboot();
F01_AWK='FNR==1{w1=w2=w3=""} {c=$0; sub(/\r$/,"",c); sub(/\/\/.*/,"",c)} c ~ /^[ \t]*(\*|\/\*)/ {next}
  c ~ /(^|[^A-Za-z0-9_])esp_restart[ \t]*\(/ { n++; if ((w1 w2 w3) !~ /spectrogram_prepare_reboot[ \t]*\([ \t]*\)[ \t]*;/) print FILENAME":"FNR }
  c ~ /[^ \t]/ {w3=w2; w2=w1; w1=c} END{print "N=" n+0}'
f01=$(find . -name '*.[ch]' -print0 | xargs -0 awk "$F01_AWK")
nr=$(awk -F= '/^N=/{s+=$2} END{print s+0}' <<<"$f01"); bad=$(grep -v '^N=' <<<"$f01")
[ "${nr:-0}" -gt 0 ] || { echo "WIRING FAIL: esp_restart() calls not found (x0)"; RC=1; }
[ -z "$bad" ] || { echo "WIRING FAIL esp_restart() without spectrogram_prepare_reboot(): $bad"; RC=1; }
# разбор F01 D1-1/D1-2: FSLOCK с таймаутом и громкий отказ финализации; D4-1: DONE после подготовки
need spectrogram.c 1 'if (s_fs_lock && xSemaphoreTake(s_fs_lock, pdMS_TO_TICKS(i ? 1000 : 5000)) != pdTRUE) {'
need spectrogram.c 1 'if (!fin_ok) ESP_LOGE(TAG, "prepare_reboot: open segment NOT finalized");'
# #AUD-DUP1: опора последней строки сохраняется при штатной перезагрузке, restore подставляет её или делает resync
need spectrogram.c 1 'if (was_rec) wf_ref_save();'
need spectrogram.c 1 'if (s_ref_pending) {'
need spectrogram.c 1 'memcpy(s_prev, wf_ref_first_prev(s_ref_choice, s_ref_bins, s_wf_snap->bins),'
need spectrogram.c 1 's_row[i]  = wf_row_delta(s_wf_snap->bins[i], s_prev[i], reset);'
need spectrogram.c 3 'unlink(WF_REF_FILE);'   # wf_ref_save (перед rename), restore, start
# #RB-STK-1: перезагрузка из esp_timer/sys_evt — в своей задаче
need wifi_manager.c 1 'if (xTaskCreate(fb_reboot_task, "fb_reboot", 6144, NULL, 5, NULL) == pdPASS) return;'
d41=$(awk '/spectrogram_prepare_reboot\(\);/{p=FNR} /set_progress\(OTA_GH_ST_DONE/{d=FNR} END{print (p && d > p) ? "ok" : "bad"}' ota_github_client.c)
[ "$d41" = ok ] || { echo "WIRING FAIL ota_github_client.c: OTA_GH_ST_DONE before spectrogram_prepare_reboot()"; RC=1; }
# Codeaudit F-08/F-09/F-10
need ota_github_client.c 1 'spectrum_autosave_abort_keep();'
need web_server.c  4 'if (!json) httpd_resp_set_status(req, "503 Service Unavailable");'
need wf_offload.c  1 'if (short_rd) { result = -16; goto done; }'
# F-09 (класс): результат cJSON_PrintUnformatted проверен на NULL в 3 строках после вызова (все *.c)
f09=$(awk 'match($0,/char \*[A-Za-z_]+ = cJSON_PrintUnformatted\(/){v=substr($0,RSTART+6,RLENGTH-6); sub(/ =.*/,"",v); k=3; want=FILENAME":"FNR; next}
  k>0 { if (index($0,"!" v) || index($0, v " ?")) k=0; else if (--k==0) print want }' ./*.c)
[ -z "$f09" ] || { echo "WIRING FAIL cJSON_PrintUnformatted without NULL check: $f09"; RC=1; }
[ "$RC" -eq 0 ] && echo "wiring: OK"
exit $RC
