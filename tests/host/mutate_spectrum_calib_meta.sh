#!/usr/bin/env bash
# 1.2.36 H12 (спека stage2 §3 D): мутанты spectrum_calib_meta_from() (main/spectrum_calib_meta.h).
# Каждый мутант обязан покраснить РОВНО названный тест (имя — в строке FAIL), baseline — 0 FAIL.
#   MD1: не копировать calib_order                      -> только calib_meta_order
#   MD2: serial_number копировать 63 байта вместо 64    -> только calib_meta_serial
#   MD3: не копировать calib_valid                      -> только calib_meta_flags
#   MD4: не копировать valid                            -> только calib_meta_flags
#   MD5: не копировать calibration[]                    -> только calib_meta_calibration
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/spectrum_calib_meta.h
mt_init "$H"
mt_mut MD1_no_order      calib_meta_order       "$H" '/m->calib_order = s->calib_order;/d'
mt_mut MD2_serial_63     calib_meta_serial      "$H" 's/sizeof(m->serial_number));/sizeof(m->serial_number) - 1);/'
mt_mut MD3_no_calib_valid calib_meta_flags      "$H" '/m->calib_valid = s->calib_valid;/d'
mt_mut MD4_no_valid      calib_meta_flags       "$H" '/m->valid = s->valid;/d'
mt_mut MD5_no_calibration calib_meta_calibration "$H" '/memcpy(m->calibration, s->calibration, sizeof(m->calibration));/d'
mt_base
mt_done
exit $?
