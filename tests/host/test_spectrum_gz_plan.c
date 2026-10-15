// #GZ-T (1.2.37-rc90): host-тест main/spectrum_gz_plan.h — умолчание уровня 0, выбор кодировки, рамка gzip (свой разбор:
// заголовок 10 Б, CRC32, ISIZE), выбор «готово / промах / как раньше» по поколению, запас PSRAM. Свой main (make test).
#include "../../main/spectrum_gz_plan.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_fail++; } } while (0)

// Эталонный CRC-32 (IEEE, отражённый, полином 0xEDB88320), побитно — не зависит от ROM mz_crc32.
static uint32_t ref_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Проверочный разбор рамки gzip с одним stored-блоком deflate (BFINAL=1, BTYPE=00): заголовок, LEN/NLEN, данные, CRC32,
// ISIZE. 1 — рамка верна и данные равны want.
static int check_gz_stored(const uint8_t *g, size_t n, const uint8_t *want, size_t wn)
{
    if (n < 10 + 5 + 8) return 0;
    if (g[0] != 0x1f || g[1] != 0x8b || g[2] != 0x08 || g[3] != 0x00) return 0;   // ID1 ID2 CM=deflate FLG=0
    if (g[9] != 0xff) return 0;                                                    // OS = unknown
    const uint8_t *d = g + 10;
    if (d[0] != 0x01) return 0;
    size_t len = (size_t)d[1] | ((size_t)d[2] << 8), nlen = (size_t)d[3] | ((size_t)d[4] << 8);
    if ((len ^ 0xFFFFu) != nlen || len != wn || 10 + 5 + len + 8 != n) return 0;
    if (memcmp(d + 5, want, wn) != 0) return 0;
    const uint8_t *t = g + n - 8;
    return rd_le32(t) == ref_crc32(want, wn) && rd_le32(t + 4) == (uint32_t)wn;
}

static void t_level(void)
{
    // Нет ключа (чистая NVS, апгрейд) — 0 при любом «сыром» значении; ключ есть — только 1 и 6, иное — 0.
    NCHECK("gz_level", spec_gz_level_from_nvs(false, 0) == 0);
    NCHECK("gz_level", spec_gz_level_from_nvs(false, 1) == 0);
    NCHECK("gz_level", spec_gz_level_from_nvs(false, 6) == 0);
    NCHECK("gz_level", spec_gz_level_from_nvs(true, 0) == 0);
    NCHECK("gz_level", spec_gz_level_from_nvs(true, 1) == 1);
    NCHECK("gz_level", spec_gz_level_from_nvs(true, 6) == 6);
    NCHECK("gz_level", spec_gz_level_from_nvs(true, 5) == 0);
    NCHECK("gz_level", spec_gz_level_from_nvs(true, 255) == 0);
    NCHECK("gz_level", spec_gz_level_valid(0) && spec_gz_level_valid(1) && spec_gz_level_valid(6));
    NCHECK("gz_level", !spec_gz_level_valid(2) && !spec_gz_level_valid(9) && !spec_gz_level_valid(-1));
    NCHECK("gz_level", spec_gz_tdefl_flags(1) == (1 | SPEC_GZ_TDEFL_GREEDY));
    NCHECK("gz_level", spec_gz_tdefl_flags(6) == 128);
    NCHECK("gz_level", spec_gz_tdefl_flags(0) == 0);
    char tail[32];
    snprintf(tail, sizeof tail, SPEC_GZ_BOOTCFG_TAIL, 6u);
    NCHECK("gz_level", strcmp(tail, ",\"gzip_level\":6}") == 0);   // хвост JSON GET /api/boot-config
}

static void t_accept(void)
{
    NCHECK("gz_accept", spec_gz_accepts("gzip"));
    NCHECK("gz_accept", spec_gz_accepts("gzip, deflate, br, zstd"));   // браузер
    NCHECK("gz_accept", spec_gz_accepts("deflate, GZip;q=0.5"));
    NCHECK("gz_accept", spec_gz_accepts("br;q=1.0, gzip;q=0.001"));
    NCHECK("gz_accept", !spec_gz_accepts(NULL));
    NCHECK("gz_accept", !spec_gz_accepts(""));
    NCHECK("gz_accept", !spec_gz_accepts("identity"));
    NCHECK("gz_accept", !spec_gz_accepts("deflate, br"));
    NCHECK("gz_accept", !spec_gz_accepts("gzip;q=0"));
    NCHECK("gz_accept", !spec_gz_accepts("gzip; q=0.000, deflate"));
    NCHECK("gz_accept", !spec_gz_accepts("x-gzip, *"));
    NCHECK("gz_accept", !spec_gz_accepts("gzipx"));
}

