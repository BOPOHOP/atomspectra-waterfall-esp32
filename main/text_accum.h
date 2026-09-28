#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* #AWF-12b (release-gate-1.2.28-code.md, F1): чистая логика накопителя
 * текстовых ответов прибора (usb_host_cdc.c: s_text_accum, handle_rx_packet
 * CMD_TEXT). Без ESP-IDF — тестируется на хосте
 * (tests/host/test_text_accum.c). Разбор бага и приёма исправления — см.
 * release-gate-1.2.28-code.md раздел F1 и SESSION-STATE/отчёт #AWF-12b. */

/* 0-9 / A-F / a-f. */
static inline bool text_is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/* Формат дампа -cal подтверждён на приборе: 40 строк ровно по 8 hex-символов,
 * разделённых \r\n (Text(400) = 40*10 байт, usb_host_cdc.c #CMD-1). "Похоже
 * на начало дампа" = первые 8 байт буфера — hex, 9-й — конец строки (\r или
 * \n); буфер короче 9 байт дампом начинаться не может. */
static inline bool text_looks_like_cal_dump_start(const char *s, int len)
{
    if (len < 9) return false;
    for (int i = 0; i < 8; i++)
        if (!text_is_hex_digit(s[i])) return false;
    return s[8] == '\r' || s[8] == '\n';
}

/* "Чистый префикс дампа": длина кратна 10 (8 hex + \r\n на строку) И каждая
 * 10-байтная строка целиком совпадает с этим форматом. Пустой аккумулятор —
 * тоже чистый префикс (0 строк). Отличает легитимное продолжение дампа,
 * раздробленного на несколько кадров ровно по границе строки, от
 * постороннего мусора перед НОВЫМ дампом (нужно для F1 приёма 1). */
static inline bool text_accum_is_clean_cal_prefix(const char *s, int len)
{
    if (len < 0 || (len % 10) != 0) return false;
    for (int off = 0; off < len; off += 10) {
        for (int i = 0; i < 8; i++)
            if (!text_is_hex_digit(s[off + i])) return false;
        if (s[off + 8] != '\r' || s[off + 9] != '\n') return false;
    }
    return true;
}

/* Строка формата дампа: 8 hex + \r\n (10 байт). Тот же формат несёт и
 * строка CRC следом за 10 строками коэффициентов — подтверждено боевым
 * логом (.logs/cal_capture.txt): L[10] "DF786A7E" — та же форма, что
 * L[0..9]. strtoul стоит на первом не-hex байте (\r), 9/10-й байт можно
 * не копировать. */
static inline bool text_accum_hex_line(const char *s, int len, uint32_t *out)
{
    if (len < 10) return false;
    for (int i = 0; i < 8; i++)
        if (!text_is_hex_digit(s[i])) return false;
    if (s[8] != '\r' || s[9] != '\n') return false;
    if (out) *out = (uint32_t)strtoul(s, NULL, 16);
    return true;
}

/* Стандартный CRC32 (init 0xFFFFFFFF, полином 0xEDB88320 рефлексирован,
 * финальный XOR 0xFFFFFFFF) над ASCII-конкатенацией n hex-строк (n*8 байт,
 * без \r\n) — тот же алгоритм, что main/spectrum.c #CMD-1. Проверено на
 * боевом дампе: CRC32(конкатенация L[0..9]) = DF786A7E = L[10]. */
static inline uint32_t text_accum_crc32_lines(const char *s, int nlines)
{
    uint32_t cc = 0xFFFFFFFF;
    for (int i = 0; i < nlines; i++) {
        const char *line = s + i * 10;
        for (int k = 0; k < 8; k++) {
            cc ^= (uint8_t)line[k];
            for (int j = 0; j < 8; j++)
                cc = (cc & 1) ? (cc >> 1) ^ 0xEDB88320 : (cc >> 1);
        }
    }
    return cc ^ 0xFFFFFFFF;
}

