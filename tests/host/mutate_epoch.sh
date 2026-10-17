#!/usr/bin/env bash
# D2 (К2 + P2/P3): мутанты эпохи, calib_channels, свёртки (ref_fold), разбора query, sweep_missed. #SA-3: каждый мутант обязан покраснить РОВНО названный тест (имя в строке FAIL),
# baseline - 0 FAIL. Прогон идёт целью test-keepgoing (все бинари, без остановки на первом упавшем), строки "INFO k2: ..." показывают K = число образцов,
# на которых мутант и исправный код дают РАЗНЫЙ вывод (#SA-3a); в baseline K=0.
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
export MT_TARGET=test-keepgoing
H=../../main/spectrum_fold.h
E=../../main/spectrum_epoch.h
C=../../main/spectrum_http_cache.c
S=../../main/spectrum.c
M=../../main/main.c
W=../../main/sweep_missed.h
mt_init "$H" "$E" "$C" "$S" "$M" "$W"
mt_mut KE1_fmt_d          epoch_fields    "$H" '/boot_id/s/%" PRIu32/%d"/g'
mt_mut KE2_no_spec_resets epoch_fields    "$H" 's|",\\"spec_resets\\":%" PRIu32 ||;s|ep->dev_resets, ep->spec_resets,|ep->dev_resets,|'
mt_mut KE3_epoch_in_v1    v1_bytes        "$C" 's|size_t n = spec_json_full_fmt(buf, cap, bins, meta, mlen, a_end, b_start);|size_t n = spec_json_full_fmt(buf, cap, bins, meta, mlen, a_end, b_start); if (s_ep.spec_resets != 0 \&\& n >= 1 \&\& n + 40 < cap) { n--; n += (size_t)snprintf(buf + n, cap - n, ",\\"spec_resets\\":%u}", (unsigned)s_ep.spec_resets); }|'
mt_mut KE4_read_after     epoch_atomic    "$E" 's|c->read_epoch(c->ep_dst);   /\* EPOCH_IN_WINDOW \*/||;s|return ok;   /\* EPOCH_RET \*/|if (ok) { read_epoch(ep_dst); } return ok;|'
mt_mut KE5_read_before    epoch_atomic    "$E" 's|c->read_epoch(c->ep_dst);   /\* EPOCH_IN_WINDOW \*/||;s|bool ok = seq_copy(|read_epoch(ep_dst); bool ok = seq_copy(|'
mt_mut KE6_live_epoch     epoch_cache_gen "$C" 's|spec_epoch_t ep = s_ep;.*|spec_epoch_t ep; spectrum_get_base_info(NULL, NULL, \&ep.dev_resets); ep.spec_resets = spectrum_reset_gen();|'
mt_mut KE7_fold_spec      epoch_events    "$E" 's|(void)spec_resets;|(*spec_resets)++;|'
mt_mut KE8_reset_dev      epoch_events    "$E" 's|(void)dev_resets;|(*dev_resets)++;|'
mt_mut KE9_no_rnd         boot_id_mix     "$E" 's|spec_fmix32(rnd ^ mac_lo32 ^ timer_lo32)|spec_fmix32((rnd \& 0u) ^ mac_lo32 ^ timer_lo32)|'
mt_mut KE10_no_mac        boot_id_mix     "$E" 's|spec_fmix32(rnd ^ mac_lo32 ^ timer_lo32)|spec_fmix32(rnd ^ (mac_lo32 \& 0u) ^ timer_lo32)|'
mt_mut KE11_zero_kept     boot_id_mix     "$E" 's|return id == 0u ? 1u : id;|return id;|'
mt_mut KE12_nvs           epoch_no_nvs    "$S" 's|s_boot_id = spec_boot_id_mix(|(void)nvs_get_u32(0, "k", 0); s_boot_id = spec_boot_id_mix(|'
mt_mut KE13_init_before   epoch_no_nvs    "$M" 's|    spectrum_boot_id_init();.*||;s|    wifi_manager_init();|    spectrum_boot_id_init();\n    wifi_manager_init();|'
mt_mut KE14_raw_incr      epoch_no_nvs    "$S" 's|spec_epoch_on_user_reset(&s_dev_resets, &s_reset_gen);|s_reset_gen++;|'
mt_mut CC1_no_calib_ch    calib_channels  "$H" 's|,\\"calib_channels\\":%d}|}|;s|ep->spec_resets, SPECTRUM_CHANNELS);|ep->spec_resets);|'
mt_mut FR1_stride         fold_ref        "$H" 's|sum += bins\[k \* m + j\];|sum += bins[k + j * (SPECTRUM_CHANNELS / m)];|'
mt_mut P3A_trunc_parsed   glue_query_trunc "$C" 's|if (qe == ESP_ERR_HTTPD_RESULT_TRUNC) return -1;.*|if (qe == ESP_ERR_HTTPD_RESULT_TRUNC) qe = ESP_OK;|'
mt_mut P3B_dup_first      channels_400    "$H" 's|if (found) return -1;|if (found) found = 1;|'
mt_mut SD1_pending_off    sweep_no_double_count "$W" 's|return s->done + sweep_gap_intervals(now_ms - s->last_ms);|return s->done + 0u * sweep_gap_intervals(now_ms - s->last_ms);|'
mt_mut SD2_pending_x2     sweep_no_double_count "$W" 's|return s->done + sweep_gap_intervals(now_ms - s->last_ms);|return s->done + 2u * sweep_gap_intervals(now_ms - s->last_ms);|'
mt_base
mt_done
exit $?