static void t_frame(void)
{
    static const uint8_t v[] = "123456789";
    NCHECK("gz_frame", ref_crc32(v, 9) == 0xCBF43926u);   // контрольное значение CRC-32 — проверка самого эталона
    uint8_t data[300];
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7 + 3);
    uint8_t buf[10 + 5 + sizeof data + 8];
    uint8_t *d = buf + 10;
    d[0] = 0x01;
    d[1] = (uint8_t)(sizeof data & 0xff);
    d[2] = (uint8_t)(sizeof data >> 8);
    d[3] = (uint8_t)~d[1];
    d[4] = (uint8_t)~d[2];
    memcpy(d + 5, data, sizeof data);
    size_t total = spec_gz_frame(buf, sizeof buf, 5 + sizeof data, ref_crc32(data, sizeof data), (uint32_t)sizeof data);
    NCHECK("gz_frame", total == sizeof buf);
    NCHECK("gz_frame", check_gz_stored(buf, total, data, sizeof data));
    NCHECK("gz_frame", spec_gz_frame(buf, sizeof buf - 1, 5 + sizeof data, 0, 0) == 0);   // не помещается — 0
    // Тело короче 1024 Б не сжимается; место под deflate — тело с рамкой не длиннее несжатого; слот — шаг 4 КБ.
    NCHECK("gz_frame", !spec_gz_worth(1023) && spec_gz_worth(1024) && spec_gz_worth(32768));
    NCHECK("gz_frame", spec_gz_deflate_room(32768) == 32768 - 18 && spec_gz_deflate_room(18) == 0);
    NCHECK("gz_frame", spec_gz_slot_cap(32768) == 32768 && spec_gz_slot_cap(28022) == 28672 && spec_gz_slot_cap(66227) == 69632);
}

static void t_pick(void)
{
    // Выключено или клиент не просил gzip — прежний ответ, не промах.
    NCHECK("gz_pick", spec_gz_pick(0, true, true, 5, 900, 5) == SPEC_GZ_PLAIN);
    NCHECK("gz_pick", spec_gz_pick(1, false, true, 5, 900, 5) == SPEC_GZ_PLAIN);
    // Готово тело ТОГО ЖЕ поколения — сжатое.
    NCHECK("gz_pick", spec_gz_pick(1, true, true, 5, 900, 5) == SPEC_GZ_HIT);
    NCHECK("gz_pick", spec_gz_pick(6, true, true, 0xFFFFFFFFu, 900, 0xFFFFFFFFu) == SPEC_GZ_HIT);
    // Поколение сменилось или слот не готов — промах (несжатое + запрос задаче).
    NCHECK("gz_pick", spec_gz_pick(1, true, true, 4, 900, 5) == SPEC_GZ_MISS);
    NCHECK("gz_pick", spec_gz_pick(1, true, true, 6, 900, 5) == SPEC_GZ_MISS);
    NCHECK("gz_pick", spec_gz_pick(1, true, false, 5, 900, 5) == SPEC_GZ_MISS);
    // Тело того поколения не сжимается (короче порога / не сжалось) — прежний ответ, не промах.
    NCHECK("gz_pick", spec_gz_pick(1, true, true, 5, 0, 5) == SPEC_GZ_PLAIN);
}

static void t_mem(void)
{
    const size_t comp = 167744u;   // sizeof(tdefl_compressor) ROM-сборки
    NCHECK("gz_mem", spec_gz_mem_ok(comp, comp + SPEC_GZ_PSRAM_RESERVE, comp));
    NCHECK("gz_mem", !spec_gz_mem_ok(comp, comp + SPEC_GZ_PSRAM_RESERVE - 1, comp + 100000));
    NCHECK("gz_mem", !spec_gz_mem_ok(comp, 4000000, comp - 1));   // нет блока нужного размера
    NCHECK("gz_mem", !spec_gz_mem_ok(comp, 100, 100));            // свободно меньше need — без переполнения size_t
    NCHECK("gz_mem", spec_gz_mem_ok(comp, 430212, 385024));       // доза ВКЛ, минимум soak S3: компрессор ещё помещается
}

int main(void)
{
    t_level();
    t_accept();
    t_frame();
    t_pick();
    t_mem();
    if (g_fail) {
        printf("test_spectrum_gz_plan: %d FAIL\n", g_fail);
        return 1;
    }
    printf("test_spectrum_gz_plan: OK\n");
    return 0;
}
