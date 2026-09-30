#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>       /* #AWF-12b F6: isfinite() — NaN/Inf в дампе прибора */
#include "acq_watch.h"  /* #AWF-12b O4: ACQ_WATCH_MS для _Static_assert ниже */

// #AWF-12 (оператор, дословно): «Если калибровка не задана (все нули), при
// старте набора, как вручную, так и автоматического, принудительно считывать
// калибровку с прибора. Если она считалась успешно, то применить её
// автоматом соответственно». Чистые хелперы-предикаты этого требования —
// без ESP-IDF, тестируются на хосте (tests/host/test_calib_autoread.c).

// «Калибровка не задана» = невалидна ИЛИ все коэффициенты — точный double
// 0.0. Прибор шлёт коэффициент как сырое memcpy-битовое представление double
// (spectrum.c: spectrum_process_info_response) — 0.0 «мусором»/приближением
// не бывает, только ровно ноль или содержательное значение. calib_order
// здесь НЕ участвует: order=0 с валидным c0!=0.0 — тоже «задана».
static inline bool calib_coeffs_all_zero(const double *coeffs, int n)
{
    for (int i = 0; i < n; i++)
        if (coeffs[i] != 0.0) return false;
    return true;
}

// #AWF-12b F6 (release-gate-1.2.28-code.md): NaN/Inf в коэффициенте — тоже
// "не задана"/"не успех". Достижимо только NaN с согласованным CRC в дампе
// прибора (не наблюдалось живьём) либо файлом calib.bin, записанным до этой
// проверки; cJSON отдаёт NaN как null (cJSON.c), JS видит null!==0 → «задана»
// при нечитаемой энергии — без этой проверки предикаты того не замечают.
static inline bool calib_coeffs_any_nonfinite(const double *coeffs, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(coeffs[i])) return true;
    return false;
}

// Раздел 5 (release-gate-firmware-v1.2.28-code.md, непокрытое): #AWF-12b F13
// (spectrum.c spectrum_load_calibration) — calib_order из файла ВНЕ [0,n) дал
// бы чтение за границей массива coeffs[] у всех потребителей ниже по коду.
// Было инлайн-условием без теста; вынесено сюда как пара с проверкой файла.
static inline bool calib_order_in_range(int order, int n)
{
    return order >= 0 && order < n;
}

static inline bool calib_is_missing(const double *coeffs, int n, bool calib_valid)
{
    if (!calib_valid) return true;
    if (calib_coeffs_any_nonfinite(coeffs, n)) return true;
    return calib_coeffs_all_zero(coeffs, n);
}

// «Считалась успешно» (п.4 ТЗ): CRC регистров L[0..9]==L[10] совпал И не все
// коэффициенты нулевые. CRC-валидный, но нулевой дамп прибора — НЕ успех:
// оператор приравнял нулевую калибровку к «не задана» по определению, значит
// такой дамп не должен перезаписывать текущую калибровку платы (ручное
// «Считать» — тоже, это желаемое изменение поведения, не только авто-путь).
static inline bool calib_read_is_success(bool crc_ok, const double *coeffs, int n)
{
    if (!crc_ok) return false;
    if (calib_coeffs_any_nonfinite(coeffs, n)) return false;  // F6
    return !calib_coeffs_all_zero(coeffs, n);
}

// Команда — старт набора «-sta», с параметрами или без (PROTOCOL.md:39-40:
// «-sta [xx] [-r] [-s]»). Тот же приём префиксного разбора с обрезкой ТОЛЬКО
// хвостовых пробельных (ведущие не обрезаются), что cmd_is_device_reset
// (main/acq_intent.h) — единообразно с ним, включая табы.
static inline bool cmd_is_acq_start(const char *cmd)
{
    size_t n = strlen(cmd);
    while (n > 0 && (cmd[n - 1] == ' ' || cmd[n - 1] == '\t' ||
                     cmd[n - 1] == '\r' || cmd[n - 1] == '\n'))
        n--;
    if (n < 4 || strncmp(cmd, "-sta", 4) != 0) return false;
    return n == 4 || cmd[4] == ' ' || cmd[4] == '\t';
}

// Гейт «слать ли -cal перед этим -sta» — двойной барьер против спама (п.3
// ТЗ). acq-watch (main/usb_host_cdc.c, acq_watch_resend_due) повторяет
// "-sta" каждые ACQ_WATCH_MS=20000 мс (acq_watch.h), пока прибор молчит —
// это НЕ новый старт, а повтор уже идущего.
//
// (1) ПЕРЕХОД намерения: prev_was_run=false — намерение ДО этой команды не
//     было RUN, то есть это настоящий старт (ручной или автоматический), а
//     не повтор сторожа. acq_watch_resend_due отправляет "-sta" ТОЛЬКО когда
//     intent УЖЕ RUN (acq_watch.h: `if (intent != ACQ_INTENT_RUN) return
//     false;`) — на повторе prev_was_run будет true, гейт закрыт по (1)
//     одному, без обращения ко времени.
// (2) Кулдаун: now_ms-last_request_ms >= COOLDOWN_MS — независимый барьер на
//     случай, если намерение дребезжит (STOP/UNKNOWN<->RUN) быстрее, чем
//     реальный интервал между стартами (напр. повторные ручные "Старт" с
//     разными параметрами таймера, которые acq_intent_for_cmd НЕ переводит в
//     RUN и потому (1) их не фильтрует).
// Порог 30000 мс выбран так: (а) на порядок больше человеческого повторного
// клика/ретрая (единицы секунд) — не даёт дребезгу пробить лимит; (б) больше
// ACQ_WATCH_MS=20000 — один полный цикл резерва сторожа укладывается внутрь
// кулдауна целиком, даже если бы барьер (1) почему-то не сработал (двойная
// защита, не полагается на один механизм).
#define CALIB_AUTOREAD_COOLDOWN_MS 30000u
// #AWF-12b O4 (release-gate-1.2.28-code.md): закрепляет довод (б) выше
// компилятором, а не только комментарием — правка одного порога без другого
// молча ломает предположение "один цикл сторожа укладывается в кулдаун".
_Static_assert(CALIB_AUTOREAD_COOLDOWN_MS > ACQ_WATCH_MS,
               "calib autoread cooldown must exceed one acq-watch cycle");

// BUG-AS-03: -cal перед стартом нужен и при заданной калибровке, пока пуст
// серийник (он есть только в полном дампе -cal).
static inline bool calib_autoread_needed(bool calib_missing, bool serial_missing)
{
    return calib_missing || serial_missing;
}

// Н-1 (release-gate 1.2.29): авто-«-cal» ради одного серийника (калибровка на
// плате задана — возможно вручную, прибору она не передаётся) коэффициенты
// прибора НЕ применяет; ручное «Считать» и запрос при пустой калибровке — да.
static inline bool calib_apply_coeffs(bool read_success, bool serial_only_request)
{
    return read_success && !serial_only_request;
}

static inline bool calib_autoread_should_request(bool prev_was_run, uint32_t now_ms,
                                                   uint32_t last_request_ms)
{
    if (prev_was_run) return false;
    // last_request_ms==0 — ещё не запрашивали в этой сессии платы; разность
    // в uint32 (переполнение millis) безопасна тем же приёмом, что
    // http_activity_quiet/wifi_return_backoff_elapsed.
    if (last_request_ms != 0 && (now_ms - last_request_ms) < CALIB_AUTOREAD_COOLDOWN_MS)
        return false;
    return true;
}
