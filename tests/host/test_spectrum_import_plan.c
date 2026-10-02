// 1.2.31: импорт фонового спектра — разбор тела ASI1 (main/spectrum_import_plan.h) и разбор пути удаления.
// Образец bins — боевой (fixtures/imp_real_bins.h, строки водопада платы), не синтетика (#SA-3a).
#include "spectrum_import_plan.h"
#include "test_util.h"
#include "fixtures/imp_real_bins.h"

static uint8_t g_body[SPEC_IMPORT_SIZE];
static spectrum_data_t g_sp;

static void put32(uint8_t *b, size_t off, uint32_t v) { for (int i = 0; i < 4; i++) b[off + i] = (uint8_t)(v >> (8 * i)); }
static void put_crc(uint8_t *b)
{
    uint32_t c = imp_crc32(b, 124, 0);
    put32(b, 124, imp_crc32(b + 128, SPEC_IMPORT_SIZE - 128, c));
}
static void mk_body(uint8_t *b)
{
    memset(b, 0, SPEC_IMPORT_SIZE);
    put32(b, 0, SPEC_IMPORT_MAGIC); b[4] = 1; b[6] = 128; put32(b, 8, 8192);
    put32(b, 12, IMP_REAL_TIME); put32(b, 16, 5); put32(b, 20, 4);
    memcpy(b + 24, IMP_REAL_CALIB, sizeof(IMP_REAL_CALIB));
    put32(b, 64, 1790000000u); memcpy(b + 72, "SN-123", 7); put32(b, 120, 1);
    for (int i = 0; i < 8192; i++) put32(b, 128 + 4 * (size_t)i, IMP_REAL_BINS[i]);
    put_crc(b);
}
static imp_err_t dec(size_t len) { memset(&g_sp, 0, sizeof g_sp); return spectrum_import_decode(g_body, len, &g_sp); }
static void test_imp_ok(void)
{
    mk_body(g_body);
    CHECK(imp_rd32(g_body + 124) == 0x85EA3F0Du);   // общий вектор с браузером: test/web/import_parse_test.mjs (node:zlib.crc32)
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK);
    CHECK(g_sp.total_counts == IMP_REAL_SUM && g_sp.total_time_sec == IMP_REAL_TIME && g_sp.lost_impulses == 5);
    CHECK(g_sp.cps == IMP_REAL_SUM / IMP_REAL_TIME);
    CHECK(strcmp(g_sp.serial_number, "IMP:SN-123") == 0);
    CHECK(g_sp.calib_valid && g_sp.calib_order == 4 && g_sp.calibration[1] == IMP_REAL_CALIB[1]);
    CHECK(g_sp.valid && g_sp.saved_at == 1790000000);
    CHECK(g_sp.bins[0] == IMP_REAL_BINS[0] && g_sp.bins[8191] == IMP_REAL_BINS[8191] && g_sp.bins[1461] == IMP_REAL_BINS[1461]);
}
static void test_imp_size(void)
{
    mk_body(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE - 1) == IMP_BAD_SIZE);
    CHECK(dec(SPEC_IMPORT_SIZE + 1) == IMP_BAD_SIZE);
    CHECK(dec(0) == IMP_BAD_SIZE);
}
static void test_imp_magic(void)
{
    mk_body(g_body); g_body[0] ^= 1; put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_MAGIC);
}
static void test_imp_version(void)
{
    mk_body(g_body); g_body[4] = 2; put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_VERSION);
    mk_body(g_body); g_body[6] = 64; put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_VERSION);
}
static void test_imp_channels(void)
{
    mk_body(g_body); put32(g_body, 8, 4096); put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_CHANNELS);
}
static void test_imp_crc(void)
{
    mk_body(g_body); g_body[SPEC_IMPORT_SIZE - 1] ^= 1;      // бит в bins[8191], crc не пересчитан
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_CRC);
    mk_body(g_body); g_body[16] ^= 1;                        // бит в заголовке (lost), crc не пересчитан
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_CRC);
}
static void set_time(uint32_t t) { mk_body(g_body); put32(g_body, 12, t); put_crc(g_body); }
static void test_imp_time(void)
{
    set_time(0);         CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_TIME);
    set_time(315360001); CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_TIME);
    set_time(315360000); CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK);
    set_time(1);         CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK);
}
static void flat(uint32_t b0, uint32_t b1, uint32_t lost)    // все каналы 0, кроме двух первых
{
    mk_body(g_body); memset(g_body + 128, 0, 4 * 8192);
    put32(g_body, 128, b0); put32(g_body, 132, b1); put32(g_body, 16, lost); put_crc(g_body);
}
static void test_imp_counts(void)
{
    flat(0, 0, 0);              CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_COUNTS);     // сумма 0
    flat(0xFFFFFFFFu, 1, 0);    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_COUNTS);     // сумма = 2^32 (u32 бы обернулась)
    flat(0xFFFFFFFFu, 0xFFFFFFFFu, 0); CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_COUNTS);   // 2^33-2: u32 обернулась бы в годное
    flat(0xFFFFFFFFu, 0, 1);    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_COUNTS);     // sum + lost = 2^32
    flat(0xFFFFFFFEu, 1, 1);    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_COUNTS);     // sum + lost = 2^32 при sum < 2^32
    flat(0xFFFFFFFFu, 0, 0);    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK);             // sum = 2^32-1 допустима
    CHECK(g_sp.total_counts == 0xFFFFFFFFu);
}
static void set_calib(int32_t order, int zero, int nan_c1)
{
    mk_body(g_body); put32(g_body, 20, (uint32_t)order);
    if (zero) memset(g_body + 24, 0, 40);
    if (nan_c1) { double d = NAN; memcpy(g_body + 32, &d, 8); }
    put_crc(g_body);
}
static void test_imp_calib(void)
{
    set_calib(5, 0, 0);  CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_CALIB);
    set_calib(-2, 0, 0); CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_CALIB);
    set_calib(4, 0, 1);  CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_CALIB);
    set_calib(2, 1, 0);  CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_CALIB);
    set_calib(-1, 0, 0); CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK && !g_sp.calib_valid && g_sp.calib_order == -1);
    set_calib(2, 0, 0);  CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK && g_sp.calib_valid && g_sp.calib_order == 2);
    CHECK(g_sp.calibration[3] == 0.0 && g_sp.calibration[4] == 0.0);                 // хвост за order обнулён
    set_calib(1, 0, 0);  { double d = NAN; memcpy(g_body + 40, &d, 8); put_crc(g_body); }
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK && g_sp.calibration[2] == 0.0);            // NaN в хвосте не мешает
}
static void test_imp_serial(void)
{
    mk_body(g_body); g_body[72] = 0x07; put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_SERIAL);
    mk_body(g_body); memset(g_body + 72, 'A', 48); put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_BAD_SERIAL);
    mk_body(g_body); memset(g_body + 72, 0, 48); put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK);                  // пустой серийник допустим
}
static void test_imp_30days(void)
{
    set_time(2592000);                                        // 30 суток, бины боевого образца
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK && g_sp.total_time_sec == 2592000 && g_sp.cps == 0);
    flat(0xFFFFFFFFu, 0, 0); put32(g_body, 12, 2592000); put_crc(g_body);
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK && g_sp.cps == 1657);   // 4294967295 / 2592000 = 1657,0...
    mk_body(g_body); put32(g_body, 64, 0xFFFFFFFFu); put32(g_body, 68, 0xFFFFFFFFu); put_crc(g_body);   // saved_at = -1
    CHECK(dec(SPEC_IMPORT_SIZE) == IMP_OK && g_sp.saved_at >= 1700000000);                        // подставлено время платы
}
// Удаление записи: только POST /api/saved/<i>/delete. Раньше любой POST /api/saved/* (в т.ч. .../import, .../abc) удалял spec_0000.
static void test_saved_delete_uri(void)
{
    CHECK(saved_delete_index("/api/saved/5/delete") == 5);
    CHECK(saved_delete_index("/api/saved/0005/delete") == 5);
    CHECK(saved_delete_index("/api/saved/9998/delete") == 9998);
    CHECK(saved_delete_index("/api/saved/7/delete?x=1") == 7);
    CHECK(saved_delete_index("/api/saved/5") == -1);
    CHECK(saved_delete_index("/api/saved/abc") == -1);
    CHECK(saved_delete_index("/api/saved/import") == -1);
    CHECK(saved_delete_index("/api/saved/import/delete") == -1);
    CHECK(saved_delete_index("/api/saved/5/deleteX") == -1);
    CHECK(saved_delete_index("/api/saved/5/delete/") == -1);
    CHECK(saved_delete_index("/api/saved//delete") == -1);
    CHECK(saved_delete_index("/api/saved/-3/delete") == -1);
    CHECK(saved_delete_index("/api/saved/5/spectrum.json") == -1);
    CHECK(saved_delete_index("/api/saved/9999/delete") == -1);
    CHECK(saved_delete_index("/api/saved/10000/delete") == -1);
    CHECK(saved_delete_index("/api/other/5/delete") == -1);
    CHECK(saved_delete_index("") == -1);
}
// IMPFAIL <имя> <число> — для mutate_spectrum_import.sh: какой именно тест покраснел (печать только при провале).
#define RUN(t) do { int b_ = g_failures; t(); if (g_failures != b_) printf("IMPFAIL %s %d\n", #t, g_failures - b_); } while (0)
void spectrum_import_plan_suite(void)
{
    RUN(test_imp_ok); RUN(test_imp_size); RUN(test_imp_magic); RUN(test_imp_version); RUN(test_imp_channels);
    RUN(test_imp_crc); RUN(test_imp_time); RUN(test_imp_counts); RUN(test_imp_calib); RUN(test_imp_serial);
    RUN(test_imp_30days); RUN(test_saved_delete_uri);
}
