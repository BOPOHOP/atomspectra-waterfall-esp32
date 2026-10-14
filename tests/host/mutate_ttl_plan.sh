#!/usr/bin/env bash
# 1.2.36 H5 (спека stage2 §3 C): мутанты ttl_expired() (main/ttl_plan.h).
# Каждый точечный мутант обязан покраснить РОВНО названный тест (имя — в строке FAIL), baseline — 0 FAIL.
#   MC1: `>` -> `>=`                -> только ttl_inside   (now-at == ttl не устарело)
#   MC2: убрать `at_us == 0 ||`     -> только ttl_first_call
#   MC0 (грубый): всегда false      -> ttl_first_call и ttl_after
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/ttl_plan.h
mt_init "$H"
mt_mut MC1_ge        ttl_inside     "$H" 's/now_us - at_us > ttl_us;/now_us - at_us >= ttl_us;/'
mt_mut MC2_no_zero   ttl_first_call "$H" 's/return at_us == 0 || now_us - at_us > ttl_us;/return now_us - at_us > ttl_us;/'
mt_mut MC0_never     "ttl_first_call ttl_after" "$H" 's/return at_us == 0 || now_us - at_us > ttl_us;/(void)at_us; (void)now_us; (void)ttl_us; return false;/'
mt_base
mt_done
exit $?