/* R2 (release-gate-1.2.28-code-rc2.md §2.1): окно дампа -cal ПО ФОРМАТУ И
 * CRC, не только с позиции 0 — закрывает S10 (однострочный hex-ответ
 * перед дампом), S07 (первый кадр дампа <9 байт), S22 ("-ok"+дамп одним
 * кадром). Скан байт-за-байтом (не только кратно 10) — мусор перед
 * дампом не обязан быть кратен строке. -1, если валидного окна нет. */

/* true, если на смещении off начинается CRC-валидное окно */
static inline bool text_accum_cal_window_at(const char *s, int len, int off)
{
    if (off < 0 || off + 110 > len) return false;
    for (int i = 0; i <= 10; i++) {
        if (!text_accum_hex_line(s + off + i * 10, len - off - i * 10, NULL))
            return false;
    }
    uint32_t ce = 0;
    text_accum_hex_line(s + off + 100, len - off - 100, &ce);
    return text_accum_crc32_lines(s + off, 10) == ce;
}

/* первое off (от 0 вверх), для которого cal_window_at истинно; иначе -1 */
static inline int text_accum_find_cal_window(const char *s, int len)
{
    for (int off = 0; off + 110 <= len; off++) {
        if (text_accum_cal_window_at(s, len, off)) return off;
    }
    return -1;
}

/* последнее такое off (перебор от конца вниз). В аккумуляторе может лежать
 * недособранный прошлый дамп (потерян кадр), за которым пришёл новый; решение
 * принимается по самому свежему дампу, иначе строка 39 от старого окна попадает
 * в середину нового. Ложное окно внутри дампа требует совпадения CRC32. */
static inline int text_accum_find_last_cal_window(const char *s, int len)
{
    for (int off = len - 110; off >= 0; off--) {
        if (text_accum_cal_window_at(s, len, off)) return off;
    }
    return -1;
}

/* строка дампа ПО ФОРМЕ: len >= 10, байты 0..7 не '\r' и не '\n', s[8]=='\r', s[9]=='\n'.
 * \0, заменённый на пробел в text_accum_feed, внутри строк L11..L38 не должен ломать сборку;
 * длинная строка -inf под форму не подходит. */
static inline bool text_accum_dump_shaped_line(const char *s, int len)
{
    if (len < 10) return false;
    for (int i = 0; i < 8; i++) {
        if (s[i] == '\r' || s[i] == '\n') return false;
    }
    return s[8] == '\r' && s[9] == '\n';
}

/* сколько строк формы дампа подряд с начала s, не больше max */
static inline int text_accum_dump_lines(const char *s, int len, int max)
{
    int count = 0;
    while (count < max) {
        if (!text_accum_dump_shaped_line(s + count * 10, len - count * 10))
            break;
        count++;
    }
    return count;
}

/* конец ПОЛНОГО дампа от окна на off: 39 строк формы + 8 байт 40-й строки (серийник);
 * завершающий \r\n 40-й строки не обязателен. Возвращает -1 при ошибках. */
static inline int text_accum_cal_dump_end(const char *s, int len, int off)
{
    if (off < 0 || text_accum_dump_lines(s + off, len - off, 39) < 39) return -1;
    int p = off + 390;
    if (len - p < 8) return -1;
    for (int i = 0; i < 8; i++) {
        if (s[p + i] == '\r' || s[p + i] == '\n') return -1;
    }
    if (len - p > 8 && s[p + 8] != '\r' && s[p + 8] != '\n') return -1;
    return p + 8;
}

/* прежний триггер: если !text_looks_like_cal_dump_start -> false; иначе посчитать '\n' в C-строке.
 * Применяется только когда CRC-окна в буфере нет (дамп без сходящегося CRC), семантика как в 1.2.27. */
static inline bool text_accum_is_complete_cal_positional(const char *s, int len)
{
    if (!text_looks_like_cal_dump_start(s, len)) return false;
    int nl = 0;
    for (const char *q = s; *q; q++)
        if (*q == '\n') nl++;
    return nl >= 39;
}

/* дамп накоплен ЦЕЛИКОМ (калибровка + серийник); раннее применение калибровки по одному окну —
 * отдельное событие TEXT_ACCUM_CAL_COEFFS в text_accum_feed. */
