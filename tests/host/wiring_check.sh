#!/usr/bin/env bash
# release-gate 1.2.29 (разбор дельты, мутанты MA–MF): host-тесты не компилируют
# spectrum.c/usb_host_cdc.c/tcp_bridge.c/..., поэтому удаление вызова чистой функции
# проходило незамеченным. Проверка: каждая строка-вызов есть в файле ровно N раз.
HERE=$(cd "$(dirname "$0")" && pwd)   # каталог скрипта (spec_writers.py) - до cd в main/
cd "${1:-$(dirname "$0")/../../main}" || exit 2   # $1 — другой каталог (проверка на копии)
RC=0
# release-gate 1.2.36 pass2 С-2: отсутствующий проверяемый файл = WIRING FAIL. Раньше grep -c на нём давал пустое n, [ -ne ]
# падал с ошибкой, и проверка молча проходила (нет spectrum_http_cache.c -> «wiring: OK»). Перечень - первые аргументы need/line
# этого скрипта; одна строка на отсутствующий файл, и гейт кончается сразу (awk-проверки без файла тоже молчали бы).
miss=0
for f in $(sed -nE 's/^ *(need|line) +([^ $]+) .*/\2/p' "$HERE/wiring_check.sh" | sort -u); do
    [ -f "$f" ] || { echo "WIRING FAIL $f: файл отсутствует"; miss=1; }
