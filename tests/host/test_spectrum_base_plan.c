// AWF-3: обнаружение сброса прибора и слияние база+прибор (spectrum_base_plan.h).
#include "spectrum_base_plan.h"
#include "test_util.h"

static void test_reset_detection(void)
{
    // Живой сценарий 25.09: D=4451с, база=0, первый STAT после обрыва
    // питания t=11с — обязан быть распознан как сброс.
    CHECK(spectrum_base_reset_detected(11, 0, 4451));
    // Обычная работа: прибор продолжает копить — НЕ сброс.
    CHECK(!spectrum_base_reset_detected(101, 0, 100));
    // После fold (база=50, показано=53): прибор продолжает нормально — НЕ сброс.
    CHECK(!spectrum_base_reset_detected(54, 50, 53));

    // Граница допуска (5с): dev_elapsed_since_base=100, dev=95 -> 95+5=100 —
    // ещё НЕ сброс (легитимная просадка STAT, #FW-12); dev=94 — уже сброс.
    CHECK(!spectrum_base_reset_detected(95, 0, 100));
    CHECK(spectrum_base_reset_detected(94, 0, 100));
    // Равенство времени — НЕ сброс (мутация "без допуска"/"<=" ловится тут же).
    CHECK(!spectrum_base_reset_detected(100, 0, 100));

    // Шлюз перезагрузился, прибор — нет: база=50, D=100 (прибор был на 50),
    // новый STAT прибора t=53 (жил ещё пару c, пока шлюз поднимался) -> НЕ сброс.
    CHECK(!spectrum_base_reset_detected(53, 50, 100));
}

static void test_merge(void)
{
    CHECK(spectrum_base_merge(0, 11) == 11);
    CHECK(spectrum_base_merge(4451, 11) == 4462);
    CHECK(spectrum_base_merge(727021, 1810) == 728831);
    CHECK(spectrum_base_merge(0, 0) == 0);
}

// P2: насыщение на границе UINT32_MAX.
static void test_merge_overflow(void)
{
    CHECK(spectrum_base_merge(0xFFFFFFFFu, 1) == 0xFFFFFFFFu);
    CHECK(spectrum_base_merge(0xFFFFFFF0u, 0x20) == 0xFFFFFFFFu);
    CHECK(spectrum_base_merge(0xFFFFFFFFu, 0) == 0xFFFFFFFFu);
    CHECK(spectrum_base_merge(100, 200) == 300);
}

// P1-b: сброс по СЧЁТУ, независимо от STAT.
static void test_counts_reset(void)
{
    CHECK(spectrum_base_reset_detected_by_counts(9, 0, 267049));       // живой сценарий 25.09
    CHECK(!spectrum_base_reset_detected_by_counts(267049, 0, 267049)); // ровно догнал — не сброс
    CHECK(!spectrum_base_reset_detected_by_counts(267100, 0, 267049)); // обогнал — рост, не сброс
    CHECK(spectrum_base_reset_detected_by_counts(267048, 0, 267049));  // на 1 меньше — сброс (допуск 0)
    CHECK(!spectrum_base_reset_detected_by_counts(5, 267049, 267054)); // после fold: expected=5 — не сброс
}

// Сценарий целиком (через ОРКЕСТРАЦИЮ spectrum_base_commit — тот же порядок,
// что spectrum.c): D восстановлен -> первый коммит сворачивает базу -> рост
// без сворачивания. Время после fold — забота caller (spectrum.c #FW-12).
// F3 (итоговое ревью 25.09): база=0 до первого коммита (не 600, как раньше) —
// свёртка теперь триггерится ТОЛЬКО по счёту (dev=3 < expected=600-0=600),
// не по времени; со старой базой=600 expected был бы 0 и тест держался бы
// на времени, которое spectrum_base_commit больше не смотрит.
static void test_sequence(void)
{
    uint32_t base_bins[3] = {0, 0, 0};
    uint32_t shown_bins[3] = {100, 200, 300};
    spectrum_base_state_t st = { base_bins, 0, 0, shown_bins, 4451, 600 };

    uint32_t dev_bins[3] = {1, 0, 2};
    CHECK(spectrum_base_commit(&st, dev_bins, 3, 3, true, 11));
    CHECK(st.base_counts == 600 && base_bins[0] == 100 && base_bins[2] == 300);
    st.base_time = st.shown_time = 4451 + 11;
    CHECK(shown_bins[0] == 101 && shown_bins[2] == 302 && st.shown_counts == 603);

    uint32_t dev_bins2[3] = {5, 0, 2};
    CHECK(!spectrum_base_commit(&st, dev_bins2, 7, 3, true, 15));
    CHECK(st.base_counts == 600);
    // dev_total(7) — накопительный СЧЁТ прибора с его последнего сброса (не
    // приращение к прошлому shown), поэтому shown = base(600)+dev(7) = 607.
    CHECK(shown_bins[0] == 105 && st.shown_counts == 607);
}

