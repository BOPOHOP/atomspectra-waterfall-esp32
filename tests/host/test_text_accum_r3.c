/* Раунд 3 (У1/У2/У6): полная проводка разбора текстовых ответов прибора — text_accum_feed_m +
text_accum_dispatch + text_accum_flush (тишина), модель разборщика — счёт калибровок и серийник по строке 39. */
#include "text_accum.h"
#include "test_util.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define R3_DUMP_110 \
    "BFF9A132\r\nC1ADF2A0\r\n3FD9122C\r\n8C25CFDF\r\n3EED426C\r\n" \
    "1F771B10\r\nBE15ECCE\r\n34854003\r\n00000000\r\n00000000\r\nDF786A7E\r\n"

#define R3_INF \
    "VERSION 13 RISE 11 FALL 27 NOISE 20 F 14000000.00 MAX 48144 HYST 1 MODE 0 STEP 3 t 50293 POT 60 POT2 105 T1 29.5 T2 OFF T3 OFF Prise 0 Srise 0 OUT 0..0/1 Pfall 0 Sfall 0 TC ON TCpot ON Tco [-40 41145 -16 48651 -12 49902 -8 49510 -4 49927 0 50170 4 51469 8 51254 12 50931 16 50310 20 49856 24 49153 28 48434 32 47663 36 46975 40 44517 44 43802 48 43083 52 42322 56 41687] TP 1000 PileUp [] PileUpThr 8192\n"

#define R3_TCPOT \
    " Tcpot [-40 51 -16 45 -12 44 -8 43 -4 42 0 42 4 42 8 42 12 42 16 43 20 43 24 44 28 45 32 47 36 49 40 53 44 59 48 69 52 84 56 105]"

static void r3_build_dump(char *d)
{
    int i;
    memcpy(d, R3_DUMP_110, 110);
    for (i = 0; i < 28; ++i) {
        memcpy(d + 110 + i * 10, "FFFFFFFF\r\n", 10);
    }
    memcpy(d + 390, "0012ABCD\r\n", 10);
    d[400] = '\0';
}

/* тот же прибор после перекалибровки: другой c0 (1.0), CRC пересчитан — начало дампа другое */
static void r3_build_dump2(char *d)
{
    char crc[12];
    r3_build_dump(d);
    memcpy(d, "3FF00000", 8);
    snprintf(crc, sizeof crc, "%08X", (unsigned)text_accum_crc32_lines(d, 10));
    memcpy(d + 100, crc, 8);
}

typedef struct {
    char acc[4096];
    int len;
    text_accum_marks_t m;
    int calib;           /* применений калибровки (CRC-окно в начале разобранного текста) */
    int serial_sets;     /* сколько раз записан серийник */
    int wrong_serial;    /* записан серийник не "0012ABCD" */
    char serial[16];
    char info[512];      /* последний разобранный -inf */
    char cal_head[9];    /* первые 8 символов последнего разобранного дампа */
    int tcpot;
} r3_dev_t;

static r3_dev_t R3;
static char R3D[401];

/* Приёмник (модель spectrum_process_info_response/_tcpot_response) */
static void r3_sink(void *ctx, text_accum_result_t r, const char *text)
{
    r3_dev_t *dv = (r3_dev_t *)ctx;
    int n;
    const char *p;
    int line;
    char l39[16];

    if (r == TEXT_ACCUM_TCPOT) {
        dv->tcpot++;
        return;
    }

    if (strstr(text, "VERSION ") != NULL) {
        /* усечение намеренное (как у приёмника в прошивке); явная копия вместо
         * snprintf — gcc 13 с -Werror=format-truncation считает её ошибкой */
        size_t k = strlen(text);
        if (k >= sizeof dv->info) k = sizeof dv->info - 1;
        memcpy(dv->info, text, k);
        dv->info[k] = '\0';
        return;
    }

    n = (int)strlen(text);
    if (n >= 8) {
        memcpy(dv->cal_head, text, 8);
        dv->cal_head[8] = '\0';
    } else {
        dv->cal_head[0] = '\0';
    }

    if (text_accum_cal_window_at(text, n, 0)) {
        dv->calib++;
    }

    /* Обработка строк для поиска серийника */
    memset(l39, 0, sizeof l39);
    p = text;
    line = 0;
    while (*p) {
        /* Пропуск разделителей перед строкой */
        while (*p == '\r' || *p == '\n') ++p;
        if (!*p) break;

        /* Копирование строки */
        if (line == 39) {
            int j = 0;
            while (*p && *p != '\r' && *p != '\n' && j < 15) {
                l39[j++] = *p++;
            }
            l39[j] = '\0';
        } else {
            while (*p && *p != '\r' && *p != '\n') ++p;
        }

        line++;
    }

    if (line >= 40 && strlen(l39) == 8) {
        int i;
        bool is_hex = true;
        for (i = 0; i < 8; ++i) {
            if (!text_is_hex_digit(l39[i])) {
                is_hex = false;
                break;
            }
        }
        if (is_hex && strcmp(l39, "FFFFFFFF") != 0) {
            dv->serial_sets++;
            if (strcmp(l39, "0012ABCD") != 0) {
                dv->wrong_serial++;
            }
            strcpy(dv->serial, l39);
        }
    }
}

