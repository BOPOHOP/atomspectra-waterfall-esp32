// #AWF-12b F1 (release-gate-1.2.28-code.md): роутер накопителя текстовых
// ответов прибора (main/text_accum.h). Реальные форматы ответов — из отчёта
// и boot_fw23.log/f5-stress-after.log: "-ok\r\n" (после -rst/-sta), дамп -cal
// 40 строк по 8 hex через \r\n, "-inf" одной строкой (VERSION..PileUpThr),
// " Tcpot [...]" с ведущим пробелом.
#include "text_accum.h"
#include "test_util.h"
#include <string.h>

// Строит N строк "AAAAAAAA\r\n" (боевой формат дампа -cal: 8 hex + \r\n,
// содержимое конкретных hex для роутера не важно, важны длина/формат) в buf,
// возвращает длину. cap должен вмещать n*10 байт.
static int build_cal_lines(char *buf, int n, int cap)
{
    int off = 0;
    for (int i = 0; i < n && off + 10 <= cap; i++) {
        memcpy(buf + off, "AAAAAAAA\r\n", 10);
        off += 10;
    }
    return off;
}

void text_accum_predicates_suite(void)
{
    CHECK(text_is_hex_digit('0') && text_is_hex_digit('9') && text_is_hex_digit('A') &&
          text_is_hex_digit('f') && !text_is_hex_digit('g') && !text_is_hex_digit('-'));

    CHECK(text_looks_like_cal_dump_start("AAAAAAAA\r\nrest", 14) == true);
    CHECK(text_looks_like_cal_dump_start("AAAAAAAA\nrest", 13) == true);   // одиночный \n тоже конец строки
    CHECK(text_looks_like_cal_dump_start("-ok\r\n", 5) == false);          // '-' не hex
    CHECK(text_looks_like_cal_dump_start("AAAAAAAAX", 9) == false);        // 9-й байт не CR/LF
    CHECK(text_looks_like_cal_dump_start("AAAAAAA", 7) == false);          // короче 9 байт

    char lines2[20];
    int n2 = build_cal_lines(lines2, 2, sizeof lines2);
    CHECK(text_accum_is_clean_cal_prefix(lines2, n2) == true);
    CHECK(text_accum_is_clean_cal_prefix("", 0) == true);                  // пусто — тривиальный префикс
    CHECK(text_accum_is_clean_cal_prefix("-ok\r\n", 5) == false);          // не кратно 10 / не hex-формат
    CHECK(text_accum_is_clean_cal_prefix("AAAAAAAAXX", 10) == false);      // 10 байт, но не \r\n в конце
    // RT2 (release-gate-1.2.28-code-rc2.md, выжившие X8/X13): отрицательные
    // проверки на CR (не только LF) и на все 8, а не 7, hex-позиций якоря.
    CHECK(text_accum_is_clean_cal_prefix("AAAAAAAA\n\n", 10) == false);    // LF есть, а CR (8-й байт) — нет
    CHECK(text_looks_like_cal_dump_start("AAAAAAA-\r", 9) == false);       // 8-й символ не hex
}