// Живой сценарий 25.09 (второй прогон): D восстановлен (267049,1634с) ->
// ПЕРВАЯ гистограмма прибора после сброса (маленькая, 9) БЕЗ свежего STAT на
// этом коммите. До фикса merge клеил dev поверх базы 0 ДО проверки сброса —
// база получала 9 вместо 267049. Проверка — именно base_counts == D.
static void test_live_bug_no_stat_first_commit(void)
{
    uint32_t base_bins[3] = {0, 0, 0};
    uint32_t shown_bins[3] = {100000, 100000, 67049};   // сумма 267049, как D
    // база ещё НЕ сворачивала ничего (base_counts=0, base_bins=0) — восстановлен
    // именно ПОКАЗЫВАЕМЫЙ (shown) спектр D, база пуста до первого fold.
    spectrum_base_state_t st = { base_bins, 0, 0, shown_bins, 1634, 267049 };

    uint32_t dev_bins[3] = {3, 2, 4};   // крошечный свежий свип, сумма 9
    bool did = spectrum_base_commit(&st, dev_bins, 9, 3, /*stat_fresh=*/false, 0);

    CHECK(did);
    CHECK(st.base_counts == 267049);   // НЕ 9
    CHECK(base_bins[0] == 100000 && base_bins[2] == 67049);
    CHECK(st.shown_counts == 267049 + 9);
}

// F3 (итоговое ревью 25.09): счёт растёт СОГЛАСОВАННО (dev=610 >= expected
// 600), а STAT-время отстало на 10с (590 < 600-5=595, старый код счёл бы это
// сбросом) — база НЕ должна измениться, счёт НЕ должен удвоиться.
static void test_time_only_regression_no_fold(void)
{
    uint32_t base_bins[3] = {50, 20, 30};       // сумма 100
    uint32_t shown_bins[3] = {350, 200, 150};   // сумма 700 (100 база + 600 dev)
    spectrum_base_state_t st = { base_bins, 1000, 100, shown_bins, 1600, 700 };

    uint32_t dev_bins[3] = {305, 200, 105};     // сумма 610 (>= expected 600)
    bool did = spectrum_base_commit(&st, dev_bins, 610, 3, /*stat_fresh=*/true,
                                     /*dev_time_now=*/590);

    CHECK(!did);
    CHECK(st.base_counts == 100 && base_bins[0] == 50 && base_bins[2] == 30);
    CHECK(st.shown_counts == 100 + 610);   // НЕ 100+610+610 (было бы при баге)
}

// N2 (ревью-2): настоящий сброс, но НОВЫЙ свип успел набрать БОЛЬШЕ старого
// (фон 10ч=36000с/100000 → сброс → горячий источник 300с/200000 > 100000) —
// count-проверка одна видит «выросло», не ловит. Ограниченный сигнал по
// времени (dev_time_now=300 < половины dev_elapsed=36000) обязан поймать.
static void test_n2_count_grew_time_catches(void)
{
    uint32_t base_bins[3] = {0, 0, 0};
    uint32_t shown_bins[3] = {40000, 30000, 30000};  // сумма 100000
    spectrum_base_state_t st = { base_bins, 0, 0, shown_bins, 36000, 100000 };

    uint32_t dev_bins[3] = {80000, 60000, 60000};    // сумма 200000
    bool did = spectrum_base_commit(&st, dev_bins, 200000, 3, /*stat_fresh=*/true,
                                     /*dev_time_now=*/300);

    CHECK(did);
    CHECK(st.base_counts == 100000);
    CHECK(st.base_time == 36000);
    CHECK(base_bins[0] == 40000 && base_bins[1] == 30000);
    CHECK(st.shown_counts == 100000 + 200000);
}

