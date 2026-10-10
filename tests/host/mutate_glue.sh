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
mt_base
mt_done
exit $?
