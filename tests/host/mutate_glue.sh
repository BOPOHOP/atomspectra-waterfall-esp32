#!/usr/bin/env bash
# release-gate 1.2.36 pass1 С-2: мутанты СКЛЕЙКИ (сам main/*.c в test_glue_*.c), которые host-тесты чистых функций и
# wiring_check не видели. Каждый обязан покраснить РОВНО названный тест, baseline - 0 FAIL.
#   Y4: удалена строка s_meta_a_end = na; (spectrum_http_cache.c) -> spectrum.json 200 с пустым телом -> glue_json_body
#   Y5: предел окна SMA 60 с вместо ёмкости кольца (monitor.c)                                       -> glue_mon_window
#   MZ3/MZ4 (pass2 С-1): на отказе роста буфера spectrum.json не уменьшены s_json_users / s_readers -> glue_json_oom
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
C=../../main/spectrum_http_cache.c
M=../../main/monitor.c
mt_init "$C" "$M"
# Y4: make test останавливается на первом упавшем бинаре (test_glue_cache), test_glue_cache_oom после него не запускается
mt_mut Y4_no_a_end   glue_json_body   "$C" '/s_meta_a_end = na;/d'
# З-1: MZ1 - общий буфер снова теряется (как до правки), MZ2 - частный не освобождается -> ровно glue_json_oom
mt_mut MZ1_shared_lost glue_json_oom  "$C" 's/if (claimed) { s_json = buf; s_json_cap = cap; s_json_users--; }/if (claimed) s_json_users--;/'
mt_mut MZ2_private_leak glue_json_oom "$C" '/if (!claimed) free(buf);/d'
# pass2 С-1: на отказе роста не отдан захват общего буфера / счётчик читателей -> ровно glue_json_oom (тест счётчики только читает)
mt_mut MZ3_users_kept   glue_json_oom "$C" 's/if (claimed) { s_json = buf; s_json_cap = cap; s_json_users--; }/if (claimed) { s_json = buf; s_json_cap = cap; }/'
mt_mut MZ4_readers_kept glue_json_oom "$C" '/^            s_readers--;/d'
mt_mut Y5_win_60     glue_mon_window  "$M" 's/if (w_sec > s_cap) w_sec = s_cap;/if (w_sec > 60) w_sec = 60;/'
# 1.2.37 TCP_NODELAY (spectrum.json: кэш + сборка, бинарный /api/spectrum): каждый мутант обязан покраснить РОВНО glue_nodelay
#   ND1: нет вызова в ветке из кэша; ND2: нет вызова в ветке после сборки; ND3: нет вызова в send_bins;
#   ND4: флаг ставится в 0 вместо 1; ND5: флаг снимается сразу после httpd_resp_send (ветка из кэша)
mt_mut ND1_cached_no_call glue_nodelay "$C" 's/^        spectrum_http_nodelay(req);/        (void)req;/'
mt_mut ND2_build_no_call  glue_nodelay "$C" '0,/^    spectrum_http_nodelay(req);/s//    (void)req;/'
mt_mut ND3_bins_no_call   glue_nodelay "$C" '/"Content-Disposition", "inline"/{n;s/spectrum_http_nodelay(req);/(void)req;/}'
mt_mut ND4_one_zero       glue_nodelay "$C" 's/^    int one = 1;/    int one = 0;/'
mt_mut ND5_cleared_after  glue_nodelay "$C" '/^        esp_err_t e = httpd_resp_send(req, payload, len);/a\        { int z = 0; setsockopt(httpd_req_to_sockfd(req), IPPROTO_TCP, TCP_NODELAY, \&z, sizeof z); }'
# 1.2.37 X-Hist-Age-Ms (spectrum.json): каждый мутант обязан покраснить РОВНО glue_hist_age (замена, не удаление: иначе unused/-Werror и прогон на старом бинарнике)
#   HA1: нет заголовка в ветке из кэша; HA2: нет заголовка в ветке после сборки; HA3: значение всегда -1 (возраст не считается);
#   HA4: заголовок ставится дважды (ветка после сборки)
mt_mut HA1_cached_no_hdr glue_hist_age "$C" 's/^        set_hist_age_hdr(req, age_hdr, sizeof(age_hdr));/        (void)age_hdr;/'
mt_mut HA2_build_no_hdr  glue_hist_age "$C" 's/^    set_hist_age_hdr(req, age_hdr, sizeof(age_hdr));/    (void)age_hdr;/'
mt_mut HA3_always_m1     glue_hist_age "$C" 's/usb_host_cdc_hist_age_ms());/-1);/'
mt_mut HA4_build_twice   glue_hist_age "$C" '/^    set_hist_age_hdr(req, age_hdr, sizeof(age_hdr));/a\    set_hist_age_hdr(req, age_hdr, sizeof(age_hdr));'
mt_base
mt_done
exit $?