// R1 (ревью-3): spectrum_base_commit_should_defer() сама по себе.
static void test_r1_defer_predicate(void)
{
    // !stat_fresh, count НЕ говорит «сброс» (вырос) -> defer.
    CHECK(spectrum_base_commit_should_defer(150000, 0, 100000, false));
    // !stat_fresh, count говорит «сброс» (просадка, как test_live_bug) -> НЕ defer.
    CHECK(!spectrum_base_commit_should_defer(9, 0, 267049, false));
    // stat_fresh=true -> никогда не defer, N2 сам разберётся.
    CHECK(!spectrum_base_commit_should_defer(150000, 0, 100000, true));
}
// R1: полная последовательность коммитов caller'а (spectrum.c) на пуре —
// коммит1 (двусмысленный, без STAT) должен быть ПРОПУЩЕН (state не
// меняется), коммит2 (STAT свежий) видит НЕПОВРЕЖДЁННые base/shown и
// корректно сворачивает по времени (N2). Сценарий ревью: фон 10ч/100000,
// сброс, горячий источник 300с растёт до 200000 к коммиту2. Итог — РОВНО
// 300000 (100000 сессии 1 + 200000 сессии 2), не 400600 и не 300600.
static void test_r1_defer_and_two_commit_sequence(void)
{
    uint32_t base_bins[3] = {0, 0, 0};
    uint32_t shown_bins[3] = {40000, 30000, 30000};  // сумма 100000
    spectrum_base_state_t st = { base_bins, 0, 0, shown_bins, 36000, 100000 };

    // коммит1: !stat_fresh, dev уже вырос (150000) -> caller обязан ПРОПУСТИТЬ.
    CHECK(spectrum_base_commit_should_defer(150000, st.base_counts, st.shown_counts, false));
    // state НЕ ТРОНУТ (caller не звал spectrum_base_commit вовсе).
    CHECK(st.shown_counts == 100000 && st.base_counts == 0);

    // коммит2: STAT свежий, dev=200000, t=301 (сессия 2 продолжается).
    CHECK(!spectrum_base_commit_should_defer(200000, st.base_counts, st.shown_counts, true));
    uint32_t dev_bins2[3] = {80000, 60000, 60000};   // сумма 200000
    bool did = spectrum_base_commit(&st, dev_bins2, 200000, 3, /*stat_fresh=*/true,
                                     /*dev_time_now=*/301);
    CHECK(did);
    CHECK(st.base_counts == 100000);
    CHECK(st.shown_counts == 300000);   // РОВНО 300000, не 400600/300600
}

// AWF-4: живой баг 27.09 (Reset -> время продолжает от старого набора).
// Коммит1: STAT застейджен старым (t=2410с), реально с Reset прошло 1с ->
// spectrum_reset_stat_is_plausible ОБЯЗАН отклонить (гейт caller'а,
// spectrum.c). Без свежего STAT и без сброса по count — коммит ОТЛОЖЕН
// (R1). Коммит2: настоящий свежий STAT (t=6с, elapsed=6с) — база НЕ
// сворачивается в 2410, dev_resets не растёт.
static void test_awf4_reset_stat_race(void)
{
    uint32_t base_bins[3]  = {0, 0, 0};
    uint32_t shown_bins[3] = {0, 0, 0};
    spectrum_base_state_t st = { base_bins, 0, 0, shown_bins, 0, 0 };

    bool stat_fresh1 = spectrum_reset_stat_is_plausible(2410, 1);
    CHECK(!stat_fresh1);   // гейт отклонил старый STAT

    uint32_t dev_bins1[3] = {5, 4, 3};   // первый мелкий свип, сумма 12
    CHECK(spectrum_base_commit_should_defer(12, st.base_counts, st.shown_counts, stat_fresh1));
    CHECK(st.shown_counts == 0 && st.base_counts == 0);   // коммит1 пропущен

    (void)dev_bins1;
    bool stat_fresh2 = spectrum_reset_stat_is_plausible(6, 6);
    CHECK(stat_fresh2);

    uint32_t dev_bins2[3] = {8, 7, 9};   // растёт с прошлого свипа, сумма 24
    CHECK(!spectrum_base_commit_should_defer(24, st.base_counts, st.shown_counts, stat_fresh2));
    bool did_reset = spectrum_base_commit(&st, dev_bins2, 24, 3, stat_fresh2, 6);

    CHECK(!did_reset);          // dev_resets НЕ растёт
    CHECK(st.base_time == 0);   // база НЕ свернулась в старое время 2410
    CHECK(st.base_counts == 0);
    CHECK(st.shown_counts == 24);
}

