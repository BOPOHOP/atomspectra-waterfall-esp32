#!/usr/bin/env bash
# 1.2.36 H1 (спека stage2 §3 A): мутанты seq_copy() (main/seq_copy.h). Точечный краснит РОВНО названный тест, baseline 0 FAIL.
#   MA1: убрать проверку s1 == s2          -> только seq_torn_once ((void)s2 — иначе -Werror=unused-variable не даёт собрать мутанта)
#   MA2: не ждать нечётного seq           -> только seq_odd_stuck
#   MA0 (грубый): убрать все барьеры       -> host-тест зелёный (барьер на x86 не наблюдаем) — ловит якорь wiring (MA0_copy_nobar)
# Затем MA1/MA2 против многопоточного стресса (make stress): стресс обязан покраснеть (иначе он не умеет краснеть, #SA-3).
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
. ./mutlib_stress.sh
H=../../main/seq_copy.h
mt_init "$H"
mt_mut MA1_no_recheck  seq_torn_once "$H" 's/if (s1 == s2) return true;/(void)s2; return true;/'
mt_mut MA2_odd_ignored seq_odd_stuck "$H" 's/if (s1 \& 1) {/if (0) {/'
mt_mut MA0_no_barriers baseline      "$H" 's/__sync_synchronize();//'
mt_base
stress_mut MA1_stress "$H" 's/if (s1 == s2) return true;/(void)s2; return true;/'
stress_mut MA2_stress "$H" 's/if (s1 \& 1) {/if (0) {/'
mt_done
exit $?
