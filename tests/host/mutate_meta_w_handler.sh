#!/usr/bin/env bash
# release-gate 1.2.36 pass2 З-3/З-1: мутанты САМОГО обработчика meta.json?w= (main/meta_w_handler.h), исполняемого test_glue_meta_w.
# Раньше его закрепляли только дословные якоря wiring: перестановка логики при тех же строках проходила.
#   MH1: 414 при обрезанном w снят              -> обрезанный w уходит в meta           -> glue_meta_w_414
#   MH2: вернулся 400 на нецелое w (как 9138a6c) -> "1e+21" от главной - «нет данных»     -> glue_meta_w_meta
#   MH3: вернулся 414 на любой длинный запрос   -> длинный без w / с уместившимся w     -> glue_meta_w_meta glue_meta_w_sma
#   MH4: обрезанная строка не разбирается       -> обрезанный w молча в meta, длинный w без SMA -> glue_meta_w_414 glue_meta_w_sma
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/meta_w_handler.h
mt_init "$H"
mt_mut MH1_cut_414_lost  glue_meta_w_414  "$H" 's/if (mw == META_W_CUT) {/if (0) {/'
mt_mut MH2_bad_400_back  glue_meta_w_meta "$H" '/if (mw == META_W_CUT) {/i\    if (mw == META_W_BAD) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "w"); return ESP_FAIL; }'
mt_mut MH3_len_414_back  "glue_meta_w_meta glue_meta_w_sma" "$H" '/^    uint32_t w = 0;/i\    if (ql >= sizeof(q)) { httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "q"); return ESP_FAIL; }'
mt_mut MH4_trunc_ignored "glue_meta_w_414 glue_meta_w_sma" "$H" 's/if (qe == ESP_OK || qe == ESP_ERR_HTTPD_RESULT_TRUNC) mw/if (qe == ESP_OK) mw/'
mt_base
mt_done
exit $?