static void r3_reset(void)
{
    memset(&R3, 0, sizeof R3);
}

static void r3_feed(const char *p, int n)
{
    text_accum_result_t r = text_accum_feed_m(R3.acc, &R3.len, (int)sizeof R3.acc, &R3.m, p, n);
    text_accum_dispatch(r, R3.acc, &R3.len, (int)sizeof R3.acc, &R3.m, r3_sink, &R3);
}

static void r3_quiet(void)   /* прошла тишина TEXT_ACCUM_QUIET_MS */
{
    text_accum_result_t r = text_accum_flush(R3.acc, &R3.len, (int)sizeof R3.acc, &R3.m);
    text_accum_dispatch(r, R3.acc, &R3.len, (int)sizeof R3.acc, &R3.m, r3_sink, &R3);
}

/* дамп кадрами по f байт; потерянный первый/последний кадр пропускается */
static void r3_frames(int f, bool skip_first, bool skip_last)
{
    int nf = (400 + f - 1) / f;
    int i;
    for (i = 0; i < nf; ++i) {
        if ((i == 0 && skip_first) || (i == nf - 1 && skip_last)) continue;
        int o = i * f;
        int k = 400 - o < f ? 400 - o : f;
        r3_feed(R3D + o, k);
    }
}

static void r3_tail(void)
{
    r3_feed(R3_INF, 404);
    r3_feed(R3_TCPOT, 129);
}

/* после -inf и Tcpot: -inf разобран с "VERSION ", Tcpot — один раз, аккумулятор пуст, чужого серийника не было */
static void r3_check_tail(void)
{
    CHECK(strncmp(R3.info, "VERSION ", 8) == 0);
    CHECK(strlen(R3.info) == 404);
    CHECK(R3.tcpot == 1);
    CHECK(R3.len == 0);
    CHECK(R3.wrong_serial == 0);
}

static const int R3_FR[] = {400, 200, 110, 100, 64, 50, 25, 10};

void text_accum_r3_u1_suite(void)
{
    int i;
    r3_build_dump(R3D);

    for (i = 0; i < (int)(sizeof R3_FR / sizeof R3_FR[0]); ++i) {
        int f = R3_FR[i];

        /* 1. целый дамп кадрами f, тишина, -inf+Tcpot */
        r3_reset();
        r3_frames(f, false, false);
        r3_quiet();
        r3_tail();
        CHECK(R3.calib >= 1);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        r3_check_tail();

        /* 2. то же без тишины (-inf сразу за дампом) */
        r3_reset();
        r3_frames(f, false, false);
        r3_tail();
        CHECK(R3.calib >= 1);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        r3_check_tail();

        /* 3. потерян последний кадр, тишина, затем новый дамп того же прибора кадрами f, тишина, -inf+Tcpot */
        r3_reset();
        r3_frames(f, false, true);
        r3_quiet();
        int c0 = R3.calib;
        r3_frames(f, false, false);
        r3_quiet();
        r3_tail();
        CHECK(R3.calib > c0);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        r3_check_tail();

        /* 4. то же, но без тишины между дампами и перед -inf (повторный -cal сразу) */
        r3_reset();
        r3_frames(f, false, true);
        c0 = R3.calib;
        r3_frames(f, false, false);
        r3_tail();
        CHECK(R3.calib > c0);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        r3_check_tail();

        /* 5. двойная потеря: у первого дампа потерян последний кадр, от второго дошёл только первый кадр (f байт; при f == 400 — весь), тишина, -inf+Tcpot */
        r3_reset();
        r3_frames(f, false, true);
        r3_quiet();
        r3_feed(R3D, f);
        r3_quiet();
        r3_tail();
        r3_check_tail();   /* главное — wrong_serial == 0 */

        /* 6. потерян первый кадр (только при f < 400) */
        if (f < 400) {
            r3_reset();
            r3_frames(f, true, false);
            r3_quiet();
            r3_tail();
            CHECK(R3.serial_sets == 0);
            r3_check_tail();

            /* 7. потерян последний кадр, затем -inf (только при f < 400) */
            r3_reset();
            r3_frames(f, false, true);
            r3_quiet();
            r3_tail();
            CHECK(R3.serial_sets == 0);
            r3_check_tail();
            if (f >= 110) {
                CHECK(R3.calib >= 1); /* окно дошло в первых кадрах */
            }

            /* 8. потерян последний кадр, затем сразу дамп ПОСЛЕ ПЕРЕКАЛИБРОВКИ (начало другое —
               совпадение содержимого не спасает, решает ожидание окна нового дампа) */
            static char d2[401];
            r3_build_dump2(d2);
            r3_reset();
            r3_frames(f, false, true);
            c0 = R3.calib;
            for (int o = 0; o < 400; o += f) r3_feed(d2 + o, 400 - o < f ? 400 - o : f);
            r3_tail();
            CHECK(R3.calib > c0);
            CHECK(strcmp(R3.serial, "0012ABCD") == 0);
            r3_check_tail();
        }
    }
}

