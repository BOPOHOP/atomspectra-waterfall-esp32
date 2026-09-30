// AWF-3: слияние «базы» (спектр до последнего сброса анализатора) с текущей
// накопительной гистограммой самого анализатора. Свободен от ESP-IDF.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Допуск на дрожание времени STAT — тот же порог, что и раньше использовался
// для «протухшего STAT» (#FW-12: «откат >=5с — рестарт прибора»).
#define SPECTRUM_BASE_RESET_TOLERANCE_S 5u

// true, если время НОВОГО STAT прибора явно МЕНЬШЕ времени, которое прибор
// уже должен был набрать с последнего сохранённого состояния (shown-base),
// с учётом допуска. shown_time_sec >= base_time_sec (инвариант shown=base+dev).
static inline bool spectrum_base_reset_detected(uint32_t dev_time_now,
                                                 uint32_t base_time_sec,
                                                 uint32_t shown_time_sec)
{
    uint32_t dev_elapsed_since_base = shown_time_sec - base_time_sec;
    if (dev_time_now + SPECTRUM_BASE_RESET_TOLERANCE_S >= dev_elapsed_since_base)
        return false;
    return true;
}

// P1-b: сброс ещё и по СЧЁТУ, независимо от STAT — гистограмма накопительная
// (монотонна внутри сессии прибора), свежий свип меньше, чем прибор уже
// должен был набрать с последнего fold (shown-база), — тоже верный признак
// рестарта, даже если STAT на этом коммите не пришёл вообще. Допуск 0: в
// отличие от времени (STAT дрожит по протоколу), сумма гистограммы — точное
// число, легитимной просадки без сброса не бывает.
#define SPECTRUM_BASE_RESET_TOLERANCE_COUNTS 0u

static inline bool spectrum_base_reset_detected_by_counts(uint32_t dev_total_now,
                                                           uint32_t base_counts,
                                                           uint32_t shown_total_counts)
{
    uint32_t dev_expected_since_base = shown_total_counts - base_counts;
    if (dev_total_now + SPECTRUM_BASE_RESET_TOLERANCE_COUNTS >= dev_expected_since_base)
        return false;
    return true;
}

// P2 (ревью 97b71d0..6426bb8): показываемое время/счётчик = база + прибор,
// НАСЫЩАЮЩЕЕ сложение (один оператор — total, bins[], время). База копится
// через рестарты платы годами — без насыщения переполнение uint32
// (~4.3×10^9) обернулось бы отрицательным/крошечным показанным значением
// молча. sum<base — верный признак переполнения (сумма меньше слагаемого).
static inline uint32_t spectrum_base_merge(uint32_t base, uint32_t dev)
{
    uint32_t sum = base + dev;
    if (sum < base) return UINT32_MAX;
    return sum;
}

// P1-b orchestration (живой тест 25.09: первый коммит после рестарта прибора
// слил dev в bins ДО проверки сброса, если STAT ещё не пришёл свежим на этом
// коммите, — база получила уже склеенное 0+крошечный_dev вместо старого
// показанного спектра). Порядок зафиксирован ЭТОЙ функцией, не комментарием:
// единственное место, которое трогает base_bins/shown_bins.
typedef struct {
    uint32_t *base_bins;      // [n], база (мутируется при сворачивании)
    uint32_t  base_time;
    uint32_t  base_counts;
    uint32_t *shown_bins;     // [n], ПОКАЗЫВАЕМЫЙ спектр (мутируется = base+dev)
    uint32_t  shown_time;     // только читается; время пишет caller (#FW-12 отдельно)
    uint32_t  shown_counts;
} spectrum_base_state_t;

// F3 (итоговое ревью 25.09): свёртка ТОЛЬКО по счёту. Раньше просадка ОДНОГО
// времени (при согласованном счёте) тоже триггерила fold — a base:=shown
// (в shown УЖЕ есть dev), новый shown=base+dev=старая_база+2·dev: удвоение.
// Просадка STAT при согласованном счёте — пересинхронизация времени (было
// до ветки: «принимаем абсолют»), её отдельно делает caller (spectrum.c
// commit_apply_time_stat_fresh_locked), fold она больше не триггерит.
// N2 (ревью-2, находка по F1): count-only свёртка не ловит НАСТОЯЩИЙ сброс,
// если новый свип успел набрать БОЛЬШЕ старого (фон 10ч=100000 → сброс →
// горячий источник 300с=200000: count-проверка видит «выросло», не «упало»).
// Второй, ОГРАНИЧЕННЫЙ сигнал по времени — не просадка >5с (это баг F3:
// STAT-дрожание/пересинхронизация тоже просаживается на секунды при
// согласованном счёте), а КАЧЕСТВЕННЫЙ обвал: dev_time_now < половины
// времени, накопленного с последнего fold. Реальный рестарт роняет время
// почти до нуля; дрожание протокола на секунды от многочасового
// dev_elapsed половину не пересекает. Звать ТОЛЬКО когда count уже сказал
// «не сброс» — иначе время само по себе заново открыло бы баг F3.
static inline bool spectrum_base_reset_detected_bounded(uint32_t dev_time_now,
                                                         uint32_t base_time_sec,
                                                         uint32_t shown_time_sec)
{
    uint32_t dev_elapsed_since_base = shown_time_sec - base_time_sec;
    if (!spectrum_base_reset_detected(dev_time_now, base_time_sec, shown_time_sec))
        return false;
    return dev_time_now < dev_elapsed_since_base / 2;
}

