#!/usr/bin/env bash
# release-gate 1.2.36 pass2 З-3: мутанты САМОГО обработчика meta.json?w= (main/meta_w_handler.h), исполняемого test_glue_meta_w.
# Раньше его закрепляли только дословные якоря wiring: перестановка логики при тех же строках проходила. Точечный мутант краснит РОВНО названный тест.
#   MH1: off-by-one длины (ql > sizeof(q))   -> запрос ровно 128 символов уходит без 414 -> glue_meta_w_414
#   MH2: ветка 400 снята                       -> мусор в w уходит в meta                -> glue_meta_w_400
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/meta_w_handler.h
mt_init "$H"
mt_mut MH1_len_off_by_one glue_meta_w_414 "$H" 's/if (ql >= sizeof(q)) {/if (ql > sizeof(q)) {/'
mt_mut MH2_no_400         glue_meta_w_400 "$H" 's/if (mw == META_W_BAD) {/if (0) {/'
mt_base
mt_done
exit $?
