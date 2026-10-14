#!/usr/bin/env bash
# 1.2.36 B (спека stage2 §3 B, R-3): мутанты spec_meta_fmt()/spec_json_full_fmt() (main/spectrum_meta_fmt.h) и spec_json_need_build() (main/spec_cache_gen.h).
# Каждый точечный мутант обязан покраснить РОВНО названный тест (имя — в строке FAIL), baseline — 0 FAIL.
#   MB1: `SPECTRUM_CHANNELS` -> `SPECTRUM_CHANNELS - 1` в meta  -> только meta_bytes_eq (полный JSON вырезает channels по смещениям)
#   MB2: `"],"` -> `"] ,"` в сборке полного JSON                -> только full_bytes_eq
#   MB3: spec_json_need_build всегда false                      -> только json_need_build
#   MB0 (грубый): `"live":%.1f` -> `%.2f`                       -> meta_bytes_eq и full_bytes_eq
# Строка "INFO spectrum_meta_fmt: samples=N differ_mb1_channels=.. differ_mb2_close=.. differ_mb0_live=.. differ_mb3_need_build_false=.."
# в выводе baseline — число образцов, на которых дефектная и исправная версии дают РАЗНЫЙ вывод (#SA-3a).
set -u
cd "$(dirname "$0")"
bash fixtures/check_old_json_sha.sh || exit 1
. ./mutlib.sh
H=../../main/spectrum_meta_fmt.h
G=../../main/spec_cache_gen.h
mt_init "$H" "$G"
mt_mut MB1_channels_minus1 meta_bytes_eq  "$H" 's/", SPECTRUM_CHANNELS);/", SPECTRUM_CHANNELS - 1);/'
mt_mut MB2_close_space     full_bytes_eq  "$H" 's/spec_w_bytes(&w, "\],", 2);/spec_w_bytes(\&w, "] ,", 3);/'
mt_mut MB3_need_never      json_need_build "$G" 's/return !have || json_render != render;/(void)have; (void)json_render; (void)render; return false;/'
mt_mut MB0_live_2dec       "meta_bytes_eq full_bytes_eq" "$H" 's/live\\":%\.1f/live\\":%.2f/'
mt_base
mt_done
exit $?
