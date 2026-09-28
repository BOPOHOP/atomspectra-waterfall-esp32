// Предикаты #AWF-12 (main/calib_autoread.h) — калибровка "не задана" (все нули),
// "считалась успешно" (CRC ok + не все нули), команда-старт набора, гейт-кулдаун
// авто-запроса -cal перед -sta.
#include "calib_autoread.h"
#include "test_util.h"

void calib_autoread_suite(void)
{
    // calib_is_missing: невалидна ИЛИ все коэффициенты точно 0.0.
    {
        const struct { double c[5]; bool valid; bool exp; } t[] = {
            {{0,0,0,0,0}, false, true},   // невалидна, нули — не задана
            {{1,0,0,0,0}, false, true},   // невалидна, но есть ненулевой — всё равно не задана
            {{0,0,0,0,0}, true,  true},   // валидна, но все нули — не задана (ТЗ)
            {{0,0,3.5,0,0}, true, false}, // валидна, один ненулевой (order=2) — задана
            {{1,2,3,4,5}, true,  false},  // валидна, все ненулевые — задана
            {{-2.0,0,0,0,0}, true, false},// отрицательный коэффициент — тоже "задана"
        };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
            bool got = calib_is_missing(t[i].c, 5, t[i].valid);
            if (got != t[i].exp) printf("calib_is_missing case %u: got %d\n", i, got);
            CHECK(got == t[i].exp);
        }
    }

    // calib_read_is_success: CRC ok И не все коэффициенты нулевые.
    {
        const struct { bool crc_ok; double c[5]; bool exp; } t[] = {
            {false, {1,2,3,4,5}, false},     // CRC не сошёлся — неуспех, даже с данными
            {false, {0,0,0,0,0}, false},     // CRC не сошёлся, нули — неуспех
            {true,  {0,0,0,0,0}, false},     // CRC ok, но нулевой дамп — НЕ успех (ТЗ)
            {true,  {0,0,0,0,1e-300}, true}, // CRC ok, один сколь угодно малый ненулевой — успех
            {true,  {1,2,3,4,5}, true},      // CRC ok, все ненулевые — успех
        };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
            bool got = calib_read_is_success(t[i].crc_ok, t[i].c, 5);
            if (got != t[i].exp) printf("calib_read_is_success case %u: got %d\n", i, got);
            CHECK(got == t[i].exp);
        }
    }

    // cmd_is_acq_start: "-sta" с параметрами или без; хвостовые пробельные обрезаются,
    // ведущие — нет (тот же приём, что cmd_is_device_reset).
    {
        const struct { const char *cmd; bool exp; } t[] = {
            {"-sta",       true},  {"-sta 60",   true}, {"-sta -r",    true},
            {"-sta\t-r",   true},  {"-sta\r\n",  true}, {"-sta  ",     true},
            {"-sta\t60\t-r", true},
            {"-stax",      false}, {"-stat",     false}, {"-sto",       false},
            {"",           false}, {" -sta",     false}, {"-st",        false},
            {"-start",     false},
        };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
            bool got = cmd_is_acq_start(t[i].cmd);
            if (got != t[i].exp) printf("cmd_is_acq_start case %u ('%s'): got %d\n", i, t[i].cmd, got);
            CHECK(got == t[i].exp);
        }
    }

    // calib_autoread_should_request: гейт "переход намерения" + кулдаун 30000 мс.
    {
        CHECK(calib_autoread_should_request(true, 100000u, 0u) == false);           // намерение уже RUN — гейт закрыт
        CHECK(calib_autoread_should_request(false, 100000u, 0u) == true);           // никогда не запрашивали — можно
        CHECK(calib_autoread_should_request(false, 100000u, 70001u) == false);      // 29999 мс < кулдауна — рано
        CHECK(calib_autoread_should_request(false, 100000u, 70000u) == true);       // ровно порог 30000 мс — можно
        // Переполнение millis (тот же класс, что wifi_return_backoff_elapsed):
        // now=16, last=0xFFFFFFF0 -> прошло всего 32 мс сквозь wrap -> рано.
        CHECK(calib_autoread_should_request(false, 16u, 0xFFFFFFF0u) == false);
        // now сдвинут ещё на 40000 мс после last (через wrap) -> порог пройден.
        CHECK(calib_autoread_should_request(false, 39984u, 0xFFFFFFF0u) == true);
    }
}
