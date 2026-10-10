#!/usr/bin/env bash
# 1.2.36 P1 (спека stage2 §3 E): мутанты autosave_unchanged() (main/autosave_skip_plan.h).
# Каждый точечный мутант обязан покраснить РОВНО названный тест (имя — в строке FAIL), baseline — 0 FAIL.
#   ME1: убрать `l->have &&`            -> только write_first
#   ME2: убрать `&& l->gen == gen`      -> только write_after_reset
#   ME3: всегда false (return false)    -> только skip_same
#   ME4: убрать `l->crc == crc &&`      -> только write_changed_calib
#   ME0 (грубый): всегда true           -> write_first, write_after_reset, write_changed_calib
# Строка "INFO autosave_skip: samples differ_first=.. differ_reset=.. differ_changed=.. differ_same=.." в выводе baseline —
# число образцов, на которых дефектная (ME1/ME2/ME4/ME3 соответственно) и исправная версии дают РАЗНЫЙ результат (#SA-3a).
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/autosave_skip_plan.h
mt_init "$H"
# (void)-приведения сохраняют использование параметров: без них -Werror=unused-parameter ломает сборку мутанта, а не тест.
mt_mut ME1_no_have    write_first             "$H" 's/return l->have \&\& l->crc == crc/return l->crc == crc/'
mt_mut ME2_no_gen     write_after_reset       "$H" 's/return l->have \&\& l->crc == crc \&\& l->gen == gen;/(void)gen; return l->have \&\& l->crc == crc;/'
mt_mut ME3_always_off skip_same               "$H" 's/return l->have \&\& l->crc == crc \&\& l->gen == gen;/(void)l; (void)crc; (void)gen; return false;/'
mt_mut ME4_no_crc     write_changed_calib     "$H" 's/return l->have \&\& l->crc == crc \&\& l->gen == gen;/(void)crc; return l->have \&\& l->gen == gen;/'
mt_mut ME0_always_on  "write_first write_after_reset write_changed_calib" "$H" 's/return l->have \&\& l->crc == crc \&\& l->gen == gen;/(void)l; (void)crc; (void)gen; return true;/'
mt_base
mt_done
exit $?