done
[ "$miss" -eq 0 ] || exit 1
need() {   # need <file> <count> <fixed string>
    [ -f "$1" ] || { echo "WIRING FAIL $1: файл отсутствует"; RC=1; return; }
    local n; n=$(grep -cF -- "$3" "$1")
    if [ "$n" -ne "$2" ]; then echo "WIRING FAIL $1: '$3' x$n (need $2)"; RC=1; fi
}
line() {   # line <file> <count> <строка кода целиком, без отступа> — не подстрока и не в комментарии (разбор pass4 P3-1)
    [ -f "$1" ] || { echo "WIRING FAIL $1: файл отсутствует"; RC=1; return; }
    local n; n=$(awk -v s="$3" '{c=$0; sub(/\r$/,"",c); sub(/^[ \t]+/,"",c)} c == s {n++} END{print n+0}' "$1")
    if [ "$n" -ne "$2" ]; then echo "WIRING FAIL $1: line '$3' x$n (need $2)"; RC=1; fi
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
need web_server.c  1 'cJSON_AddNumberToObject(root, "boot_count", boot_config_get_boot_session());'
# 1.2.31: новая сессия платы после Сброса непустого спектра
need web_server.c  1 'cJSON_AddNumberToObject(root, "session", boot_config_get_session());'
need spectrum.c    1 'if (session_reset_opens(s_spectrum.valid, s_spectrum.total_time_sec)) s_sess_req++;'
need spectrum.c    1 'if (!session_snap_current(s_sess_req, expect_req))'
need main.c        1 'session_apply_bump(&ss, req_now, boot_config_bump_session(ss.sess));'
need main.c        1 'ss.seen_req = spectrum_session_req();'
need main.c        1 'backup_cfg.backup_keep, ss.seen_req);'
need spectrogram.c 1 'h.boot_session = boot_config_get_session();'   # I2: живой NVS, не кеш загрузки
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
need web_waterfall.c 1 'if (!out) httpd_resp_set_status(req, "503 Service Unavailable");'   # pass3 F-09: h_offload_get
need wf_offload.c  1 'if (short_rd) { result = -16; goto done; }'
# #OTA-VR: оба пути OTA ставят загрузочный раздел через повтор проверки; отказ esp_ota_end() — окончательный
line web_server.c  1 'err = ota_set_boot_verified(update);'
need web_server.c  0 'esp_ota_set_boot_partition('
line ota_github_client.c 1 'if (ota_set_boot_verified(update) != ESP_OK) {'
line ota_github_client.c 1 'install_fail(0, "set_boot_partition_failed"); goto done;'
need ota_github_client.c 0 'esp_ota_set_boot_partition('
line ota_github_client.c 1 'if (esp_ota_end(ota) != ESP_OK) { install_fail(0, "ota_end_failed"); goto done; }'
line ota_busy.c    1 'esp_err_t err = ota_boot_retry_pure(set_boot_cb, &b, 3, ESP_ERR_OTA_VALIDATE_FAILED, &calls);'
line ota_busy.c    1 'return esp_ota_set_boot_partition(b->p);'                  # pass4 P2-1: сам вызов IDF
line ota_busy.c    1 'if (b->n++) vTaskDelay(pdMS_TO_TICKS(200));'               # пауза между повторами
# отказ esp_ota_end(): сразу за вызовом — if (err != ESP_OK) {, в его теле — return ESP_FAIL; (pass4 P3-1, E4)
oe=$(awk '{c=$0; sub(/\r$/,"",c); sub(/^[ \t]+/,"",c)}
  st == 2 && c == "return ESP_FAIL;" {r++}  st == 2 && c == "}" {st = 0}
  st == 1 {st = (c == "if (err != ESP_OK) {") ? 2 : 0; if (st == 2) a++}
  c == "err = esp_ota_end(ota);" {n++; st = 1}
  END{print (n == 1 && a == 1 && r == 1) ? "ok" : "bad " n+0 "/" a+0 "/" r+0}' web_server.c)
[ "$oe" = ok ] || { echo "WIRING FAIL web_server.c: esp_ota_end() failure is not final (#OTA-VR, $oe)"; RC=1; }
# #AUD-RST: неподтверждённый Сброс — опора до Сброса сохраняется и проверяется в wf_task
need spectrogram.c 1 'memcpy(s_pre_rst_bins, s_prev, WF_CHANNELS * sizeof(uint32_t));'
need spectrogram.c 1 'bool keep = s_pre_rst_valid && wf_rst_keeps_data('
need spectrogram.c 1 's_prev_valid = s_wf_snap->valid && wf_base_zero();   /* #AUD-RST: пустой снимок после Сброса — не опора */'
# Гейт 1.2.29: cJSON в PSRAM, счётчик отказов аллокации, стек tcp_rx без приёмного буфера
need main.c        1 'cJSON_InitHooks(&cj_hooks);'
need main.c        1 'heap_caps_register_failed_alloc_callback(alloc_failed_cb);'
need web_server.c  1 'cJSON_AddNumberToObject(root, "alloc_fail", af_n);'
need tcp_bridge.c  1 'xTaskCreatePinnedToCore(tcp_rx_task,     "tcp_rx",  5120, NULL, 5, NULL, 1);'
need tcp_bridge.c  0 'uint8_t buf[1024];'
need tcp_bridge.c  0 'vTaskDelete(NULL); return; }'                        # F-5: отказ буфера не убивает задачу
# Разбор 1c57e98 / Codeaudit 19:49: база AWF-3, устаревшая опора (F-4), рукопожатие wf_task↔prepare_reboot (LK-16)
need spectrogram.c 3 '&& wf_base_zero();'
need spectrogram.c 1 'if (rs == s_wf_resync_seen) s_pre_rst_valid = false;'
need spectrogram.c 1 'for (int i = 0; i < 200 && s_wf_busy; i++) vTaskDelay(pdMS_TO_TICKS(10));'
need spectrogram.c 1 's_wf_busy = true;    /* до проверки recording'
need main.c        1 'if (t_cjson_try) return;'
need spectrogram.c 1 's_pre_rst_bins = s_ref_bins;'                        # один PSRAM-буфер на две роли (min_free_heap)
# Разбор 45cc8ae..5859f53 pass3 F-02: сброс флага итерации; опора файла потребляется один раз и РАНЬШЕ захвата опоры до Сброса
need spectrogram.c 1 's_wf_busy = false;   /* LK-16'
rp=$(awk '{c=$0; sub(/\r$/,"",c); sub(/^[ \t]+/,"",c)} p == "if (s_ref_pending) {" {n++; if (c == "s_ref_pending = false;") a++}
  {p=c} END{print (n == 0 || (n == 1 && a == 1)) ? "ok" : "bad " n+0 "/" a+0}' spectrogram.c)
[ "$rp" = ok ] || { echo "WIRING FAIL spectrogram.c: s_ref_pending not cleared when the file reference is consumed ($rp)"; RC=1; }
ord=$(awk '/if \(s_ref_pending\) \{/ && !a {a=FNR} /memcpy\(s_pre_rst_bins, s_prev, WF_CHANNELS/ && !b {b=FNR}
  END{print (!a || !b || a < b) ? "ok" : "bad " a "/" b}' spectrogram.c)
[ "$ord" = ok ] || { echo "WIRING FAIL spectrogram.c: pre-reset capture precedes file-reference consume (shared buffer, $ord)"; RC=1; }
# Разбор 54194aa pass2: барьеры Деккера — сразу после s_wf_busy = true и сразу перед ожиданием в prepare_reboot
dk=$(awk '{c=$0; sub(/\r$/,"",c); sub(/^[ \t]+/,"",c)}
  p ~ /^s_wf_busy = true;/ {n++; if (c ~ /^__sync_synchronize\(\);/) a++}
  c ~ /^for \(int i = 0; i < / && p ~ /^__sync_synchronize\(\);/ {b++}
  {p=c} END{print (a == n && b == 1) ? "ok" : "bad " n+0 "/" a+0 "/" b+0}' spectrogram.c)
[ "$dk" = ok ] || { echo "WIRING FAIL spectrogram.c: Dekker barrier around s_wf_busy missing ($dk)"; RC=1; }
# P-03 (Codeaudit): журнал главной страницы существует и показывается (в копии main/ без web/ — пропуск)
if [ -f ../web/index.html ]; then
    # #59/#60 (1.2.30): журнал обмена с прибором — свёрнутая секция под калибровкой, с сохранением в файл
    need ../web/index.html 1 '<pre id="log" style="margin:0 0 8px;'
    need ../web/index.html 1 'id="log-body" style="display:none;'
    need ../web/index.html 1 'onclick="saveLog()"'
    need ../web/index.html 1 'function lg(m,at){if(!logEl)return;var a='
    # S-01 (1.2.30): страницы шлют CSRF-токен на тяжёлых GET
    need ../web/service.html   2 'await gget("/api/settings/backup");'
    need ../web/system.html    1 'await gget("/api/ota/github/check")'
    need ../web/waterfall.html 1 'hf.call(window,"/api/waterfall/window?rows=256",{headers:{"X-CSRF-Token":csrfToken}})'
fi
# S-01 (1.2.30): тяжёлые GET требуют CSRF-токен на стороне платы (отступ: закомментированная строка не считается)
need web_server.c    2 '    if (!csrf_check(req)) return ESP_FAIL;   // S-01 (1.2.30)'
need web_waterfall.c 1 '    if (!web_csrf_check(req)) return ESP_FAIL;   // S-01 (1.2.30)'
# LK-07 (1.2.30): проверка релиза GitHub — не в задаче httpd; решение запуск/ожидание/готовое — ota_gh_check_state.h
need web_server.c        1 '    ota_gh_check_async(resp, sizeof(resp));'
need ota_github_client.c 1 '    ota_chk_act_t act = ota_chk_decide(s_chk_state, age_ms, OTA_GH_CHECK_KEEP_MS);'
need ota_github_client.c 1 '        if (xTaskCreatePinnedToCore(ota_gh_check_task, "ota_gh_chk", OTA_GH_CHK_STACK, NULL, 3, NULL, 1) != pdPASS) {'
need ota_github_client.c 1 '    s_chk_state = OTA_CHK_DONE;'
# WP5 (1.2.30): журнал отладки отдаётся потоком по кускам, без malloc на всё кольцо; счётчик байт растёт при записи
need debug_log_ring.c 1 '    char *chunk = malloc(DBGLOG_CHUNK);'
need debug_log_ring.c 1 '? dbglog_chunk_plan(s_bytes_total, s_used, want, end, DBGLOG_CHUNK, &off, &len) : DBGLOG_CHUNK_OVERWRITTEN;'
need debug_log_ring.c 1 '    s_bytes_total += len;'
need debug_log_ring.c 0 'tmp = malloc(used + 1);'
# LK-09 (1.2.30): отброшенному WS-клиенту кадры из очереди не шлём (не ждём сокетный таймаут на каждом); очередь ≤ 4 кадров
need web_waterfall.c 1 '    if (!alive) { free(a); return; }'
need web_waterfall.c 1 '    for (int i = 0; i < WF_WS_MAX; i++) if (s_ws_fds[i] == a->fd) { alive = true; break; }'
need web_waterfall.c 1 '#define WS_INFLIGHT_MAX  4'
# LK-08/P-01 (1.2.30): долгие выдачи — в отдельной задаче (httpd свободен), одна за раз; окно страницы — последние N строк
need web_waterfall.c 1 '    if (!j || httpd_req_async_handler_begin(req, &cp) != ESP_OK) {'
need web_waterfall.c 1 '#define WF_DL_MAX         1'
need web_waterfall.c 1 '    reg(server, "/api/waterfall/window", HTTP_GET,  h_window_async);'
need web_waterfall.c 1 '    reg(server, "/api/waterfall/export.aswf", HTTP_GET, h_export_aswf_async);'
need web_waterfall.c 1 '    reg(server, "/api/waterfall/export.n42",  HTTP_GET, h_export_n42_async);'
need web_waterfall.c 1 '    reg(server, "/api/waterfall/segment",  HTTP_GET, h_segment_async);'
need web_waterfall.c 1 '        if (want >= 1 && want < rows) rows = want;'
# LK-02/03/04 (1.2.30): старт/стоп/очистка/удаление сегмента — в отдельной задаче со своим счётчиком
need web_waterfall.c 1 '    reg(server, "/api/waterfall/start",  HTTP_POST, h_start_async);'
need web_waterfall.c 1 '    reg(server, "/api/waterfall/stop",   HTTP_POST, h_stop_async);'
need web_waterfall.c 1 '    reg(server, "/api/waterfall/clear",  HTTP_POST, h_clear_async);'
need web_waterfall.c 1 '    reg(server, "/api/waterfall/segment/delete", HTTP_POST, h_segdel_async);'
need web_waterfall.c 3 '&s_ctl_active, WF_CTL_MAX); }'
# WP10 (1.2.30): P3-7 — сначала обрезка первой строки, потом счётчик «(+N)»; P3-8 — отказ загрузки offload виден; Старт/Стоп показывают не-200
need ../web/index.html 1 '.replace(/\n/g," ⏎ ");'   # журнал обмена: полный текст ответа (без обрезки), для сохранения в файл
need ../web/index.html 1 'a.download="atomspectra-exchange-"'
need ../web/index.html 1 'onclick="clearLog()"'
need ../web/index.html 1 'document.getElementById("log-head").onclick=function(){'   # ровно один обработчик (дубль был в первой версии)
need ../web/index.html 1 'new Blob(["# "+d.toLocaleString()+"\n"+logEl.textContent]'
need ../web/waterfall.html 2 ' }).catch(function(){oflSetMsg(t("ofl.err"),"err");});'
need ../web/waterfall.html 1 'if(!r.ok)lg("start: HTTP "+r.status);'
need ../web/waterfall.html 1 'if(!r.ok)lg("stop: HTTP "+r.status);'
# Разбор кода 1.2.30 (release-gate-1.2.30-code.md): P2-1 обрыв → закрыть сессию, P3-1 Очистка не поверх выдачи, P3-2 счётчик до complete, P3-3 ядро 1,
# P2-2 журнал: обрыв потока = ESP_FAIL (не «успешный» 200), P2-3 клиент окна с токеном, P1-1 режим шва по sha256
need web_waterfall.c 1 '    if (rc != ESP_OK) httpd_sess_trigger_close(j.req->handle, httpd_req_to_sockfd(j.req));'
need web_waterfall.c 1 '    if (s_dl_active) return wf_dl_busy(req);'
need web_waterfall.c 1 '    if (xTaskCreatePinnedToCore(wf_dl_task, "wf_dl", WF_DL_STACK, j, 5, NULL, 1) != pdPASS) {'
need debug_log_ring.c 2 'err = ESP_FAIL; break; }'
need ../scripts/waterfall_n42.py 1 'headers={"X-CSRF-Token": tok}'
need ../scripts/wf_pull_client.py 1 '        return h is not None and h == hashlib.sha256(blob).hexdigest()'
# Живой гейт 1.2.30 (WP5): DONE — штатный конец среза, не обрыв (иначе каждый ответ журнала завершался ESP_FAIL без финального чанка)
need debug_log_ring.c 1 '        if (cr == DBGLOG_CHUNK_DONE) break;'
# Живой гейт 1.2.30: не хватило внутренней RAM под стек задачи — откат на прежнее синхронное поведение, а не отказ; стеки уменьшены
need web_waterfall.c 1 '        esp_err_t rc = h(cp);'
need web_waterfall.c 1 '#define WF_DL_STACK       6144'
need ota_github_client.c 1 '            sync_fb = true;'
need ota_github_client.c 1 '#define OTA_GH_CHK_STACK 6144'
# #59 (1.2.30): настройка «всегда читать калибровку из прибора» — NVS, API, UI и оба решения в usb_host_cdc (нужен -cal; применять ли коэффициенты)
need boot_config.c 1 '    out->calib_always_from_device = get_flag(h, "cal_al");'
need boot_config.c 1 '    e |= nvs_set_u8(h, "cal_al", in->calib_always_from_device ? 1 : 0);'
need web_server.c 1 '        bc.calib_always_from_device = cJSON_IsTrue(it);'
need usb_host_cdc.c 1 '        calib_autoread_needed_pref(spectrum_calibration_is_missing(), spectrum_serial_is_missing(), boot_config_calib_always()))'
need usb_host_cdc.c 1 '    spectrum_calib_set_serial_only(calib_request_serial_only(spectrum_calibration_is_missing(), boot_config_calib_always()));'
need ../web/system.html 1 '  calib_always:document.getElementById("bc-calib-always").checked'
# #60 (1.2.30): у строки журнала прибора — время ПРИЁМА (запись, выдача, страница), а не «сейчас»
need usb_host_cdc.c 1 '    s_devlog_ms[slot] = (uint32_t)(esp_timer_get_time() / 1000);'
need usb_host_cdc.c 1 '            tms = s_devlog_ms[slot];'
need usb_host_cdc.c 1 '"%s{\"seq\":%" PRIu32 ",\"t\":%" PRIu32 ",\"text\":\"",'
need ../web/index.html 1 'lg("← "+t,(typeof r.up_ms==="number"&&typeof e.t==="number")?new Date(Date.now()-((r.up_ms-e.t)>>>0)):undefined)'
# LK-05 (1.2.30): httpd ждёт прибор на -inf / -tc_pot? не дольше 1 с на команду (было 2 с)
need web_server.c 1 '#define SETTINGS_RAW_WAIT_MS 1000'
need web_server.c 2 '    for (int waited = 0; waited < SETTINGS_RAW_WAIT_MS; waited += 50) {'
# #RST-TAIL (1.2.30): строка водопада перед -rst — вызов в трёх точках отправки (отступ в строке: закомментированный вызов не считается)
# и ДО передачи команды прибору; wf_task берёт решение из wf_tail_plan.h и отдаёт s_tail_done после оборота.
need web_server.c   1 '    (void)spectrogram_flush_tail(1200);'
need tcp_bridge.c   1 '        if (saw_rst) (void)spectrogram_flush_tail(1200);'
need usb_host_cdc.c 1 '    if (cmd_is_device_reset(cmd0)) (void)spectrogram_flush_tail(1200);'
need spectrogram.c  1 '        if (!wf_tail_should_row(tail_force, now_time, s_prev_time, iv)) continue;'
need spectrogram.c  1 '        if (tail_force) { s_tail_force = false; s_tail_inflight = true; }'
need spectrogram.c  1 '        if (s_tail_inflight) { s_tail_inflight = false; if (s_tail_done) xSemaphoreGive(s_tail_done); }'
need spectrogram.c  1 '    xSemaphoreGive(s_commit_sig);            // разбудить wf_task вне очереди'
rt=$(awk '/^    \(void\)spectrogram_flush_tail/{f=FNR} f && FNR==f+1 && /bool sent = usb_host_cdc_send\(pkt\.data/{ok=1} END{print (!f || ok) ? "ok" : "bad"}' web_server.c)
[ "$rt" = ok ] || { echo "WIRING FAIL web_server.c: spectrogram_flush_tail must precede usb_host_cdc_send in handle_reset ($rt)"; RC=1; }
rt=$(awk '/^        if \(saw_rst\) \(void\)spectrogram_flush_tail/{f=FNR} f && FNR==f+1 && /int rc = usb_host_cdc_send\(buf, n\);/{ok=1} END{print (!f || ok) ? "ok" : "bad"}' tcp_bridge.c)
[ "$rt" = ok ] || { echo "WIRING FAIL tcp_bridge.c: spectrogram_flush_tail must precede usb_host_cdc_send ($rt)"; RC=1; }
rt=$(awk '/^    if \(cmd_is_device_reset\(cmd0\)\) \(void\)spectrogram_flush_tail/{f=FNR} f && FNR==f+1 && /int rc = usb_host_cdc_send\(pkt\.data, pkt\.len\);/{ok=1} END{print (!f || ok) ? "ok" : "bad"}' usb_host_cdc.c)
[ "$rt" = ok ] || { echo "WIRING FAIL usb_host_cdc.c: spectrogram_flush_tail must precede usb_host_cdc_send ($rt)"; RC=1; }
# F-09 (класс): результат cJSON_PrintUnformatted проверен на NULL в 3 строках после вызова (все *.c)
f09=$(awk 'match($0,/char \*[A-Za-z_]+ = cJSON_PrintUnformatted\(/){v=substr($0,RSTART+6,RLENGTH-6); sub(/ =.*/,"",v); k=3; want=FILENAME":"FNR; next}
  k>0 { if (index($0,"!" v) || index($0, v " ?")) k=0; else if (--k==0) print want }' ./*.c)
[ -z "$f09" ] || { echo "WIRING FAIL cJSON_PrintUnformatted without NULL check: $f09"; RC=1; }
# #MX-3..#MX-12 (1.2.31): замечания пользователя по странице спектра, «Системе» и «Мониторингу»
need ../web/system.html  1 "spark(\"cpuc\",cpu,'#f0c45a',{mn:0,mx:100});"              # MX-3: CPU прибора в шкале 0–100 %
need ../web/index.html   1 'acqSince=(acqHint===false)?now-6000:now;'                     # MX-4: начальное состояние из acq_intent
need ../web/index.html   0 '<span class="ac">CPS <span>'                                   # MX-5: CPS не дублируется в строке статуса
need ../web/index.html   1 '<div class="status" id="status" data-i18n="status.connecting" style="font-size:11.5px;'   # MX-6
need ../web/common-time.js 1 'if (d > 0) p.push(d + u.d);'                       # MX-7: дни в длительности словами (fmtDur, OPT-11: перенесено с главной)
need ../web/common-time.js 1 'var prefix = d > 0 ? d + u.d + " " : "";'          # MX-7: дни в часах ЧЧ:ММ:СС (fmtClock)
need ../web/index.html   1 'var sma=(typeof d.sma==="number")?d.sma:null;var es=document.getElementById("big-sma"),W=(sma===null)?0:Math.round(d.sma_w);'   # MX-8 / #MX-14 (1.2.36): SMA и охват sma_w - с платы (кольцо monitor.c), вместо smaCps()
need ../web/index.html   0 'smaHist'                                                      # #MX-14: своя история вкладки удалена
need ../web/index.html   0 'smaCps('                                                      # #MX-14: расчёт в браузере удалён
need ../web/index.html   1 'fetch("/api/spectrum/meta.json?w="+smaWinSec())'              # #MX-14: окно W уходит на плату тем же запросом meta
need ../web/index.html   1 'es.textContent=(sma===null)?"":" / "+sma.toFixed(2);'        # 1.2.36: после дроби только число (единица CPS - статический span)
need ../web/index.html   0 'aswf-sma-win'                                                # 1.2.36: множитель «отсчётов» убран, окно = интервал усреднения
need ../web/index.html   1 'if(st.iv>=1)iv=Math.min(Math.floor(st.iv),86400)}catch(e){}return iv}'   # 1.2.36: smaWinSec = iv из aswf-mon-sess
need ../web/monitor.html 1 'id="interval" min="1" max="86400" step="1" value="10" oninput="saveState()"'   # 1.2.36: смена интервала сразу в localStorage (до Старта)
need ../web/monitor.html 1 'var smaAll=smaRatioSeries('                                 # #MX-14: «Мониторинг» - то же определение (common-time.js)
need ../web/monitor.html 0 'acc+=samples[i].cps'                                          # #MX-14: среднее отношений вёдер удалено
need ../web/common-time.js 1 'out.push(sd > 0 ? sc / sd : 0);'                            # #MX-14: smaRatioSeries - отношение сумм
need ../web/monitor.html 1 'aswf-sma-win"),e=document.getElementById("smaWin")'           # MX-8: окно SMA помнится
need ../web/index.html   1 'function visN(N){if(!xRange||!isKev||!calib)return N;'          # MX-9
need ../web/index.html   1 'var bw=PW/visN(N);'                                            # MX-9: курсор в том же масштабе
need ../web/index.html   1 'Math.pow(v/mx,1/Math.E)'                                       # MX-10
need ../web/index.html   1 'var NM=(NV>=N)?N-1:NV;'                                         # MX-11: канал переполнения вне масштаба
need monitor.c           1 'ring_push_dose(tsec, counts - prev_counts, (uint16_t)dur, t_dc, dose_ws);'   # MX-12
need web_server.c        1 'smp[i].t_dc < 0 ? "-" : ""'                                     # MX-12: знак при -0.x
need ../web/monitor.html 1 'pushBase(pend[k][0],pend[k][1],pend[k][2],pend[k][3]);'         # MX-12
need ../web/monitor.html 1 '"rel_err_pct","temp_c"]'                                       # MX-12: колонка в CSV
# 1.2.31: импорт фона в «Сохранённые» (design-1.2.31-import.md) + дефект «любой POST /api/saved/* удаляет запись»
need web_server.c 1 '#include "spectrum_import_plan.h"'
line web_server.c 1 'int idx = saved_delete_index(req->uri);'                       # удаление — только /api/saved/<i>/delete, иначе 404
need web_server.c 1 'HTTP_POST, handle_import,'                                    # маршрут импорта НЕ под /api/saved/* (там POST = удаление)
need web_server.c 1 '"/api/import"'
# 3A: старая проверка content_len != SPEC_IMPORT_SIZE заменена выбором import_len_action (OK / DRAIN_400 / CLOSE_400)
need web_server.c 1 'switch (import_len_action(req->content_len)) {'
need web_server.c 1 'if (import_recv_deadline_passed(t0, esp_timer_get_time())) return false;'      # срок приёма
need web_server.c 1 'bool whole = import_drain_input(req, req->content_len);'                      # F1: DRAIN_400 знает, дочитано ли
need web_server.c 1 'bool whole = import_drain_input(req, IMPORT_LINGER_MAX_BYTES);'               # F2: слив перед 403 по CSRF
need web_server.c 2 'return whole ? ESP_OK : ESP_FAIL;'                                            # 403 и DRAIN_400: недочитано -> закрыть сокет
need web_server.c 2 'if (!whole) httpd_resp_set_hdr(req, "Connection", "close");'
need web_server.c 1 'return whole && remaining == 0;'                                              # признак полноты слива
need web_waterfall.c 1 'return unread ? ESP_FAIL : ESP_OK;'                                       # busy-503 с непрочитанным телом
need spectrum.c   1 'cleanup_orphan_import_tmp();  // #AWF-F4: осиротевший spec/import.tmp (обрыв питания при импорте)'
need web_server.c 1 'n42_start_iso_utc('
need web_server.c 1 'return web_async_run(req, handle_import_job);'                # асинхронная задача, общий счётчик s_dl_active
need web_server.c 1 'uint8_t *b = heap_caps_malloc(SPEC_IMPORT_SIZE, MALLOC_CAP_SPIRAM);'      # буферы — в PSRAM, не во внутренней RAM
need web_server.c 1 'spectrum_data_t *sp = heap_caps_malloc(sizeof(*sp), MALLOC_CAP_SPIRAM);'
need web_server.c 2 'if (r == HTTPD_SOCK_ERR_TIMEOUT && !ota_timeout_budget_exceeded(++streak, OTA_MAX_CONSECUTIVE_TIMEOUTS)) continue;'   # приём + слив (дубль 3A)
need web_server.c 1 'imp_err_t e = rx ? spectrum_import_decode(b, SPEC_IMPORT_SIZE, sp) : IMP_BAD_SIZE;'
need web_server.c 1 'if (!http_io_gate_enter_wait_or_503(req, SAVED_FLASH_GATE_WAIT_MS)) { free(sp); return ESP_OK; }'   # запись под воротами flash
need web_server.c 1 'int idx = spectrum_import_to_flash(sp);'
imp=$(awk '{c=$0; sub(/\r$/,"",c)} c ~ /^static esp_err_t handle_import\(httpd_req_t \*req\)/{f=1} f && c ~ /csrf_ok\(req\)/{ok=1} f && c == "}"{f=0} END{print ok ? "ok" : "bad"}' web_server.c)
[ "$imp" = ok ] || { echo "WIRING FAIL web_server.c: handle_import without csrf_ok ($imp)"; RC=1; }
need web_server.c 1 '",\"calib_set\":%s,\"saved_at\":%ld",'                          # дата набора в JSON записи (экспорт → импорт на другой плате)
need web_server.c 1 'memcmp(tag, "IMP:", 4) ? "" : ",\"imp\":true"'                # метка импорта в /api/list
need spectrum.c   2 'int idx = spec_find_free_slot(path, sizeof(path));'            # «Сохранить» и импорт — один поиск слота
need spectrum.c   1 'if (!flash_quiet_writer_lock(flash_quiet_writer_lock_ticks())) return -5;'
need spectrum.c   1 'if (idx >= 0 && !atomic_write_snapshot(SPEC_DIR "/" IMPORT_TMP_NAME, path, sp)) idx = -3;'   # tmp+rename, имя не spec_*
need web_waterfall.c 1 'return wf_dl_async(req, h, &s_dl_active, WF_DL_MAX);'      # web_async_run: тот же счётчик, максимум одна задача 6144 Б
need ../web/saved.html 1 'r=await post("/api/import",{headers:{"Content-Type":"application/octet-stream"},body:body});'
need ../web/saved.html 1 'document.getElementById("btn-imp").onclick=function(){document.getElementById("imp-file").click();};'
need ../web/saved.html 1 "(s.imp?'<span"                                         # метка «фон, импорт» в списке
need ../web/saved.html 1 'dv.setUint32(124,impCrc(u8,128,buf.byteLength,impCrc(u8,0,124,0)),true);'
need ../web/index.html 1 'var ob=ovlView();'                                       # оверлей рисуется по энергетической шкале живого спектра
need ../web/index.html 1 'ovlKey=k;ovlCache=rb?rebinByEnergy(overlayBins,overlayCal,calib):overlayBins;'
need ../web/index.html 1 'overlayCal=(r.calib_set!==false&&calibArraySet(r.calib))?r.calib:null;'
need ../web/index.html 0 'yO=new Array(overlayBins.length)'
need ../web/index.html 1 "(s.imp?' <span"
# разбор 1.2.31: подписи увеличенного окна — по видимым каналам; serial импорта без кавычки и обратного слэша
need ../web/index.html 1 'var ch=Math.floor((cx-PL)/PW*visN(N)); if(ch<0||ch>=N)return null;'
need ../web/saved.html 1 '.replace(/[^\x20-\x7E]|["\\]/g,"?").substring(0,43);}'
need spectrum_import_plan.h 1 "b[72 + n] == '\"'"
need ../web/index.html 1 'function setCps(v,ns){isCps=v;if(!ns)viewSave();'
need ../web/index.html 1 'setLog(d(o.log,isLog),1);setCps(d(o.cps,isCps),1);setKev(d(o.kev,isKev),1)'
need ../web/index.html 1 'function setKev(v,ns){isKev=v;if(!ns)viewSave();'
need ../web/index.html 1 'function viewRestore(){'
need ../web/index.html 1 'onclick="toggleOverlay('
need ../web/index.html 1 'localStorage.getItem("aswf-ovl")'
need ../web/index.html 0 'X.setLineDash([5,4]);X.beginPath();for(var i=0;i<Math.min(yO.length'
need wifi_manager.c 1 'wifi_setup_should_return(s_unverified, s_got_ip_this_boot, s_setup_auth_fails, elapsed_s)'
need wifi_manager.c 1 'wifi_setup_fail_bump(s_setup_auth_fails, reason)'
need wifi_manager.c 1 'nvs_set_u8(nvs, "unver", 1);'
need wifi_manager.c 1 'nvs_erase_key(uv, "unver");'
need wifi_manager.c 1 'if (s_unverified) return_to_setup_and_reboot();'
# #AWF-WIFI-1 (проход B W-1/W-2/W-4): срок таймера - из wifi_fallback_timeout_s(s_unverified); значение s_unverified
# читается из NVS ДО старта таймера; GOT_IP останавливает таймер ДО сброса s_unverified. Строки целиком (не подстроки, не комментарии).
lnum() {   # lnum <file> <строка кода целиком, без отступа> -> номер первой такой строки (0 - нет)
    awk -v s="$2" '{c=$0; sub(/\r$/,"",c); sub(/^[ \t]+/,"",c)} c == s {print NR; exit} END{if(!NR)print 0}' "$1" | head -1
}
T_RD='s_unverified = (unver != 0);'
T_ST='esp_timer_start_once(s_fallback_timer, (uint64_t)wifi_fallback_timeout_s(s_unverified) * 1000000);'
line wifi_manager.c 1 "$T_RD"
line wifi_manager.c 1 'nvs_get_u8(nvs, "unver", &unver);'
line wifi_manager.c 1 "$T_ST"
line wifi_manager.c 1 '(const char *)wifi_config.sta.ssid, (unsigned)wifi_fallback_timeout_s(s_unverified), (int)s_unverified);'
need wifi_manager.c 0 'WIFI_RECONNECT_FALLBACK_S) * 1000000'
a=$(lnum wifi_manager.c "$T_RD"); b=$(lnum wifi_manager.c "$T_ST")
{ [ "$a" -gt 0 ] && [ "$b" -gt 0 ] && [ "$a" -lt "$b" ]; } || { echo "WIRING FAIL wifi_manager.c: чтение s_unverified (:$a) должно быть строго раньше старта таймера (:$b)"; RC=1; }
line wifi_manager.c 1 'if (s_fallback_timer) esp_timer_stop(s_fallback_timer);'
line wifi_manager.c 1 's_unverified = false;'
line wifi_manager.c 1 'if (s_got_ip_this_boot) return;   // #AWF-WIFI-1 (W-4): IP уже была — таймер опоздал к GOT_IP'
c=$(lnum wifi_manager.c 'if (s_fallback_timer) esp_timer_stop(s_fallback_timer);'); d=$(lnum wifi_manager.c 's_unverified = false;')
{ [ "$c" -gt 0 ] && [ "$d" -gt 0 ] && [ "$c" -lt "$d" ]; } || { echo "WIRING FAIL wifi_manager.c: остановка fallback-таймера (:$c) должна быть раньше сброса s_unverified (:$d)"; RC=1; }
# 1.2.36 OPT-1 (F): одна формула живого времени (live_time.h); две static-копии удалены, 4 вызова в web_server.c + 2 в кэше
need web_server.c 0 'static float compute_live_time('
need spectrum_http_cache.c 0 'static float compute_live_time('
need web_server.c 4 'spectrum_compute_live_time(sp)'
need spectrum_http_cache.c 1 'spectrum_compute_live_time(sp)'   # после B один вызов — в build_meta (раньше два: full+meta)
need spectrum.c 1 'return live_time_calc(sp->total_time_sec, sp->total_counts, sp->lost_impulses,'
# 1.2.36 H12 (D): /api/device — поля хвоста снимка в локальной структуре (spectrum_calib_meta.h), без буфера sizeof(spectrum_data_t)
need web_server.c 1 'bool have_sp = spectrum_get_calib_meta(&m);'
need web_server.c 0 'spectrum_get_meta('
need spectrum.c 1 'spectrum_calib_meta_from(&s_spectrum, m);'
# 1.2.36 Commit 0: счётчики замера «до/после» в /api/system (наблюдаемость; спека §3 Commit 0)
need web_server.c 1 '"spectrum_build_us"'
# 1.2.36 P1 (E): пропуск записи неизменного current.bin — проверка критерия в одноразовом пути + сброс признака в 4 местах (Сброс, consume_abort, отказ записи, порционный путь)
# С-1 (gate 1.2.36 pass1): шаг пропуска повторяет снятие reset.mark тем же reset_mark_clear_if_current
need spectrum.c 1 'if (autosave_skip_step(&s_as_last, snap_crc, snap_gen, reset_mark_clear_if_current)) {'
need spectrum.c 1 's_as_last = (as_last_t){ true, snap_crc, snap_gen };'
need spectrum.c 4 's_as_last.have = false;'
need web_server.c 1 '"autosave_skips"'
# 1.2.36 H5 (C): медленная часть /api/system (обходы кучи, стеки) и кэш esp_littlefs_info — одна проверка срока ttl_plan.h
need web_server.c 2 'ttl_expired('
need web_server.c 1 'if (ttl_expired(s_slow_at, slow_now, SYS_SLOW_PERIOD_US)) {'
need web_server.c 1 '"heap_walk_age_ms"'
# 1.2.36 H2/H3/H4 (B): поля meta — один раз за пересборку (spec_meta_fmt.h, ОДИН build_meta), полный JSON — лениво из send_json, старые build_json_* и per-build free() удалены
need spectrum_http_cache.c 1 'spec_meta_in_fill(&in, sp, &lv);'
need spectrum_http_cache.c 2 'spec_meta_fmt('
need spectrum_http_cache.c 1 'build_meta(s_sp_build, &s_meta_build, &s_meta_build_cap, &nml, &na, &nb);'
need spectrum_http_cache.c 1 'if (!spec_json_need_build(s_json_have, s_json_render, rc)) {'
need spectrum_http_cache.c 2 'spec_json_full_fmt('
need spectrum_http_cache.c 0 'build_json_full('
need spectrum_http_cache.c 0 'build_json_meta('
need spectrum_http_cache.c 0 'free(s_meta);'
need spectrum_http_cache.c 0 'free(s_json);'
need web_server.c 1 '"spectrum_json_builds"'
# 1.2.36 H1 (A): seqlock — КАЖДАЯ секция SPEC_LOCK двигает seq (нечётный внутри) с барьерами в макросах; кэш снимает
# снимок seq_copy() без мьютекса, не вышло — прежний путь под замком (счётчики в /api/system). Барьеры на x86 host-тест не видит.
line spectrum.c 1 '#define SPEC_LOCK()   do { if (s_spec_lock) xSemaphoreTake(s_spec_lock, portMAX_DELAY); s_spec_seq++; __sync_synchronize(); } while (0)'
line spectrum.c 1 '#define SPEC_UNLOCK() do { __sync_synchronize(); s_spec_seq++; if (s_spec_lock) xSemaphoreGive(s_spec_lock); } while (0)'
need seq_copy.h 2 '__sync_synchronize();'
need spectrum.c 1 'if (seq_copy(&s_spec_seq, out, &s_spectrum, sizeof(*out), SPEC_SEQ_TRIES,'
need spectrum.c 1 's_spec_seq_fallback++;'
need spectrum_http_cache.c 1 'spectrum_get_snapshot_seq(s_sp_build)'
need spectrum_http_cache.c 0 'spectrum_get_snapshot(s_sp_build)'
need web_server.c 1 '"spec_seq_retry"'
need web_server.c 1 '"spec_seq_fallback"'
# 1.2.36 #MX-14 (G): SMA из кольца платы - meta.json?w=W: обход кольца под s_lock без копии, хвост sma/sma_w/sma_n, ответ чанками с терминатором
need monitor.c 1 'k = mon_window_sum(s_ring, s_cap, s_head, s_count, w_sec, &sc, &sd);'
need meta_w_handler.h 1 'monitor_window(w, &sc, &sd, &n);'
# З-5 (gate 1.2.36 pass1): ?w= - буфер META_Q_MAX, длинный запрос 414, плохой w 400, разбор только meta_w_parse
need meta_w_handler.h 1 'char q[META_Q_MAX];'
# pass2 З-1: 414 только когда буфер не вместил сам w (META_W_CUT); 400 нет (w не целое - прежний meta); обрезанная строка разбирается
need meta_w_handler.h 1 'if (qe == ESP_OK || qe == ESP_ERR_HTTPD_RESULT_TRUNC) mw = meta_w_parse(q, qe != ESP_OK, &w);'
need meta_w_handler.h 1 'if (mw == META_W_CUT) { httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "w: query too long"); return ESP_FAIL; }'
need meta_w_handler.h 0 'HTTPD_400_BAD_REQUEST'
need meta_w_handler.h 0 'if (ql >= sizeof(q))'
need meta_w_handler.h 1 'return spectrum_http_send_meta_sma_dose(req, sc, sd, n, &dz);'
need meta_w_handler.h 1 'monitor_dose_live(w, &dz);'
need monitor.c 1 'float dose_ws = dose_sample(tsec, prev_time, resync);'
# #GZ-T (1.2.37-rc90, тест): сжатие тел спектра - крючки кэша (сам кэш и httpd не сжимают: tdefl только в spectrum_gz.c), NVS gz_lvl, API, метрики, UI
need spectrum_http_cache.c 1 'if (gz && s_gz_try && s_gz_try(req, ch, &ge)) return ge;'
need spectrum_http_cache.c 3 's_gz_try'
need spectrum_http_cache.c 0 'SPECTRUM_HTTP_KIND'
need spectrum_http_cache.c 0 'spectrum_http_cache_pin'
need spectrum_http_cache.c 0 's_gz_note'
need spectrum_http_cache.c 0 'mz_deflate'
need spectrum_http_cache.c 0 'tdefl_compress'
need web_server.c 0 'mz_deflate'
need web_server.c 0 'tdefl_compress'
need spectrum_http_cache.c 2 'ep = s_ep;   // D2 (K2): копия значением, пара с bins этого же поколения'
need spectrum_http_cache.c 0 'tdefl_'
need web_server.c 0 'tdefl_'
need spectrum_gz.c 1 'int r = gz_deflate(out, cap, body, len, level, out_len);'
need spectrum_gz.c 1 'esp_err_t be = httpd_req_async_handler_begin(req, &ar);'
need spectrum_gz.c 1 'httpd_req_async_handler_complete((httpd_req_t *)j->req);'
need spectrum_gz.c 1 'while (gz_job_pop(&j)) {'
need boot_config.c 1 'esp_err_t grc = nvs_get_u8(h, "gz_lvl", &gv);'
need boot_config.c 1 'e |= nvs_set_u8(h, "gz_lvl", spec_gz_level_from_nvs(true, in->gzip_level));'
need web_server.c 1 'spectrum_gz_init();'
need web_server.c 1 'spectrum_gz_sync();'
need web_server.c 1 'spectrum_gz_add_metrics(root);'
need ../web/system.html 1 'return await bootPost({gzip_level:'   # saveGzip
# #DOSE-TOGGLE (1.2.37): тумблер «Считать живую МД» — NVS (чтение/запись), API, UI и освобождение буферов в dose_sample
need boot_config.c 1 '    e |= nvs_set_u8(h, "dose_live_en", in->dose_live_enabled ? 1 : 0);'
need boot_config.c 1 '        out->dose_live_enabled = boot_flag_default_on_from_nvs(drc == ESP_OK, dv);'
need web_server.c 1 '        bc.dose_live_enabled = cJSON_IsTrue(it);'
need ../web/system.html 1 ' return await bootPost({dose_live:el.checked},statusId||"dose-live-status");'
need monitor.c 1 '        dose_buffers_release();'

need spectrum_http_cache.c 1 'sl = spec_meta_sma_tail(tail, 128, sum_counts, sum_dur, n);'
need web_server.c 1 'return meta_w_handle(req);'   # pass2 З-3: обработчик - в meta_w_handler.h (исполняет test_glue_meta_w)
need spectrum_http_cache.c 1 'size_t tl = spec_meta_sma_tail('
need spectrum_http_cache.c 1 'e = httpd_resp_send_chunk(req, NULL, 0);'
# 1.2.37: TCP_NODELAY на сокете запроса ПЕРЕД httpd_resp_send спектральных ответов (spectrum.json: кэш + сборка, бинарный /api/spectrum) - ровно три вызова хелпера, флаг не снимается; исполняет test_glue_cache (glue_nodelay)
line spectrum_http_cache.c 3 'spectrum_http_nodelay(req);'
need spectrum_http_cache.c 1 'setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one)'
# 1.2.37: X-Hist-Age-Ms - ровно у двух отправок spectrum.json (из кэша и после сборки), значение - геттер платы под DIAG_LOCK; исполняет test_glue_cache (glue_hist_age)
line spectrum_http_cache.c 2 'set_hist_age_hdr(req, age_hdr, sizeof(age_hdr));'
need spectrum_http_cache.c 1 'httpd_resp_set_hdr(req, "X-Hist-Age-Ms", hdr);'
need usb_host_cdc.c 1 '    return acq_hist_age_ms(now, hist_ts);'
# release-gate 1.2.36 passA З-1: КАЖДАЯ запись s_spectrum - под SPEC_LOCK (или из мест вызова под ним): spec_writers.py с
# фиксированными флагами. Законные исключения ровно два: memset в spectrum_init (до запуска httpd, одна задача) и const-геттер
# spectrum_get_current (--scan-dir: приведений без const - 0). Отказ - одна строка WIRING FAIL с перечнем UNLOCKED.
sw=$(python3 "$HERE/spec_writers.py" spectrum.c --scan-dir . --allow-fn spectrum_init --const-getter spectrum_get_current 2>&1); swrc=$?
if [ "$swrc" -ne 0 ]; then
    echo "WIRING FAIL spec_writers rc=$swrc: $(grep '^# summary' <<<"$sw") $(grep -P '\tUNLOCKED\t' <<<"$sw" | cut -f1,2 | tr '\t\n' ': ')"; RC=1
fi
[ "$RC" -eq 0 ] && echo "wiring: OK"
exit $RC