// D2: STAT застейджен ДО Reset (gen=1), Reset увеличил gen до 2, прибор
// больше НИКОГДА не шлёт STAT (сценарий отчёта). Старая (без gen) функция
// рано или поздно, когда elapsed догонит stat_time, сочла бы STAT
// правдоподобным -- ИСХОДНЫЙ баг отложенно. Новая (gen-aware) обязана
// отклонять его ВСЕГДА, пока не появится STAT текущего gen.
static void test_d2_stale_stat_never_accepted_after_reset(void)
{
    uint32_t stat_time = 100;
    uint32_t stat_gen = 1;
    uint32_t current_gen = 2;
    CHECK(!spectrum_reset_stat_is_plausible_gen(stat_time, 1, stat_gen, current_gen));
    // Демонстрация исходного бага: старая функция БЕЗ gen считает это
    // правдоподобным, когда elapsed догоняет stat_time.
    CHECK(spectrum_reset_stat_is_plausible(stat_time, 96));
    // gen-aware версия — НЕ принимает, ни сразу, ни спустя сколько угодно.
    CHECK(!spectrum_reset_stat_is_plausible_gen(stat_time, 96, stat_gen, current_gen));
    CHECK(!spectrum_reset_stat_is_plausible_gen(stat_time, 1000000, stat_gen, current_gen));
    // Новый свежий STAT (gen совпадает с текущим) -- гейт снова по времени.
    CHECK(spectrum_reset_stat_is_plausible_gen(5, 6, current_gen, current_gen));
    CHECK(!spectrum_reset_stat_is_plausible_gen(2000, 6, current_gen, current_gen));
}

// тест: запуск без сброса — шлюз открыт, статистика принимается
static void test_issue58_boot_without_reset_accepts_stat(void) {
    bool bt = true;
    spectrum_reset_gate_t g = { false, 0, 0 };
    CHECK(spectrum_reset_gate_step(&g, 3000000, 3, 1667686, 1, 0, 0, &bt));
    CHECK(!bt);
    CHECK(spectrum_reset_gate_step(&g, 8000000, 8, 1667691, 2, 0, 0, &bt));
    CHECK(g.reject_since_us == 0);
}

// тест: AWF-4 — устаревшая статистика сразу после сброса отклоняется, правдоподобная принимается
static void test_issue58_awf4_race_still_rejected(void) {
    bool bt = true;
    spectrum_reset_gate_t g = { true, 0, 0 };
    CHECK(!spectrum_reset_gate_step(&g, 1000000, 1, 2410, 5, 1, 1, &bt));
    CHECK(spectrum_reset_gate_step(&g, 6000000, 6, 6, 6, 1, 1, &bt));
    CHECK(!bt);
}

// тест: U1 — статистика старой генерации никогда не принимается и не запускает таймер
static void test_u1_old_gen_never_starts_timer(void) {
    bool bt = true;
    spectrum_reset_gate_t g = { true, 0, 0 };
    CHECK(!spectrum_reset_gate_step(&g, 1000000, 1, 1667686, 9, 2, 3, &bt));
    CHECK(g.reject_since_us == 0);
    CHECK(!spectrum_reset_gate_step(&g, 1000000000, 1000, 1667686, 10, 2, 3, &bt));
    CHECK(g.reject_since_us == 0);
}

// тест: U1 — таймаут принимает только статистику, полученную после первого отклонения
static void test_u1_timeout_needs_new_stat(void) {
    bool bt = true;
    spectrum_reset_gate_t g = { true, 0, 0 };
    CHECK(!spectrum_reset_gate_step(&g, 1000000, 1, 1667686, 7, 3, 3, &bt));
    CHECK(g.reject_since_us == 1000000);
    CHECK(g.reject_first_seq == 7);
    CHECK(!spectrum_reset_gate_step(&g, 10999999, 10, 1667695, 8, 3, 3, &bt));
    CHECK(!spectrum_reset_gate_step(&g, 11000000, 11, 1667686, 7, 3, 3, &bt));
    CHECK(!bt);
    CHECK(spectrum_reset_gate_step(&g, 11000000, 11, 1667696, 8, 3, 3, &bt));
    CHECK(bt);
}

