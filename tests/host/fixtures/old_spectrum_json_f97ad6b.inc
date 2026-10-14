static bool append_fmt(char **buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        size_t avail = (*cap > *len) ? (*cap - *len) : 0;
        va_start(ap, fmt);
        int n = vsnprintf(*buf + *len, avail, fmt, ap);
        va_end(ap);
        if (n < 0) return false;
        if ((size_t)n < avail) {
            *len += (size_t)n;
            return true;
        }
        size_t need = *len + (size_t)n + 1;
        size_t ncap = *cap ? *cap * 2 : 65536;
        while (ncap < need) ncap *= 2;
        char *nb = heap_caps_realloc(*buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!nb) nb = realloc(*buf, ncap);
        if (!nb) return false;
        *buf = nb;
        *cap = ncap;
    }
}

// UI-P1 (ui-perf-2026-09-27.md P1): та же семантика роста буфера, что и
// append_fmt (доубливание при нехватке места), но ЗАПИСЬ элемента —
// json_append_uint32_csv (main/json_uint_fmt.h, host-тест
// tests/host/test_json_uint_fmt.c: побайтовое совпадение со старым
// vsnprintf-путём) вместо vsnprintf на КАЖДЫЙ из 8192 бинов.
static bool append_uint32_csv_grow(char **buf, size_t *len, size_t *cap,
                                    uint32_t v, int need_comma)
{
    for (;;) {
        size_t w = json_append_uint32_csv(*buf, *cap, *len, v, need_comma);
        if (w > 0) { *len += w; return true; }
        size_t need = *len + 12;   // max 1 запись ("," + 10 цифр)
        size_t ncap = *cap ? *cap * 2 : 65536;
        while (ncap < need) ncap *= 2;
        char *nb = heap_caps_realloc(*buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!nb) nb = realloc(*buf, ncap);
        if (!nb) return false;
        *buf = nb; *cap = ncap;
    }
}

static bool build_json_full(const spectrum_data_t *sp, char **out, size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    if (!append_fmt(&buf, &len, &cap, "{\"bins\":[")) goto fail;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
        if (!append_uint32_csv_grow(&buf, &len, &cap, sp->bins[i], i != 0)) goto fail;
    }
    uint32_t hist_ok = 0, hist_drop = 0;
    spectrum_get_hist_stats(&hist_ok, &hist_drop);
    int dead = usb_host_cdc_spectrometer_dead() ? 1 : 0;
    int as_age = spectrum_autosave_age_sec();
    int as_streak = spectrum_autosave_fail_streak();
    char jt1[16], jt2[16], jt3[16];
    spectrum_temp_json(jt1, sizeof jt1, sp->temperature[0]);
    spectrum_temp_json(jt2, sizeof jt2, sp->temperature[1]);
    spectrum_temp_json(jt3, sizeof jt3, sp->temperature[2]);
    if (!append_fmt(&buf, &len, &cap,
        "],\"total\":%" PRIu32 ",\"cpu\":%u,\"cps\":%" PRIu32 ",\"lost\":%" PRIu32
        ",\"time\":%" PRIu32 ",\"live\":%.1f,"
        "\"bridge_drop\":%" PRIu32 ",\"usb_rx_err\":%" PRIu32 ",\"rx_ring_drops\":%" PRIu32 ","
        "\"hist_ok\":%" PRIu32 ",\"hist_drop\":%" PRIu32 ","
        "\"autosave_age_sec\":%d,\"autosave_fail_streak\":%d,"
        "\"t1\":%s,\"t2\":%s,\"t3\":%s,\"serial\":\"%s\",\"dead\":%d",
        sp->total_counts, (unsigned)sp->cpu_load, sp->cps, sp->lost_impulses,
        sp->total_time_sec, compute_live_time(sp),
        tcp_bridge_dropped_bytes(), usb_host_cdc_rx_errors(), usb_host_cdc_rx_ring_drops(),
        hist_ok, hist_drop, as_age, as_streak,
        jt1, jt2, jt3,
        sp->serial_number[0] ? sp->serial_number : "", dead)) goto fail;
    if (sp->calib_valid) {
        if (!append_fmt(&buf, &len, &cap, ",\"calib\":[")) goto fail;
        for (int i = 0; i <= sp->calib_order; i++) {
            if (!append_fmt(&buf, &len, &cap, "%s%.15g", i ? "," : "", sp->calibration[i])) goto fail;
        }
        if (!append_fmt(&buf, &len, &cap, "]")) goto fail;
    }
    // #AWF-12b F2 (release-gate-1.2.28-code.md): тот же признак, что
    // usb_host_cdc.c-гейт и /api/device (web_server.c) — index.html читает
    // именно этот эндпоинт (/api/spectrum/meta.json) на каждом кадре и
    // включает кнопку идентификации нуклидов по нему (nucBtnEnable).
    if (!append_fmt(&buf, &len, &cap, ",\"calib_set\":%s",
        calib_is_missing(sp->calibration, CALIB_COEFFS, sp->calib_valid) ? "false" : "true")) goto fail;
    // У-2: главная страница пишет в журнал, когда счётчик растёт (Reset не выполнен прибором).
    if (!append_fmt(&buf, &len, &cap, ",\"reset_unconfirmed\":%" PRIu32,
        spectrum_reset_unconfirmed_count())) goto fail;
    if (!append_fmt(&buf, &len, &cap, "}")) goto fail;
    *out = buf;
    *out_len = len;
    return true;