// R1 (ревью-3): коммит БЕЗ свежего STAT, чей count НЕ подтверждает сброс,
// двусмысленный — возможен настоящий сброс с горячим источником (свежий
// dev_total уже БОЛЬШЕ старого expected), куда N2 без времени не
// дотягивается (только count-check выше видит только ПРОСАДКУ, не рост).
// caller обязан ПРОПУСТИТЬ публикацию такого коммита целиком (не звать
// spectrum_base_commit) — следующий коммит либо явно увидит просадку по
// счёту, либо к нему подоспеет STAT и сработает N2 (бонус: от НЕПОВРЕЖДЁННОГО
// base/shown, не от уже слитого этим двусмысленным коммитом — F3-удвоение).
static inline bool spectrum_base_commit_should_defer(uint32_t dev_total_now,
                                                      uint32_t base_counts,
                                                      uint32_t shown_total_counts,
                                                      bool stat_fresh)
{
    if (stat_fresh) return false;
    return !spectrum_base_reset_detected_by_counts(dev_total_now, base_counts, shown_total_counts);
}

// stat_fresh/dev_time_now — время участвует в решении ЧЕРЕЗ ограниченный
// сигнал (см. ниже), только когда stat_fresh (несвежий STAT не значит ничего).
static inline bool spectrum_base_commit(spectrum_base_state_t *st, const uint32_t *dev_bins,
                                        uint32_t dev_total, size_t n,
                                        bool stat_fresh, uint32_t dev_time_now)
{
    bool reset = spectrum_base_reset_detected_by_counts(dev_total, st->base_counts, st->shown_counts);
    if (!reset && stat_fresh)
        reset = spectrum_base_reset_detected_bounded(dev_time_now, st->base_time, st->shown_time);
    if (reset) {
        for (size_t i = 0; i < n; i++) st->base_bins[i] = st->shown_bins[i];
        st->base_time = st->shown_time;
        st->base_counts = st->shown_counts;
    }
    for (size_t i = 0; i < n; i++)
        st->shown_bins[i] = spectrum_base_merge(st->base_bins[i], dev_bins[i]);
    st->shown_counts = spectrum_base_merge(st->base_counts, dev_total);
    return reset;
}

// AWF-4 (живой баг 27.09): Reset -> время продолжает от старого набора.
// Первый коммит после Reset (valid=false) не имеет prev/expected для сверки
// (#FW-12 клэмп в commit_apply_time_stat_fresh_locked работает только при
// valid=true) — застейдженный STAT старого набора, переживший Reset (обычный
// порядок ИЛИ гонка: STAT ушёл ПОСЛЕ spectrum_reset(), но ДО -rst на приборе),
// принимался абсолютом. Правило: правдоподобное время STAT на первом
// коммите после Reset не может превышать реально прошедшее с Reset (elapsed)
// + допуск на джиттер (тот же SPECTRUM_BASE_RESET_TOLERANCE_S).
static inline bool spectrum_reset_stat_is_plausible(uint32_t stat_time_sec,
                                                      uint32_t elapsed_since_reset_sec)
{
    return stat_time_sec <= elapsed_since_reset_sec + SPECTRUM_BASE_RESET_TOLERANCE_S;
}

