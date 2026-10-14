#!/usr/bin/env bash
# 1.2.36 OPT-1 (спека stage2 §3 F): мутанты live_time_calc() (main/live_time.h).
# Каждый точечный мутант обязан покраснить РОВНО названный тест (имя — в строке FAIL), baseline — 0 FAIL.
#   MF1: убрать `if (dead > total) dead = total;`   -> только live_clamp_hi
#   MF2: `+ 1.0f` -> `+ 0.0f` в tau                 -> только live_random_eq
#   MF0 (грубый): `total - dead` -> `total + dead`   -> live_random_eq и live_clamp_hi
# Строки "INFO live_random_eq: samples=N differ=D" в выводе мутанта — число образцов, на которых
# дефектная и исправная версии дают РАЗНЫЙ результат (#SA-3a).
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/live_time.h
mt_init "$H"
mt_mut MF1_no_clamp_hi  live_clamp_hi  "$H" '/if (dead > total) dead = total;/d'
mt_mut MF2_tau_no_plus1 live_random_eq "$H" 's/(float)fall + 1.0f/(float)fall + 0.0f/'
mt_mut MF0_sign_flip    "live_random_eq live_clamp_hi" "$H" 's/return total - dead;/return total + dead;/'
mt_base
mt_done
exit $?
