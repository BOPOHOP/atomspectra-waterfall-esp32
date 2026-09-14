// Сторож набора (main/acq_watch.h). Штампы — из /api/usb-diag живой платы 14.09:
// open=3402, последняя гистограмма перед -sto 941539, замер через 35 с — 977018.
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
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        bool got = acq_watch_resend_due(c[i].in, c[i].now, c[i].open, c[i].hist, c[i].resend);
        if (got != c[i].exp) printf("acq_watch case %u: got %d\n", i, got);
        CHECK(got == c[i].exp);
    }
}