static inline bool text_accum_is_complete_cal(const char *s, int len)
{
    int off = text_accum_find_last_cal_window(s, len);
    if (off >= 0) return text_accum_cal_dump_end(s, len, off) >= 0;
    return text_accum_is_complete_cal_positional(s, len);
}

/* -inf: один Text(404) с параметрами прибора (VERSION..PileUpThr). Подстрока,
 * не якорь на позицию 0 — прежний триггер, устойчив к мусору в начале
 * (usb_host_cdc.c #CMD-1). */
static inline bool text_accum_is_complete_inf(const char *s)
{
    return strstr(s, "PileUpThr ") != NULL && strstr(s, "VERSION ") != NULL;
}

/* Ответ на -tc_pot?: один Text-пакет " Tcpot [...]" (ведущий пробел
 * подтверждён на приборе, devlog seq 18 2026-07-01), завершение — закрывающая
 * скобка. Прежний триггер без изменений. */
static inline bool text_accum_is_complete_tcpot(const char *s, int len)
{
    return len >= 6 && s[len - 1] == ']' && strstr(s, "Tcpot ") != NULL;
}

/* F1, приём (2): короткий "-ok" прибора, завершённый переводом строки (\r?\n),
 * И БОЛЬШЕ НИЧЕГО в аккумуляторе (весь буфер от позиции 0, не подстрока) —
 * подтверждено на приборе после -rst и после -sta (boot_fw23.log:183-190,
 * f5-stress-after.log:52-53). "-ok" не встречается ни как продолжение дампа
 * (тот начинается с hex-цифры, "-" не hex), ни внутри -inf/Tcpot. */
static inline bool text_accum_is_complete_short_ack(const char *s, int len)
{
    if (len < 4 || s[0] != '-' || s[1] != 'o' || s[2] != 'k') return false;
    int i = 3;
    if (s[i] == '\r') i++;
    if (s[i] != '\n') return false;
    return (i + 1) == len;   /* ровно "-ok\r\n" / "-ok\n" целиком, без хвоста */
}

/* F1, приём (1): сбросить аккумулятор ДО добавления пришедшего пакета, если в
 * аккумуляторе уже лежит непереваренный мусор (НЕ чистый префикс дампа), а
 * пакет сам выглядит как начало НОВОГО дампа. */
static inline bool text_accum_should_reset_before_pkt(const char *accum, int accum_len,
                                                        const char *pkt, int pkt_len)
{
    if (accum_len <= 0) return false;
    if (!text_looks_like_cal_dump_start(pkt, pkt_len)) return false;
    if (text_accum_is_clean_cal_prefix(accum, accum_len)) return false;
    /* У2 (раунд 3): после постороннего префикса ("-ok"+дамп одним кадром) лежит CRC-окно,
     * и строки дампа от него идут ровно до конца буфера — пакет может быть продолжением
     * этого дампа, сброс потерял бы окно и серийник. */
    int w = text_accum_find_last_cal_window(accum, accum_len);
    if (w >= 0 && w + 10 * text_accum_dump_lines(accum + w, accum_len - w, 40) == accum_len)
        return false;
    return true;
}

typedef enum {
    TEXT_ACCUM_NONE = 0,      /* пакет поглощён, накопление продолжается */
    TEXT_ACCUM_CAL,           /* дамп -cal накоплен целиком */
    TEXT_ACCUM_INF,           /* -inf накоплен целиком */
    TEXT_ACCUM_TCPOT,         /* ответ на -tc_pot? накоплен целиком */
    TEXT_ACCUM_SHORT_ACK,     /* короткий -ok — поглощён, отброшен без данных */
    TEXT_ACCUM_OVERFLOW,      /* переполнение без триггера — отброшено */
    TEXT_ACCUM_CAL_COEFFS,   /* CRC-окно дампа только что появилось, дамп ещё не весь: применить калибровку, аккумулятор НЕ сбрасывать */
} text_accum_result_t;

