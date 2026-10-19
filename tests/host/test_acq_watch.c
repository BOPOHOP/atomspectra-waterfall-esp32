// Сторож набора (main/acq_watch.h). Штампы — /api/usb-diag платы 14.09: open=3402, гистограмма 941539.
#include "acq_watch.h"
#include "test_util.h"

void acq_watch_suite(void)
{
    const struct { uint8_t in; uint32_t now, open, hist, resend; bool exp; } c[] = {
        {ACQ_INTENT_RUN,     941839, 3402, 941539, 0,      false}, // набор идёт, кадр 0,3 с назад
        {ACQ_INTENT_RUN,     977018, 3402, 941539, 0,      true},  // перезагрузка прибора, тишина 35 с
        {ACQ_INTENT_STOP,    977018, 3402, 941539, 0,      false}, // пользователь нажал «Стоп»
        {ACQ_INTENT_UNKNOWN, 977018, 3402, 941539, 0,      false}, // набором управляет TCP-клиент
        {ACQ_INTENT_RUN,     960000, 941000, 900000, 0,    false}, // 19 с после переоткрытия: не взведён
        {ACQ_INTENT_RUN,     977018, 3402, 941539, 967018, false}, // повтор был 10 с назад
        {ACQ_INTENT_RUN,     977018, 3402, 941539, 957018, true},  // повтор был 20 с назад
        {ACQ_INTENT_RUN,     966000, 941000, 900000, 0,    true},  // после переоткрытия кадров нет 25 с
        {ACQ_INTENT_RUN,     961539, 3402, 941539, 0,      true},  // тишина ровно 20 000 мс
        {ACQ_INTENT_RUN,     0x6000, 0xFFFFF000u, 0x5F00, 0, false}, // открыто до переполнения 28 с назад, кадр свежий
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        bool got = acq_watch_resend_due(c[i].in, c[i].now, c[i].open, c[i].hist, c[i].resend);
        if (got != c[i].exp) printf("acq_watch case %u: got %d\n", i, got);
        CHECK(got == c[i].exp);
    }
    // 1.2.37: возраст гистограммы для X-Hist-Age-Ms: нет штампа -> -1; обычный; ноль мс; переполнение uint32 мс; потолок INT32_MAX
    const struct { uint32_t now, ts; int32_t exp; } a[] = {
        {977018, 0, -1}, {977018, 941539, 35479}, {941539, 941539, 0}, {0x00000100u, 0xFFFFFF00u, 0x200}, {0x90000000u, 1, INT32_MAX},
    };
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) {
        int32_t got = acq_hist_age_ms(a[i].now, a[i].ts);
        if (got != a[i].exp) printf("acq_hist_age case %u: got %d\n", i, (int)got);
        CHECK(got == a[i].exp);
    }
}
