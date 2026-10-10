#!/usr/bin/env bash
# release-gate 1.2.36 pass1 С-2: мутанты СКЛЕЙКИ (сам main/*.c в test_glue_*.c), которые host-тесты чистых функций и
# wiring_check не видели. Каждый обязан покраснить РОВНО названный тест, baseline - 0 FAIL.
#   Y4: удалена строка s_meta_a_end = na; (spectrum_http_cache.c) -> spectrum.json 200 с пустым телом -> glue_json_body
#   Y5: предел окна SMA 60 с вместо ёмкости кольца (monitor.c)                                       -> glue_mon_window
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
C=../../main/spectrum_http_cache.c
M=../../main/monitor.c
mt_init "$C" "$M"
mt_mut Y4_no_a_end   glue_json_body   "$C" '/s_meta_a_end = na;/d'
mt_mut Y5_win_60     glue_mon_window  "$M" 's/if (w_sec > s_cap) w_sec = s_cap;/if (w_sec > 60) w_sec = 60;/'
mt_base
mt_done
exit $?