fail:
    free(buf);
    return false;
}

static bool build_json_meta(const spectrum_data_t *sp, char **out, size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    uint32_t hist_ok = 0, hist_drop = 0;
    spectrum_get_hist_stats(&hist_ok, &hist_drop);
    int dead = usb_host_cdc_spectrometer_dead() ? 1 : 0;
    int as_age = spectrum_autosave_age_sec();
    int as_streak = spectrum_autosave_fail_streak();
    char jt1[16], jt2[16], jt3[16];
    spectrum_temp_json(jt1, sizeof jt1, sp->temperature[0]);
    spectrum_temp_json(jt2, sizeof jt2, sp->temperature[1]);
    spectrum_temp_json(jt3, sizeof jt3, sp->temperature[2]);
    if (!append_fmt(&buf, &len, &cap,
        "{\"total\":%" PRIu32 ",\"cpu\":%u,\"cps\":%" PRIu32 ",\"lost\":%" PRIu32
        ",\"time\":%" PRIu32 ",\"live\":%.1f,"
        "\"bridge_drop\":%" PRIu32 ",\"usb_rx_err\":%" PRIu32 ",\"rx_ring_drops\":%" PRIu32 ","
        "\"hist_ok\":%" PRIu32 ",\"hist_drop\":%" PRIu32 ","
        "\"autosave_age_sec\":%d,\"autosave_fail_streak\":%d,"
        "\"t1\":%s,\"t2\":%s,\"t3\":%s,\"serial\":\"%s\",\"dead\":%d,\"channels\":%d",
        sp->total_counts, (unsigned)sp->cpu_load, sp->cps, sp->lost_impulses,
        sp->total_time_sec, compute_live_time(sp),
        tcp_bridge_dropped_bytes(), usb_host_cdc_rx_errors(), usb_host_cdc_rx_ring_drops(),
        hist_ok, hist_drop, as_age, as_streak,
        jt1, jt2, jt3,
        sp->serial_number[0] ? sp->serial_number : "", dead, SPECTRUM_CHANNELS)) goto fail;
    if (sp->calib_valid) {
        if (!append_fmt(&buf, &len, &cap, ",\"calib\":[")) goto fail;
        for (int i = 0; i <= sp->calib_order; i++) {
            if (!append_fmt(&buf, &len, &cap, "%s%.15g", i ? "," : "", sp->calibration[i])) goto fail;
        }
        if (!append_fmt(&buf, &len, &cap, "]")) goto fail;
    }
    // #AWF-12b F2: тот же признак, что build_json_full выше и /api/device.
    if (!append_fmt(&buf, &len, &cap, ",\"calib_set\":%s",
        calib_is_missing(sp->calibration, CALIB_COEFFS, sp->calib_valid) ? "false" : "true")) goto fail;
    // У-2: главная страница пишет в журнал, когда счётчик растёт (Reset не выполнен прибором).
    if (!append_fmt(&buf, &len, &cap, ",\"reset_unconfirmed\":%" PRIu32,
        spectrum_reset_unconfirmed_count())) goto fail;
    if (!append_fmt(&buf, &len, &cap, "}")) goto fail;
    *out = buf;
    *out_len = len;
    return true;
fail:
    free(buf);
    return false;
}
