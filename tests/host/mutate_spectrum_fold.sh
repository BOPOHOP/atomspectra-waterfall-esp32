#!/usr/bin/env bash
# D1 К1 (спека D1 п.2, план 13-plan.md 1.4): мутанты свёртки bins для GET /api/spectrum.json?channels=N (main/spectrum_fold.h)
# и обвязки (main/spectrum_http_cache.c). Каждый точечный мутант обязан покраснить РОВНО названный тест (имя - в строке FAIL), baseline - 0 FAIL.
#   MF1: подмена m (ширина группы вдвое больше)              -> только fold_width   (сумма цела, число каналов N/2, сдвиг группы 0 цел)
#   MF2: сдвиг свёртки на 1 канал (кольцом)                  -> только fold_origin  (сумма цела, равномерный вход не различает)
#   MF3: потеря последней группы (значение обнулено)         -> только fold_sum     (число каналов, ширина и канал 0 целы)
# Доп. (не из задания; make test останавливается на первом упавшем бинаре, поэтому красные тесты следующих бинарей не видны):
#   MF4: проверка допустимого N снята -> channels_400 (test_runner; glue_400 не достигается)
#   MF5: запрос без параметра идёт по пути v2 -> glue_json_body glue_hist_age glue_nodelay (СУЩЕСТВУЮЩИЙ test_glue_cache: ответ без параметра байтово прежний)
# D2 (P2): добавлен NCHECK fold_ref (сравнение с независимым ref_fold на 20 образцах) - MF1..MF3 теперь красят свой тест И fold_ref (так и должно быть: значения свёртки неверны).
# Строка "INFO spectrum_fold: samples=N ... differ_from_ref=K": N - образцов (5 снимков .146 x 4 значения N), K - на скольких образцах дефектная и
# исправная версии дают РАЗНЫЙ вывод (#SA-3a); в baseline K=0.
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/spectrum_fold.h
C=../../main/spectrum_http_cache.c
mt_init "$H" "$C"
mt_mut MF1_m_doubled   "fold_width fold_ref"  "$H" 's|return SPECTRUM_CHANNELS / n;|return SPECTRUM_CHANNELS / n * 2;|'
mt_mut MF2_shift1      "fold_origin fold_ref" "$H" 's|sum += bins\[k \* m + j\];|sum += bins[(k * m + j + 1) \& (SPECTRUM_CHANNELS - 1)];|'
mt_mut MF3_last_lost   "fold_sum fold_ref"    "$H" 's|uint32_t v = spec_fold_group(bins, m, k);|uint32_t v = (k == ng - 1) ? 0u : spec_fold_group(bins, m, k);|'
mt_mut MF4_any_n       channels_400 "$H" 's|if (!spec_channels_valid(val)) return -1;|if (val <= 0) return -1;|'
mt_mut MF5_v2_always   "glue_json_body glue_hist_age glue_nodelay" "$C" 's|if (qk > 0) return spectrum_http_send_json_v2|if (qk >= 0) return spectrum_http_send_json_v2|'
# D2 К0: query длиннее буфера (95) без channels - ответ версии 1; 400 только при channels в q[] или на границе.
#   QT1: любой усечённый query -> 400 (поведение до правки) -> только glue_query_trunc_v1 (test_glue_epoch)
#   QT2: проверка границы (хвост мог начаться с "channels") снята -> только glue_query_trunc
mt_mut QT1_trunc_always_400 glue_query_trunc_v1 "$C" 's|if (spec_channels_query_parse(q, n) != 0) return -1;|return -1;|'
mt_mut QT2_no_boundary      glue_query_trunc    "$C" 's|if (strncmp(seg, "channels", strlen(seg)) == 0) return -1;||'
mt_base
mt_done
exit $?
