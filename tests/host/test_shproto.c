// Host-тесты конечного автомата shproto (components/shproto/shproto.c).
// Компилируется обычным gcc/clang — компонент зависит только от stdint/stddef/
// stdbool/string, ESP-IDF не требуется. Здесь живёт main() и тесты декодера
// shproto_byte_received; round-trip-тесты — в test_roundtrip.c.
#include "shproto.h"
#include "test_util.h"
#include <string.h>
#include <stdio.h>

int g_failures = 0;

// Round-trip-набор из test_roundtrip.c (одна общая сборка → один main).
void roundtrip_suite(void);
// #FIELD-6: guard-логика источника времени из test_net_time.c (main/net_time.c).
void nettime_suite(void);
// #FW-50: фильтр уровня для UART из test_debug_log_filter.c
// (main/debug_log_level_filter.h).
void dbglog_filter_suite(void);
// #FW-8 residual: staging gap continuity from test_hist_stage.c
void hist_stage_suite(void);
// #FW-8 residual: quiet-budget math from test_flash_quiet.c
void flash_quiet_suite(void);
// A1-P0: разбор тела POST /api/settings/restore — запись за границу приёмного массива
void run_kv_array_tests(void);
void t1_settle_suite(void);
void wf_seg_clear_suite(void);
// issue #52: разбор имени снимка и план ротации (main/backup_plan.c).
void test_backup_plan(void);
void fuzz_backup_name(void);
// Сторож набора после перезагрузки прибора (main/acq_watch.h).
void acq_watch_suite(void);
void acq_intent_suite(void);
void cmd_is_device_reset_suite(void);
// AWF-1/2a: восстановление спектра и реконнект/возврат Field AP.
void test_restore_plan(void);
void test_wifi_reconnect_plan(void);
void test_wifi_return_plan(void);
void test_wifi_return_backoff(void);
// AWF-3: сброс прибора и слияние база+прибор (main/spectrum_base_plan.h).
void spectrum_base_plan_suite(void);
// AWF-2a captive: классификация URI активность/проба (main/http_activity_plan.h).
void test_http_activity_plan(void);
void test_http_404_activity(void);
void test_http_activity_quiet_model(void);
// AWF-2a финал: семантика missing-key boot-флага (main/boot_flag_plan.h).
void test_boot_flag_plan(void);
void test_wf_ref_plan(void);   // #AUD-DUP1
void test_session_plan(void);  // 1.2.31
void test_wf_rst_keep(void);   // #AUD-RST
void test_wf_tail_plan(void);  // #RST-TAIL
void test_ota_gh_check_state(void);  // LK-07
void test_debug_log_chunk(void);  // WP5
// AWF-4: проверка заголовка OTA-образа (main/ota_image_check.h).
void ota_image_check_suite(void);
void ota_mark_valid_plan_suite(void);
void json_uint_fmt_suite(void);
void version_suite(void);
void sha256sums_suite(void);
void gh_parse_suite(void);
void ota_busy_suite(void);
void ota_gh_decision_suite(void);
void ota_gh_redirect_suite(void);
void ota_timeout_budget_suite(void);
void spectrum_import_plan_suite(void);
void ota_gh_dl_retry_suite(void);
// #AWF-12: предикаты авто-считывания калибровки (main/calib_autoread.h).
void calib_autoread_suite(void);
void text_accum_predicates_suite(void);
void text_accum_triggers_suite(void);
void text_accum_f1_regression_suite(void);
void text_accum_split_dump_suite(void);
void text_accum_ok_then_inf_suite(void);
void text_accum_generic_garbage_before_dump_suite(void);
void text_accum_overflow_suite(void);
void text_accum_overflow_boundary_suite(void);  // RT2
// sweep-A 1.2.28: P-009 бюджет гейта, #REC-12 пин чтения, P-042 seg_seq,
// #FW-19 план экспорта n42, R7 признак калибровки в файловых выводах.
void http_gate_budget_suite(void);
void wf_seg_pin_suite(void);
void wf_seg_seq_suite(void);
void wf_export_plan_suite(void);
void calib_export_suite(void);
void calib_export_sites_suite(void);
void seg_pin_sites_suite(void);
// R2/R3 (release-gate-1.2.28-code-rc2.md §2.1, sweep-C группа D): S10/S07/S22
// (дамп теряется/сдвигается), S17-класс (\0 внутри пакета маскирует -inf).
void text_accum_r2_offset_dump_suite(void);
void text_accum_r2_fragment_before_dump_suite(void);
void text_accum_r2_ok_plus_dump_one_frame_suite(void);
void text_accum_r3_null_byte_suite(void);
void text_accum_m1_split_real_dump_suite(void);
void wf_seg_rebuild_range_suite(void);
void spec_cache_gen_suite(void);
void text_accum_round2_suite(void);       // Н1/Н2 раунда 2
void round2_wiring_sites_suite(void);     // Н12 раунда 2: проводка в прошивочных .c
void text_accum_r3_old_api_suite(void);  // У1/У2 раунда 3: прежний API
void text_accum_r3_u1_suite(void);       // У1 раунда 3: дробление, потеря кадра
void text_accum_r3_u2_suite(void);       // У2 раунда 3: мусор + дробление
void text_accum_r3_dispatch_suite(void); // У6 раунда 3: проводка разбора
void round3_flash_lock_suite(void);      // У3 раунда 3
void round3_snapshot_file_suite(void);   // У4 раунда 3
void round3_ota_reopen_suite(void);      // У5 раунда 3
void round3_seg_rebuild_entry_suite(void); // У6/О5 раунда 3