#define TEXT_ACCUM_MARKS_CAP 16      /* начала последних пакетов в аккумуляторе */
#define TEXT_ACCUM_QUIET_MS  1000    /* тишина без текстовых пакетов, после которой отложенный дамп разбирается */

/* У1 (раунд 3): начало нового ответа прибора всегда совпадает с началом пакета; по этому
списку отличаем продолжение дампа от нового дампа. Нулевая инициализация — пустой список. */
typedef struct {
    int n;                            /* сколько начал пакетов в at[] */
    int at[TEXT_ACCUM_MARKS_CAP];     /* смещения начал пакетов, по возрастанию */
    int lost_max;                     /* 0 — вытесненных нет; иначе наибольшее вытесненное начало: любая позиция 1..lost_max считается возможным началом */
} text_accum_marks_t;


static inline void text_accum_marks_trim(text_accum_marks_t *m, int len) {
    /* аккумулятор укоротился (сброс) — начала за его концом недействительны */
    int j = 0;
    for (int i = 0; i < m->n; i++) {
        if (m->at[i] < len) m->at[j++] = m->at[i];
    }
    m->n = j;
    if (m->lost_max >= len) m->lost_max = 0;
}

static inline void text_accum_marks_add(text_accum_marks_t *m, int q) {
    if (m->n > 0 && m->at[m->n - 1] == q) return;
    if (m->n == TEXT_ACCUM_MARKS_CAP) {
        if (m->at[0] > m->lost_max) m->lost_max = m->at[0];
        for (int i = 0; i < m->n - 1; i++) m->at[i] = m->at[i + 1];
        m->n--;
    }
    m->at[m->n++] = q;
}

static inline bool text_accum_marks_has(const text_accum_marks_t *m, int q) {
    if (q > 0 && q <= m->lost_max) return true;
    for (int i = 0; i < m->n; i++) {
        if (m->at[i] == q) return true;
    }
    return false;
}

static inline bool text_accum_dump_prefix_shaped(const char *s, int n) {
    int full = n / 10;
    if (text_accum_dump_lines(s, n, full) < full) return false;
    const char *t = s + full * 10;
    int r = n - full * 10;
    for (int i = 0; i < r && i < 8; i++) {
        if (t[i] == '\r' || t[i] == '\n') return false;
    }
    if (r >= 9 && t[8] != '\r') return false;
    return true;
}

/* У1: полный дамп от окна w может оказаться хвостом старого дампа, к которому приклеилось
начало нового (прежний потерял последний кадр). Новый дамп начинается с начала пакета на границе строки;
пока его окно не пришло целиком, серийник брать нельзя. */
static inline bool text_accum_serial_ambiguous(const char *s, int len, int w, const text_accum_marks_t *m, bool final) {
    for (int k = 11; k <= 39; k++) {
        int q = w + 10 * k;
        if (q >= len) break;
        if (!text_accum_marks_has(m, q)) continue;
        int nl = text_accum_dump_lines(s + q, len - q, 10);
        if (nl >= 1 && memcmp(s + q, s + w, (size_t)(10 * nl)) == 0) return true;   /* новый дамп с тем же началом */
        if (!final && q + 110 > len && text_accum_dump_prefix_shaped(s + q, len - q)) return true;   /* ещё может оказаться новым дампом */
    }
    return false;
}

/* решение по уже накопленному буферу; before — длина до последнего пакета; final — прошла тишина
TEXT_ACCUM_QUIET_MS (новых пакетов не будет): решает только дамп. */
static inline text_accum_result_t text_accum_eval(char *accum, int *accum_len, int cap, const text_accum_marks_t *m, int before, bool final) {
    int len = *accum_len;
    int woff = text_accum_find_last_cal_window(accum, len);
    if (woff >= 0) {
        if (text_accum_cal_dump_end(accum, len, woff) >= 0 &&
            !text_accum_serial_ambiguous(accum, len, woff, m, final)) return TEXT_ACCUM_CAL;
        if (woff + 110 > before) return TEXT_ACCUM_CAL_COEFFS;   /* окно впервые целиком — один раз на окно */
    } else if (text_accum_is_complete_cal_positional(accum, len)) {
        return TEXT_ACCUM_CAL;
    }
    if (final) return TEXT_ACCUM_NONE;
    if (text_accum_is_complete_inf(accum))               return TEXT_ACCUM_INF;
    if (text_accum_is_complete_tcpot(accum, len))        return TEXT_ACCUM_TCPOT;
    if (text_accum_is_complete_short_ack(accum, len))    { *accum_len = 0; return TEXT_ACCUM_SHORT_ACK; }
    if (len >= cap - 128)                                { *accum_len = 0; return TEXT_ACCUM_OVERFLOW; }
    return TEXT_ACCUM_NONE;
}