void text_accum_triggers_suite(void)
{
    // Порог >=39 '\n' (не 40) — намеренно, терпим отсутствие/обрыв \r\n у 40-й
    // строки (серийник, spectrum.c "Serial rejected (misaligned -cal)").
    // +1 в размере буферов — место под '\0' после ровно N*10 байт дампа
    // (нашла ASan: dump40 заполнял все 400 байт, запись '\0' в [400] —
    // переполнение стека; тот же класс проверки, что защищает прод-код).
    char dump38[381]; int n38 = build_cal_lines(dump38, 38, sizeof dump38 - 1);
    dump38[n38] = '\0';
    CHECK(text_accum_is_complete_cal(dump38, n38) == false);   // 38 строк = 38 \n — ещё рано
    char dump39[391]; int n39 = build_cal_lines(dump39, 39, sizeof dump39 - 1);
    dump39[n39] = '\0';
    CHECK(text_accum_is_complete_cal(dump39, n39) == true);    // 39 строк = 39 \n — уже достаточно
    char dump40[401]; int n40 = build_cal_lines(dump40, 40, sizeof dump40 - 1);
    dump40[n40] = '\0';
    CHECK(text_accum_is_complete_cal(dump40, n40) == true);    // 40 строк — обычный случай

    // RT2 (выживший X11): 40 '\n' есть, но текст НЕ начинается с hex-строки
    // (якорь на позицию 0 обязателен, одного счётчика '\n' недостаточно).
    char notdump[400]; int ndn = 0;
    for (int i = 0; i < 40; i++) { memcpy(notdump + ndn, "-ok\r\n", 5); ndn += 5; }
    CHECK(text_accum_is_complete_cal(notdump, ndn) == false);

    CHECK(text_accum_is_complete_short_ack("-ok\r\n", 5) == true);
    CHECK(text_accum_is_complete_short_ack("-ok\n", 4) == true);
    CHECK(text_accum_is_complete_short_ack("-okX\r\n", 6) == false);   // не то слово
    CHECK(text_accum_is_complete_short_ack("-ok\r\nAAAAAAAA\r\n", 15) == false); // хвост после -ok — не короткий ack целиком

    CHECK(text_accum_is_complete_inf("VERSION 23 ... PileUpThr 10\r\n") == true);
    CHECK(text_accum_is_complete_inf("VERSION 23 only") == false);      // нет PileUpThr

    CHECK(text_accum_is_complete_tcpot(" Tcpot [1,2,3]", 14) == true);  // ведущий пробел — боевой формат
    CHECK(text_accum_is_complete_tcpot(" Tcpot [1,2,3", 13) == false);  // нет закрывающей ]

    // RT2 (выживший X6): "PileUpThr" без "VERSION " — не -inf целиком.
    CHECK(text_accum_is_complete_inf("PileUpThr 10\r\n") == false);
}

// RT2 (выживший X10): точная граница переполнения — cap-128, не cap-1. cap=200
// (не 4096) для точности границы без гигантских буферов теста.
void text_accum_overflow_boundary_suite(void)
{
    char accum[200]; int len = 0;
    char g1[71]; memset(g1, 'z', sizeof g1);
    CHECK(text_accum_feed(accum, &len, sizeof accum, g1, sizeof g1) == TEXT_ACCUM_NONE);
    CHECK(len == 71);   // cap-128 = 72, ещё не достигли
    char g2[1] = {'z'};
    CHECK(text_accum_feed(accum, &len, sizeof accum, g2, 1) == TEXT_ACCUM_OVERFLOW);  // len=72=cap-128
}

// Симулирует usb_host_cdc.c::usb_host_cdc_apply_text_accum_result — сброс
// аккумулятора на любой результат != NONE (буфер уже прочитан вызывающим).
static text_accum_result_t feed_and_apply(char *accum, int *len, int cap,
                                           const char *pkt, int pkt_len)
{
    text_accum_result_t r = text_accum_feed(accum, len, cap, pkt, pkt_len);
    if (r != TEXT_ACCUM_NONE) *len = 0;
    return r;
}

// F1 (release-gate-1.2.28-code.md), КРИТИЧНО: сценарии "Сброс"->"Старт" и
// загрузка с очисткой+автостартом — прибор шлёт "-ok\r\n" на -rst/-sta ПЕРЕД
// авто-запрошенным -cal (без предварительного -inf, в отличие от ручного
// "Считать"). Без исправления дамп дописывался после "-ok\r\n" и терялся.
void text_accum_f1_regression_suite(void)
{
    char accum[4096]; int len = 0;
    // RT3 (release-gate-1.2.28-code-rc2.md): text_accum_feed НАПРЯМУЮ, не
    // feed_and_apply — сброс на SHORT_ACK теперь часть контракта роутера
    // (RO3), проверяем прошивочный код, а не тестовую копию правила.
    text_accum_result_t r1 = text_accum_feed(accum, &len, sizeof accum, "-ok\r\n", 5);
    CHECK(r1 == TEXT_ACCUM_SHORT_ACK);
    CHECK(len == 0);   // аккумулятор пуст для следующего ответа — сбросила сама text_accum_feed

    char dump[400]; int dn = build_cal_lines(dump, 40, sizeof dump);
    text_accum_result_t r2 = feed_and_apply(accum, &len, sizeof accum, dump, dn);
    CHECK(r2 == TEXT_ACCUM_CAL);   // дамп распознан целиком, а не потерян за "-ok"
}

