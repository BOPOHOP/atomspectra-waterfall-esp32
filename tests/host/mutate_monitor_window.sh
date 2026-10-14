#!/usr/bin/env bash
# 1.2.36 #MX-14 (спека stage2 §3 G, §10): мутанты mon_window_sum() (main/monitor_window.h) и spec_meta_sma_tail() (main/spectrum_meta_fmt.h).
# Каждый точечный мутант обязан покраснить РОВНО названный тест (имя - в строке FAIL), baseline - 0 FAIL.
#   MG1: условие остановки `<` -> `<=`        -> только win_exact    (Σdur ровно W: лишний сэмпл)
#   MG2: индекс `head + cap - 1 - i` -> `head + cap - i` -> только win_wrap (сдвиг на сэмпл: ring с переворотом и разными counts)
#   MG3: убрана ветка sd==0 (sma:null)        -> только sma_tail_null
#   MG0 (грубый): остановка по Σdur выключена -> win_exact, win_overshoot, win_wrap
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/monitor_window.h
F=../../main/spectrum_meta_fmt.h
mt_init "$H" "$F"
mt_mut MG1_le_stop    win_exact       "$H" 's/sd < w_sec; i++/sd <= w_sec; i++/'
mt_mut MG2_idx_shift  win_wrap        "$H" 's/head + cap - 1 - i/head + cap - i/'
mt_mut MG3_no_null    sma_tail_null   "$F" 's/if (sd == 0) k = /if (0) k = /'
mt_mut MG0_nostop     "win_exact win_overshoot win_wrap" "$H" 's/sd < w_sec; i++/(sd < w_sec || 1); i++/'
mt_base
mt_done
exit $?