void text_accum_r3_u2_suite(void)
{
    static const int fr[] = {200, 110, 64};
    int i;
    r3_build_dump(R3D);

    for (i = 0; i < 3; ++i) {
        int f = fr[i];
        char pkt[420];
        memcpy(pkt, "-ok\n", 4);
        memcpy(pkt + 4, R3D, (size_t)f);
        r3_reset();
        r3_feed(pkt, 4 + f);
        for (int o = f; o < 400; o += f) {
            r3_feed(R3D + o, 400 - o < f ? 400 - o : f);
        }
        r3_quiet();
        r3_tail();
        CHECK(R3.calib >= 1);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        r3_check_tail();
    }

    /* Отдельно для f = 100 (окно в первом пакете не целиком — калибровка теряется) */
    {
        int f = 100;
        char pkt[420];
        memcpy(pkt, "-ok\n", 4);
        memcpy(pkt + 4, R3D, (size_t)f);
        r3_reset();
        r3_feed(pkt, 4 + f);
        for (int o = f; o < 400; o += f) {
            r3_feed(R3D + o, 400 - o < f ? 400 - o : f);
        }
        r3_quiet();
        r3_tail();
        /* проверки только r3_check_tail() */
        r3_check_tail();
    }
}

void text_accum_r3_dispatch_suite(void)
{
    r3_build_dump(R3D);

    /* 1. смещение: «-ok\n»+дамп одним пакетом (404 байта) → в разбор уходит текст с начала окна */
    {
        char pkt[420];
        r3_reset();
        memcpy(pkt, "-ok\n", 4);
        memcpy(pkt + 4, R3D, 400);
        r3_feed(pkt, 404);
        CHECK(strcmp(R3.cal_head, "BFF9A132") == 0);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        CHECK(R3.calib == 1);
        r3_tail();
        r3_check_tail();
    }

    /* 2. мусор с '\0' перед -inf одним пакетом */
    {
        char pkt[410];
        pkt[0] = 'A';
        pkt[1] = 'B';
        pkt[2] = '\0';
        memcpy(pkt + 3, R3_INF, 404);
        r3_reset();
        r3_feed(pkt, 407);
        CHECK(strncmp(R3.info, "VERSION ", 8) == 0);
        CHECK(R3.len == 0);
    }

    /* 3. кадр оборван посреди строки */
    {
        r3_reset();
        r3_feed(R3D, 115);
        CHECK(R3.calib == 1);
        r3_feed(R3D + 115, 285);
        r3_quiet();
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);   /* байт на конце отрезка CAL_COEFFS восстановлен */
        r3_tail();
        r3_check_tail();
    }

    /* 4. отложенный дамп (кадры по 100 Б) и -inf следом одним пакетом */
    {
        r3_reset();
        r3_frames(100, false, false);
        CHECK(R3.serial_sets == 0);   /* последний кадр на границе строки мог быть началом нового дампа — ждём */
        r3_feed(R3_INF, 404);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        CHECK(strncmp(R3.info, "VERSION ", 8) == 0);
        CHECK(R3.len == 0);
    }

    /* 5. отложенный дамп и короткий «-ok\n» */
    {
        r3_reset();
        r3_frames(100, false, false);
        r3_feed("-ok\n", 4);
        CHECK(strcmp(R3.serial, "0012ABCD") == 0);
        CHECK(R3.len == 0);
    }

    /* 6. отложенный дамп разбирается одной тишиной */
    {
        r3_reset();
        r3_frames(100, false, false);
        r3_quiet();
        CHECK(R3.serial_sets == 1);
        CHECK(R3.len == 0);
    }
}