// T1(б): дамп, раздробленный прибором на несколько кадров SHPROTO ровно по
// границе строки (U6 отчёта — не подтверждено, но комментарий usb_host_cdc.c
// заявлял "одним кадром", проверять на живучесть). Продолжение НЕ должно
// сбрасывать уже накопленный чистый префикс, хоть само и похоже на "начало
// дампа" (каждая строка дампа выглядит так).
void text_accum_split_dump_suite(void)
{
    char accum[4096]; int len = 0;
    char part1[200]; int n1 = build_cal_lines(part1, 20, sizeof part1);
    text_accum_result_t r1 = feed_and_apply(accum, &len, sizeof accum, part1, n1);
    CHECK(r1 == TEXT_ACCUM_NONE);
    CHECK(len == 200);   // накопилось, не сброшено

    char part2[200]; int n2 = build_cal_lines(part2, 20, sizeof part2);
    text_accum_result_t r2 = feed_and_apply(accum, &len, sizeof accum, part2, n2);
    CHECK(r2 == TEXT_ACCUM_CAL);   // склеилось в полный дамп (40 строк), не потеряно
}

// T1(в): "-ok" + "-inf" — короткий ack перед плановым/ручным -inf (не только
// перед -cal), тот же класс, что F1.
void text_accum_ok_then_inf_suite(void)
{
    char accum[4096]; int len = 0;
    CHECK(feed_and_apply(accum, &len, sizeof accum, "-ok\r\n", 5) == TEXT_ACCUM_SHORT_ACK);
    const char *inf = "VERSION 23 rise=1 fall=2 PileUpThr 10\r\n";
    int lenBefore = len;
    text_accum_result_t r = text_accum_feed(accum, &len, sizeof accum, inf, (int)strlen(inf));
    CHECK(r == TEXT_ACCUM_INF);   // триггер -inf терпит мусор В НАЧАЛЕ (strstr-подстрока)…
    // …но БЕЗ сброса "-ok" остался бы ПРЕФИКСОМ accum, и spectrum_process_
    // info_response() (main/spectrum.c) режет accum на строки С ПОЗИЦИИ 0 —
    // "-ok" встал бы lbuf[0], сдвинув индексы всех полей -inf (тот же класс
    // порчи, что уже был между -cal/-inf до #CMD-1, F1 "Побочный эффект").
    CHECK(lenBefore == 0);                       // аккумулятор был пуст ДО -inf
    CHECK(strncmp(accum, "VERSION ", 8) == 0);    // -inf с позиции 0, не "-ok\r\nVERSION..."
}

// F1 приём (1), обобщённый backstop: НЕ-"-ok" мусор (гипотетический
// неопознанный короткий ответ, U1 отчёта — ответ на -sto не подтверждён)
// тоже не должен приклеивать к себе следующий дамп.
void text_accum_generic_garbage_before_dump_suite(void)
{
    char accum[4096]; int len = 0;
    // "-er\r\n" не матчит НИ один существующий триггер — застревает в
    // аккумуляторе, как "-ok" до исправления variant(2).
    memcpy(accum, "-er\r\n", 5); len = 5;
    char dump[400]; int dn = build_cal_lines(dump, 40, sizeof dump);
    CHECK(feed_and_apply(accum, &len, sizeof accum, dump, dn) == TEXT_ACCUM_CAL);
}

// Переполнение без триггера (cap как в проде, s_text_accum=4096) — несколько
// кусков мусора подряд копятся, ни один триггер не срабатывает, отброс с
// overflow на пороге cap-128 — не переполнение самого буфера.
void text_accum_overflow_suite(void)
{
    char accum[4096]; int len = 0;
    char garbage[1000];
    memset(garbage, 'z', sizeof garbage);   // 'z' не hex — ни один триггер не сработает
    text_accum_result_t r = TEXT_ACCUM_NONE;
    int rounds = 0;
    // RT3: text_accum_feed напрямую — сброс на OVERFLOW тоже часть контракта
    // роутера (RO3) теперь, не тестовой обёртки.
    while (r == TEXT_ACCUM_NONE && rounds < 10) {
        r = text_accum_feed(accum, &len, sizeof accum, garbage, sizeof garbage);
        rounds++;
    }
    CHECK(r == TEXT_ACCUM_OVERFLOW);
    CHECK(rounds > 1);   // действительно копилось несколько кусков, не с первого
    CHECK(len == 0);
}