// Н-1.3: STAT старого поколения с правдоподобным временем гейт не принимает (D2)
static void test_n13_gate_old_gen_plausible_rejected(void) {
    bool bt = true;
    spectrum_reset_gate_t g = { true, 0, 0 };
    CHECK(!spectrum_reset_gate_step(&g, 5000000, 5, 3, 4, 2, 3, &bt));
    CHECK(!bt);
    CHECK(g.reject_since_us == 0);
}

// Н-1.2: каждый STAT получает новый seq, gen и session на момент постановки
static void test_n12_stat_tag_stamp(void) {
    spectrum_stat_tag_t t = { false, 0, 0, 0 };
    spectrum_stat_tag_stamp(&t, 3, 7);
    CHECK(t.fresh && t.gen == 3 && t.session == 7 && t.seq == 1);
    spectrum_stat_tag_stamp(&t, 4, 7);
    CHECK(t.seq == 2 && t.gen == 4);
}

// Н-1.1: STAT прошлого сеанса USB и погашенный STAT в коммите не участвуют
static void test_n11_stat_tag_session(void) {
    spectrum_stat_tag_t t = { false, 0, 0, 0 };
    spectrum_stat_tag_stamp(&t, 1, 5);
    CHECK(spectrum_stat_tag_usable(&t, 5));
    CHECK(!spectrum_stat_tag_usable(&t, 6));
    t.fresh = false;
    CHECK(!spectrum_stat_tag_usable(&t, 5));
}

// Н-1.2: публикация снимает гейт; перенос опоры — только без подтверждения
static void test_n12_gate_on_publish(void) {
    spectrum_reset_gate_t g = { true, 123, 9 };
    CHECK(spectrum_reset_gate_on_publish(&g, true, false));
    CHECK(!g.armed && g.reject_since_us == 0);
    g.armed = true;
    CHECK(!spectrum_reset_gate_on_publish(&g, true, true));
    CHECK(!spectrum_reset_gate_on_publish(&g, false, false));
}

// Н-Д1: отложенный -rst — только пока тот же сброс не выполнен
static void test_nd1_reset_pending_valid(void) {
    spectrum_reset_gate_t g = { true, 0, 0 };
    CHECK(spectrum_reset_pending_valid(&g, 5, 5));
    CHECK(!spectrum_reset_pending_valid(&g, 5, 6));
    g.armed = false;
    CHECK(!spectrum_reset_pending_valid(&g, 5, 5));
}

// issue #58: старт без current.bin — первый коммит публикуется, не откладывается
static void test_issue58_boot_commit_publishes(void) {
    uint32_t base_bins[3] = {0,0,0};
    uint32_t shown_bins[3] = {0,0,0};
    spectrum_base_state_t st = { base_bins, 0, 0, shown_bins, 0, 0 };
    uint32_t dev_bins[3] = {400, 300, 300};
    CHECK(!spectrum_base_commit_should_defer(1000, st.base_counts, st.shown_counts, true));
    bool did = spectrum_base_commit(&st, dev_bins, 1000, 3, true, 1667686);
    CHECK(!did);
    CHECK(st.base_counts == 0);
    CHECK(st.shown_counts == 1000);
    CHECK(shown_bins[0] == 400 && shown_bins[1] == 300 && shown_bins[2] == 300);
}

void spectrum_base_plan_suite(void)
{
    test_reset_detection();
    test_merge();
    test_merge_overflow();
    test_counts_reset();
    test_sequence();
    test_live_bug_no_stat_first_commit();
    test_time_only_regression_no_fold();
    test_n2_count_grew_time_catches();
    test_r1_defer_predicate();
    test_r1_defer_and_two_commit_sequence();
    test_awf4_reset_stat_race();
    test_d2_stale_stat_never_accepted_after_reset();
    test_issue58_boot_without_reset_accepts_stat();
    test_issue58_boot_commit_publishes();
    test_issue58_awf4_race_still_rejected();
    test_u1_old_gen_never_starts_timer();
    test_u1_timeout_needs_new_stat();
    test_n13_gate_old_gen_plausible_rejected();
    test_n12_stat_tag_stamp();
    test_n11_stat_tag_session();
    test_n12_gate_on_publish();
    test_nd1_reset_pending_valid();
}