// D2 (2026-09-27, P2, verify-awf4-2026-09-27.md разд.5): чисто временной
// гейт выше СЛЕП к тому, что STAT больше НЕ ОБНОВЛЯЕТСЯ после Reset —
// если старый застейдженный STAT переживает Reset (fresh не сбрасывается
// Reset'ом, main/spectrum.c), elapsed_since_reset РАСТЁТ и через
// stat_time_sec-5 секунд неравенство станет истинным — старый STAT будет
// принят как "правдоподобный", воспроизводя исходный баг отложенно.
// Правило: STAT несёт номер generation (s_reset_gen на момент постановки
// fresh=true, main/spectrum.c). Если Reset случился ПОСЛЕ постановки (gen
// STAT'а старше текущего) — STAT принадлежит уже сброшенному набору и не
// может стать правдоподобным НИКОГДА, независимо от elapsed.
static inline bool spectrum_reset_stat_is_plausible_gen(uint32_t stat_time_sec,
                                                          uint32_t elapsed_since_reset_sec,
                                                          uint32_t stat_stage_gen,
                                                          uint32_t current_reset_gen)
{
    if (stat_stage_gen != current_reset_gen) return false;
    return spectrum_reset_stat_is_plausible(stat_time_sec, elapsed_since_reset_sec);
}

// issue #58: гейт выше — только после явного Reset в этой загрузке (armed), и
// не вечно: STAT текущего gen неправдоподобен SPECTRUM_RESET_CONFIRM_TIMEOUT_S —
// прибор -rst не выполнил. У-1 (release-gate 1.2.29): таймер запускает только
// STAT текущего gen; по таймауту принимается только STAT, пришедший ПОСЛЕ
// первого отказа (другой seq) — отклонённый пакет не расходуется и иначе сам
// себя принял бы через 10 с (например, залежавшись на переподключении USB).
#define SPECTRUM_RESET_CONFIRM_TIMEOUT_S 10u

typedef struct {
    bool     armed;             // Reset был, первый коммит после него не прошёл
    int64_t  reject_since_us;   // 0 = серии отказов текущего gen нет
    uint32_t reject_first_seq;  // номер STAT на первом отказе серии
} spectrum_reset_gate_t;

// true = STAT принять; *by_timeout = принят без подтверждения сброса прибором.
static inline bool spectrum_reset_gate_step(spectrum_reset_gate_t *g, int64_t now_us,
                                            uint32_t elapsed_since_reset_sec, uint32_t stat_time_sec,
                                            uint32_t stat_seq, uint32_t stat_gen,
                                            uint32_t current_gen, bool *by_timeout)
{
    *by_timeout = false;

    if (!g->armed) {
        return true;
    }

    if (spectrum_reset_stat_is_plausible_gen(stat_time_sec, elapsed_since_reset_sec, stat_gen, current_gen)) {
        return true;
    }

    if (stat_gen != current_gen) {
        return false;
    }

    if (g->reject_since_us == 0) {
        g->reject_since_us = now_us;
        g->reject_first_seq = stat_seq;
        return false;
    }

    if ((now_us - g->reject_since_us) / 1000000 < SPECTRUM_RESET_CONFIRM_TIMEOUT_S) {
        return false;
    }

    if (stat_seq == g->reject_first_seq) {
        return false;
    }

    *by_timeout = true;
    return true;
}

// Н-1.1/Н-1.2 (release-gate 1.2.29): метка STAT в staging. seq отличает новый
// пакет от отклонённого (У-1), gen — поколение сброса (D2), session — сеанс USB:
// STAT, поставленный до отключения прибора, в коммите нового сеанса не участвует
// (иначе второй залежавшийся STAT принимался по таймауту, время оседало в базе).
typedef struct {
    bool     fresh;
    uint32_t gen;
    uint32_t seq;
    uint32_t session;
} spectrum_stat_tag_t;

static inline void spectrum_stat_tag_stamp(spectrum_stat_tag_t *t, uint32_t gen, uint32_t session)
{
    t->fresh = true;
    t->gen = gen;
    t->session = session;
    t->seq++;
}

static inline bool spectrum_stat_tag_usable(const spectrum_stat_tag_t *t, uint32_t session)
{
    return t->fresh && t->session == session;
}

// Публикация коммита снимает гейт. true — первая публикация после valid=false
// без подтверждённого сброса: водопад и монитор переносят опору без строки (У-3).
static inline bool spectrum_reset_gate_on_publish(spectrum_reset_gate_t *g, bool first_valid,
                                                  bool reset_confirmed)
{
    g->armed = false;
    g->reject_since_us = 0;
    return first_valid && !reset_confirmed;
}

// Н-Д1 (release-gate 1.2.29): отложенный -rst досылается, только пока ТОТ ЖЕ сброс не
// выполнен: поколение не сменилось (новый Сброс, дошедший -rst) и гейт взведён (нет
// публикации — ни подтверждённой, ни по таймауту, когда набор прибора уже показан).
static inline bool spectrum_reset_pending_valid(const spectrum_reset_gate_t *g, uint32_t pending_gen,
                                                uint32_t current_gen)
{
    return g->armed && pending_gen == current_gen;
}
