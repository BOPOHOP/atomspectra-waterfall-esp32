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
need usb_host_cdc.c 1 'if (s_rst_pending && !spectrum_reset_still_undelivered(s_rst_pending_gen)) {'
need usb_host_cdc.c 1 'spectrum_calib_set_serial_only(!spectrum_calibration_is_missing());'
need wifi_manager.c 2 'if (tcp_bridge_client_active(WIFI_RETURN_BRIDGE_IDLE_MS)) {'
need tcp_bridge.c  1 'setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka)) < 0 ||'
need monitor.c     1 'if (!prev_valid || resync != prev_resync) {'
# #HTTP-FS1: pull-ack удаление — только через очередь wf_fs_task, не синхронно в httpd
need web_waterfall.c 1 'if (!spectrogram_seg_delete_async(idx)) {'
need web_waterfall.c 0 'spectrogram_seg_delete(idx)'
need spectrogram.c 1 'while (s_del_q && xQueueReceive(s_del_q, &del_idx, 0) == pdTRUE) {'
[ "$RC" -eq 0 ] && echo "wiring: OK"
exit $RC
