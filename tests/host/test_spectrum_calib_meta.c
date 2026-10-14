#include "spectrum_calib_meta.h"
#include "test_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

/* Реальный калибровочный вектор с платы (копируется дословно) */
static const double REAL_CALIB[5] = {3.52287433185117, 0.383773900647254, 2.67593756344673e-05, -6.49007437672639e-09, 5.67558675724181e-13};

/* Создаёт источник в куче (структура ~33 КБ, нельзя на стеке) */
static spectrum_data_t *make_src(void)
{
    spectrum_data_t *s = (spectrum_data_t *)calloc(1, sizeof(*s));
    if (s == NULL) {
        exit(2);
    }
    memcpy(s->calibration, REAL_CALIB, sizeof(REAL_CALIB));
    s->calib_order = 3;
    s->calib_valid = true;
    s->valid = true;
    for (int i = 0; i < 63; i++) {
        s->serial_number[i] = (char)('A' + (i % 26));
    }
    s->serial_number[63] = '\0';
    for (uint32_t i = 0; i < 8192u; i++) {
        s->bins[i] = i * 7u + 1u;
    }
    s->total_counts = 141502u;
    s->total_time_sec = 197u;
    return s;
}

/* Отравляет приёмник, чтобы поймать не скопированные поля */
static void fill_poison(spectrum_calib_meta_t *m)
{
    memset(m, 0xA5, sizeof(*m));
}

static void test_calib_meta_calibration(void)
{
    spectrum_data_t *src = make_src();
    spectrum_calib_meta_t m;

    fill_poison(&m);
    spectrum_calib_meta_from(src, &m);
    NCHECK("calib_meta_calibration", memcmp(m.calibration, REAL_CALIB, sizeof(REAL_CALIB)) == 0);

    /* Второй источник с особыми значениями (знак нуля, денормализованное) */
    spectrum_data_t *src2 = make_src();
    src2->calibration[0] = -0.0;
    src2->calibration[1] = 1e-300;
    src2->calibration[2] = 0.0;
    src2->calibration[3] = 1.0;
    src2->calibration[4] = -1.0;

    fill_poison(&m);
    spectrum_calib_meta_from(src2, &m);
    NCHECK("calib_meta_calibration", memcmp(m.calibration, src2->calibration, sizeof(src2->calibration)) == 0);

    free(src);
    free(src2);
}

static void test_calib_meta_order(void)
{
    spectrum_data_t *src = make_src();
    spectrum_calib_meta_t m;

    for (int order = 0; order <= 4; order++) {
        src->calib_order = order;
        fill_poison(&m);
        spectrum_calib_meta_from(src, &m);
        NCHECK("calib_meta_order", m.calib_order == order);
    }

    free(src);
}

static void test_calib_meta_flags(void)
{
    spectrum_data_t *src = make_src();
    spectrum_calib_meta_t m;

    const bool combos[4][2] = {
        {false, false},
        {false, true},
        {true, false},
        {true, true}
    };

    for (int i = 0; i < 4; i++) {
        bool v = combos[i][0];
        bool cv = combos[i][1];
        src->valid = v;
        src->calib_valid = cv;

        fill_poison(&m);
        spectrum_calib_meta_from(src, &m);

        NCHECK("calib_meta_flags", (m.valid ? 1 : 0) == (v ? 1 : 0));
        NCHECK("calib_meta_flags", (m.calib_valid ? 1 : 0) == (cv ? 1 : 0));
        NCHECK("calib_meta_flags", *(unsigned char *)&m.valid == (v ? 1u : 0u));
        NCHECK("calib_meta_flags", *(unsigned char *)&m.calib_valid == (cv ? 1u : 0u));
    }

    free(src);
}

static void test_calib_meta_serial(void)
{
    spectrum_data_t *src = make_src();
    spectrum_calib_meta_t m;

    /* Полный серийник: 63 символа + '\0' */
    fill_poison(&m);
    spectrum_calib_meta_from(src, &m);
    NCHECK("calib_meta_serial", memchr(m.serial_number, 0, 64) == &m.serial_number[63]);   /* первый нуль — ровно на 63-м байте (без выхода за 64) */
    NCHECK("calib_meta_serial", m.serial_number[63] == '\0');
    NCHECK("calib_meta_serial", memcmp(m.serial_number, src->serial_number, 64) == 0);

    /* Короткий серийник, остальное обнулено */
    memset(src->serial_number, 0, sizeof(src->serial_number));
    strcpy(src->serial_number, "SN-0042");
    fill_poison(&m);
    spectrum_calib_meta_from(src, &m);
    NCHECK("calib_meta_serial", strcmp(m.serial_number, "SN-0042") == 0);

    /* Пустой серийник */
    memset(src->serial_number, 0, sizeof(src->serial_number));
    fill_poison(&m);
    spectrum_calib_meta_from(src, &m);
    NCHECK("calib_meta_serial", m.serial_number[0] == '\0');

    free(src);
}

static void test_calib_meta_size(void)
{
    NCHECK("calib_meta_size", sizeof(spectrum_calib_meta_t) < 256);
    NCHECK("calib_meta_size", sizeof(spectrum_calib_meta_t) < sizeof(spectrum_data_t) / 100);
}

void spectrum_calib_meta_suite(void)
{
    test_calib_meta_calibration();
    test_calib_meta_order();
    test_calib_meta_flags();
    test_calib_meta_serial();
    test_calib_meta_size();
}
