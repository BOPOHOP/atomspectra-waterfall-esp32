/* Раунд 3 (У1/У2): сценарии через прежний API text_accum_feed + text_accum_result_span/
text_accum_result_consumes — компилируются и на коде до исправления, где они краснеют. */
#include "text_accum.h"
#include "test_util.h"
#include <string.h>

#define R3A_DUMP_110 \
    "BFF9A132\r\nC1ADF2A0\r\n3FD9122C\r\n8C25CFDF\r\n3EED426C\r\n" \
    "1F771B10\r\nBE15ECCE\r\n34854003\r\n00000000\r\n00000000\r\nDF786A7E\r\n"

static void r3a_build_dump(char *d)
{
    int i;
    memcpy(d, R3A_DUMP_110, 110);
    for (i = 0; i < 28; ++i) {
        memcpy(d + 110 + i * 10, "FFFFFFFF\r\n", 10);
    }
    memcpy(d + 390, "0012ABCD\r\n", 10);
    d[400] = '\0';
}

/* строка idx текста s (разрезка по сериям \r/\n, как в разборщике прибора) в out[64] */
static void r3a_line(const char *s, int idx, char *out)
{
    int i;
    for (i = 0; i <= idx; ++i) {
        while (*s == '\r' || *s == '\n') ++s;
        if (i < idx) {
            while (*s && *s != '\r' && *s != '\n') ++s;
        } else {
            int j = 0;
            while (*s && *s != '\r' && *s != '\n' && j < 63) {
                out[j++] = *s++;
            }
            out[j] = '\0';
            return;
        }
    }
    out[0] = '\0';
}

void text_accum_r3_old_api_suite(void)
{
    static char acc[4096];
    static char d[401];
    int len;
    text_accum_result_t r;
    static const int fr[] = {100, 50, 10};
    int f_idx;

    r3a_build_dump(d);

    /* Блок 1 — У1 на кадрах f */
    for (f_idx = 0; f_idx < 3; ++f_idx) {
        int f = fr[f_idx];
        len = 0;
        for (int o = 0; o + f < 400; o += f) {          /* все кадры, кроме последнего */
            r = text_accum_feed(acc, &len, 4096, d + o, f);
            if (text_accum_result_consumes(r)) len = 0;
        }
        r = text_accum_feed(acc, &len, 4096, d, f);    /* первый кадр НОВОГО дампа того же прибора */
        CHECK(r != TEXT_ACCUM_CAL);                    /* хвост старого + начало нового — не дамп: серийник не брать */
    }

    /* Блок 2 — У2: мусор "-ok\n" и первые 200 байт дампа ОДНИМ пакетом, затем остальные 200 байт */
    {
        char pkt[210];
        memcpy(pkt, "-ok\n", 4);
        memcpy(pkt + 4, d, 200);
        len = 0;
        r = text_accum_feed(acc, &len, 4096, pkt, 204);
        CHECK(r == TEXT_ACCUM_CAL_COEFFS);
        r = text_accum_feed(acc, &len, 4096, d + 200, 200);
        CHECK(r == TEXT_ACCUM_CAL);                    /* продолжение дампа не сбросило окно */
        int o = 0, e = 0;
        if (text_accum_result_span(r, acc, len, &o, &e)) {
            char ln[64];
            acc[e] = '\0';
            r3a_line(acc + o, 39, ln);
            CHECK(strcmp(ln, "0012ABCD") == 0);
        }
    }
}