// Тестовая команда. CMD_HISTOGRAM (0x01) объявлена в main/atomspectra.h, но она
// вне include-path host-сборки; shproto трактует cmd как обычный uint8_t.
#define T_CMD 0x42

// Кодирует кадр (cmd + payload) в проволочный буфер через encoder shproto.
// Возвращает длину кадра на проводе (ведущий 0xFF + START + тело + FINISH).
static size_t encode_frame(uint8_t cmd, const uint8_t *payload, size_t n,
                           uint8_t *wire, size_t wire_cap)
{
    shproto_struct pe;
    shproto_init(&pe, wire, wire_cap);
    shproto_packet_start(&pe, cmd);
    for (size_t i = 0; i < n; i++) shproto_packet_add_data(&pe, payload[i]);
    shproto_packet_complete(&pe);
    return pe.len;
}

// Скармливает поток байт в декодер побайтно.
static void feed(shproto_struct *pd, const uint8_t *wire, size_t n)
{
    for (size_t i = 0; i < n; i++) shproto_byte_received(pd, wire[i]);
}

// 1. Happy path: валидный кадр декодируется, cmd и данные совпадают с исходными.
static void test_happy_path(void)
{
    uint8_t wire[64], dec[64];
    const uint8_t payload[] = {0x10, 0x11, 0x22, 0x33};
    size_t wlen = encode_frame(T_CMD, payload, sizeof(payload), wire, sizeof(wire));

    shproto_struct pd;
    shproto_init(&pd, dec, sizeof(dec));
    feed(&pd, wire, wlen);

    CHECK(pd.ready == true);
    CHECK(pd.dropped == false);
    CHECK(pd.cmd == T_CMD);
    CHECK(pd.len == sizeof(payload));
    CHECK(memcmp(pd.data, payload, sizeof(payload)) == 0);
}

// 2. CRC corruption: инверсия одного байта payload → dropped, не ready.
static void test_crc_corruption(void)
{
    uint8_t wire[64], dec[64];
    const uint8_t payload[] = {0x11, 0x22, 0x33};  // без маркеров → без escape
    size_t wlen = encode_frame(T_CMD, payload, sizeof(payload), wire, sizeof(wire));

    // Контроль: неиспорченный кадр декодируется чисто.
    shproto_struct ctrl;
    shproto_init(&ctrl, dec, sizeof(dec));
    feed(&ctrl, wire, wlen);
    CHECK(ctrl.ready == true);

    // Проволочный кадр: [0]=0xFF [1]=0xFE(START) [2]=cmd [3]=0x11 [4]=0x22 ...
    // Портим payload-байт по индексу 4. XOR 0x01 → 0x23: не маркер, поэтому
    // фрейминг цел, но CRC ломается.
    uint8_t bad[64];
    memcpy(bad, wire, wlen);
    bad[4] ^= 0x01;
    CHECK(bad[4] != SHPROTO_START && bad[4] != SHPROTO_ESC && bad[4] != SHPROTO_FINISH);

    shproto_struct pd;
    shproto_init(&pd, dec, sizeof(dec));
    feed(&pd, bad, wlen);
    CHECK(pd.ready == false);
    CHECK(pd.dropped == true);
}

// 4. Потеря START: байты до первого 0xFE игнорируются (started остаётся false),
//    а следующий валидный кадр после мусора всё равно декодируется.
static void test_lost_start(void)
{
    uint8_t dec[64];
    shproto_struct pd;
    shproto_init(&pd, dec, sizeof(dec));

    const uint8_t garbage[] = {0x01, 0x02, 0x03, 0xAA, 0xBB};  // нет 0xFE
    feed(&pd, garbage, sizeof(garbage));
    CHECK(pd.started == false);
    CHECK(pd.ready == false);
    CHECK(pd.len == 0);

    uint8_t wire[64];
    const uint8_t payload[] = {0x55, 0x66};
    size_t wlen = encode_frame(T_CMD, payload, sizeof(payload), wire, sizeof(wire));
    feed(&pd, wire, wlen);
    CHECK(pd.ready == true);
    CHECK(pd.cmd == T_CMD);
    CHECK(pd.len == sizeof(payload));
    CHECK(memcmp(pd.data, payload, sizeof(payload)) == 0);
}

