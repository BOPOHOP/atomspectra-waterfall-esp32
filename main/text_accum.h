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
    if (text_accum_is_clean_cal_prefix(accum, accum_len)) return false;
    return text_looks_like_cal_dump_start(pkt, pkt_len);
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
    if (text_accum_should_reset_before_pkt(accum, *accum_len, pkt, pkt_len))
        *accum_len = 0;

    int before = *accum_len;   /* длина до этого пакета (после возможного сброса) */
    int sp = cap - *accum_len - 1;
    if (sp < 0) sp = 0;
    int cp = pkt_len < sp ? pkt_len : sp;
    /* R3 (release-gate-1.2.28-code-rc2.md §2.1): \0 внутри пакета прибора
     * копируется как есть, но strstr/strlen ниже читают accum как C-строку
     * и останавливаются на первом \0 — всё, что после него в ЭТОМ ЖЕ
     * пакете, становится невидимым триггерам (-inf/Tcpot/-ok). Заменяем
     * \0 на пробел на копировании — байты и их порядок целы. */
    for (int i = 0; i < cp; i++) {
        char c = pkt[i];
        accum[*accum_len + i] = (c == '\0') ? ' ' : c;
    }
    *accum_len += cp;
    accum[*accum_len] = '\0';

    /* Н1/Н2 раунда 2 — калибровка применяется, как в 1.2.27, по первому кадру с CRC-окном (событие CAL_COEFFS, без сброса), серийник — когда пришёл весь дамп (CAL); потерянный второй кадр больше не откладывает калибровку и не смешивает остаток дампа с -inf. */
    int woff = text_accum_find_last_cal_window(accum, *accum_len);
    if (woff >= 0) {
        if (text_accum_cal_dump_end(accum, *accum_len, woff) >= 0) return TEXT_ACCUM_CAL;
        if (woff + 110 > before) return TEXT_ACCUM_CAL_COEFFS;   /* окно впервые целиком — один раз на окно */
    } else if (text_accum_is_complete_cal_positional(accum, *accum_len)) {
        return TEXT_ACCUM_CAL;
    }

    if (text_accum_is_complete_inf(accum))                   return TEXT_ACCUM_INF;
    if (text_accum_is_complete_tcpot(accum, *accum_len))     return TEXT_ACCUM_TCPOT;
    if (text_accum_is_complete_short_ack(accum, *accum_len)) { *accum_len = 0; return TEXT_ACCUM_SHORT_ACK; }
    if (*accum_len >= cap - 128)                             { *accum_len = 0; return TEXT_ACCUM_OVERFLOW; }
    return TEXT_ACCUM_NONE;
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