/* роутер с учётом начал пакетов (прошивка держит m между пакетами). */
static inline text_accum_result_t text_accum_feed_m(char *accum, int *accum_len, int cap, text_accum_marks_t *m, const char *pkt, int pkt_len) {
    if (text_accum_should_reset_before_pkt(accum, *accum_len, pkt, pkt_len))
        *accum_len = 0;
    int before = *accum_len;   /* длина до этого пакета (после возможного сброса) */
    text_accum_marks_trim(m, before);
    int sp = cap - *accum_len - 1;
    if (sp < 0) sp = 0;
    int cp = pkt_len < sp ? pkt_len : sp;
    /* \0 внутри пакета — на пробел: strstr/strlen ниже читают accum как C-строку */
    for (int i = 0; i < cp; i++) {
        char c = pkt[i];
        accum[*accum_len + i] = (c == '\0') ? ' ' : c;
    }
    *accum_len += cp;
    accum[*accum_len] = '\0';
    if (cp > 0) text_accum_marks_add(m, before);
    return text_accum_eval(accum, accum_len, cap, m, before, false);
}

/* Роутер накопителя — ВСЯ логика handle_rx_packet(CMD_TEXT) из
 * usb_host_cdc.c, без побочных эффектов (без USB/ESP-IDF/логов). accum —
 * буфер (станет null-terminated), *accum_len — длина на входе/выходе; cap —
 * размер accum (с местом под '\0'). pkt/pkt_len — новый пакет (не обязан
 * быть null-terminated). НЕ сбрасывает *accum_len на CAL/INF/TCPOT — буфер
 * ещё нужен вызывающему (spectrum_process_info_response читает accum), сброс
 * делает вызывающий сам после того, как прочитал результат. На SHORT_ACK/
 * OVERFLOW буфер вызывающему не нужен — сбрасывает сам роутер (RO3/RT3,
 * release-gate-1.2.28-code-rc2.md): это часть контракта, покрытого хост-
 * тестом, а не отдельная копия правила на стороне вызывающего кода. */
static inline text_accum_result_t text_accum_feed(char *accum, int *accum_len, int cap,
                                                    const char *pkt, int pkt_len)
{
    /* У1 (раунд 3): без списка начал пакетов известно лишь начало ЭТОГО пакета —
     * прошивка зовёт text_accum_feed_m со списком, который живёт между пакетами. */
    text_accum_marks_t m = {0};
    return text_accum_feed_m(accum, accum_len, cap, &m, pkt, pkt_len);
}

/* Что отдать разборщику ответа (spectrum_process_info_response / spectrum_process_tcpot_response): полуинтервал [*off, *end) буфера s длиной len. Возвращает false, если разбирать нечего. */
static inline bool text_accum_result_span(text_accum_result_t r, const char *s, int len, int *off, int *end)
{
    if (r == TEXT_ACCUM_CAL) {
        int w = text_accum_find_last_cal_window(s, len);
        if (w >= 0) {
            int e = text_accum_cal_dump_end(s, len, w);
            if (e >= 0) { *off = w; *end = e; return true; }
        }
        /* позиционная ветка, CRC-окна нет */
        *off = 0;
        *end = len;
        return true;
    }
    if (r == TEXT_ACCUM_CAL_COEFFS) {
        int w = text_accum_find_last_cal_window(s, len);
        if (w < 0) return false;
        *off = w;
        *end = w + 10 * text_accum_dump_lines(s + w, len - w, 39);
        return true;
    }
    if (r == TEXT_ACCUM_INF) {
        const char *v = strstr(s, "VERSION ");
        if (!v) return false;
        *off = (int)(v - s);
        *end = len;
        return true;
    }
    if (r == TEXT_ACCUM_TCPOT) {
        const char *v = strstr(s, "Tcpot ");
        if (!v) return false;
        *off = (int)(v - s);
        *end = len;
        return true;
    }
    return false;
}