// 5. Два пакета подряд в одном потоке байт — оба декодируются.
static void test_two_packets(void)
{
    uint8_t w1[64], w2[64], dec[64];
    const uint8_t p1[] = {0x01, 0x02, 0x03};
    const uint8_t p2[] = {0xA0, 0xB0};
    size_t l1 = encode_frame(0x11, p1, sizeof(p1), w1, sizeof(w1));
    size_t l2 = encode_frame(0x22, p2, sizeof(p2), w2, sizeof(w2));

    shproto_struct pd;
    shproto_init(&pd, dec, sizeof(dec));

    feed(&pd, w1, l1);
    CHECK(pd.ready == true);
    CHECK(pd.cmd == 0x11);
    CHECK(pd.len == sizeof(p1));
    CHECK(memcmp(pd.data, p1, sizeof(p1)) == 0);

    feed(&pd, w2, l2);
    CHECK(pd.ready == true);
    CHECK(pd.cmd == 0x22);
    CHECK(pd.len == sizeof(p2));
    CHECK(memcmp(pd.data, p2, sizeof(p2)) == 0);
}

// 6. Переполнение буфера: payload длиннее buf_size → декодер обрезает по границе,
//    не пишет за пределы (canary после буфера цела), не падает; усечённый буфер →
//    CRC не сходится → dropped, не ready.
static void test_buffer_overflow(void)
{
    struct { uint8_t buf[8]; uint8_t canary[8]; } g;
    memset(g.canary, 0xAA, sizeof(g.canary));

    uint8_t wire[128];
    uint8_t payload[40];
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(i + 1);
    size_t wlen = encode_frame(T_CMD, payload, sizeof(payload), wire, sizeof(wire));

    shproto_struct pd;
    shproto_init(&pd, g.buf, sizeof(g.buf));  // buf_size = 8 << payload
    feed(&pd, wire, wlen);

    for (size_t i = 0; i < sizeof(g.canary); i++)
        CHECK(g.canary[i] == 0xAA);
    CHECK(pd.len <= sizeof(g.buf));
    CHECK(pd.ready == false);
}

int main(void)
{
    test_happy_path();
    test_crc_corruption();
    test_lost_start();
    test_two_packets();
    test_buffer_overflow();
    roundtrip_suite();
    nettime_suite();
    dbglog_filter_suite();
    hist_stage_suite();
    flash_quiet_suite();
    run_kv_array_tests();
    t1_settle_suite();
    wf_seg_clear_suite();
    test_backup_plan();
    fuzz_backup_name();
    acq_watch_suite();
    acq_intent_suite();
    cmd_is_device_reset_suite();
    test_restore_plan();
    test_wifi_reconnect_plan();
    test_wifi_return_plan();
    test_wifi_return_backoff();
    spectrum_base_plan_suite();
    test_http_activity_plan();
    test_http_404_activity();
    test_http_activity_quiet_model();
    test_boot_flag_plan();
    test_wf_ref_plan();
    test_session_plan();
    test_wf_rst_keep();
    test_wf_tail_plan();
    test_ota_gh_check_state();
    test_debug_log_chunk();
    ota_image_check_suite();
    ota_mark_valid_plan_suite();
    json_uint_fmt_suite();
    version_suite();
    sha256sums_suite();
    gh_parse_suite();
    ota_busy_suite();
    ota_gh_decision_suite();
    ota_gh_redirect_suite();
    ota_timeout_budget_suite();
    spectrum_import_plan_suite();
    ota_gh_dl_retry_suite();
    calib_autoread_suite();
    text_accum_predicates_suite();
    text_accum_triggers_suite();
    text_accum_f1_regression_suite();
    text_accum_split_dump_suite();
    text_accum_ok_then_inf_suite();
    text_accum_generic_garbage_before_dump_suite();
    text_accum_overflow_suite();
    text_accum_overflow_boundary_suite();  // RT2
    http_gate_budget_suite();      // sweep-A
    wf_seg_pin_suite();
    wf_seg_seq_suite();
    wf_export_plan_suite();
    calib_export_suite();
    calib_export_sites_suite();
    seg_pin_sites_suite();
    text_accum_r2_offset_dump_suite();
    text_accum_r2_fragment_before_dump_suite();
    text_accum_r2_ok_plus_dump_one_frame_suite();
    text_accum_r3_null_byte_suite();
    text_accum_m1_split_real_dump_suite();
    wf_seg_rebuild_range_suite();
    spec_cache_gen_suite();
    text_accum_round2_suite();
    round2_wiring_sites_suite();
    text_accum_r3_old_api_suite();
    text_accum_r3_u1_suite();
    text_accum_r3_u2_suite();
    text_accum_r3_dispatch_suite();
    round3_flash_lock_suite();
    round3_snapshot_file_suite();
    round3_ota_reopen_suite();
    round3_seg_rebuild_entry_suite();

    if (g_failures) {
        printf("\n%d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
    printf("\nALL PASSED\n");
    return 0;
}