/* true для TEXT_ACCUM_CAL, TEXT_ACCUM_INF, TEXT_ACCUM_TCPOT (вызывающий сбрасывает аккумулятор после разбора); false для остальных, в том числе TEXT_ACCUM_CAL_COEFFS (остаток дампа ещё придёт). SHORT_ACK/OVERFLOW сбрасывает сама feed. */
static inline bool text_accum_result_consumes(text_accum_result_t r)
{
    return r == TEXT_ACCUM_CAL || r == TEXT_ACCUM_INF || r == TEXT_ACCUM_TCPOT;
}

/* CAL по CRC-окну — до конца дампа и его \r\n, ответ, пришедший следом, остаётся;
остальные поглощающие — весь буфер; прочие — ничего. */
static inline int text_accum_consume_len(text_accum_result_t r, const char *s, int len) {
    if (r == TEXT_ACCUM_CAL) {
        int w = text_accum_find_last_cal_window(s, len);
        int e = w >= 0 ? text_accum_cal_dump_end(s, len, w) : -1;
        if (e < 0) return len;                     /* позиционная ветка — весь буфер, как раньше */
        for (int i = 0; i < 2 && e < len && (s[e] == '\r' || s[e] == '\n'); i++) e++;
        return e;
    }
    return text_accum_result_consumes(r) ? len : 0;
}

static inline void text_accum_consume(char *s, int *len, text_accum_marks_t *m, int k) {
    if (k <= 0) return;
    if (k >= *len) { *len = 0; s[0] = '\0'; m->n = 0; m->lost_max = 0; return; }
    memmove(s, s + k, (size_t)(*len - k));
    *len -= k;
    s[*len] = '\0';
    int j = 0;
    for (int i = 0; i < m->n; i++) {
        int q = m->at[i] - k;
        if (q >= 0) m->at[j++] = q;
    }
    m->n = j;
    m->lost_max = m->lost_max > k ? m->lost_max - k : 0;
}

/* после тишины TEXT_ACCUM_QUIET_MS: отложенный дамп (ждал, не начало ли нового дампа в хвосте) разбирается. */
static inline text_accum_result_t text_accum_flush(char *accum, int *accum_len, int cap, const text_accum_marks_t *m) {
    return text_accum_eval(accum, accum_len, cap, m, *accum_len, true);
}

typedef void (*text_accum_sink_fn)(void *ctx, text_accum_result_t r, const char *text);

/* проводка результата: отрезок text_accum_result_span → sink (на время вызова на конце отрезка '\0',
байт восстанавливается), снятие поглощённого; хвост после CAL разбирается тут же. Возвращает последний результат. */
static inline text_accum_result_t text_accum_dispatch(text_accum_result_t r, char *accum, int *accum_len, int cap, text_accum_marks_t *m, text_accum_sink_fn sink, void *ctx) {
    for (int guard = 0; guard < 8; guard++) {
        int off = 0, end = 0;
        if (text_accum_result_span(r, accum, *accum_len, &off, &end)) {
            char saved = accum[end];
            accum[end] = '\0';
            sink(ctx, r, accum + off);
            accum[end] = saved;
        }
        text_accum_consume(accum, accum_len, m, text_accum_consume_len(r, accum, *accum_len));
        if (r != TEXT_ACCUM_CAL || *accum_len == 0) break;
        r = text_accum_eval(accum, accum_len, cap, m, *accum_len, false);
        if (r == TEXT_ACCUM_NONE) break;
    }
    return r;
}
