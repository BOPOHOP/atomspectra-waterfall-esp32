#pragma once
#include <stdbool.h>
#include <stdint.h>
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

/* Дамп накоплен целиком: начинается как дамп (позиция 0) И содержит >=39
 * переводов строки (39-я строка — серийник, spectrum.c). Перенесена из
 * usb_host_cdc.c (была is_complete_cal) без изменения семантики. */
static inline bool text_accum_is_complete_cal(const char *s, int len)
{
    if (!text_looks_like_cal_dump_start(s, len)) return false;
    int nl = 0;
    for (const char *q = s; *q; q++)
        if (*q == '\n') nl++;
    return nl >= 39;
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

    int sp = cap - *accum_len - 1;
    if (sp < 0) sp = 0;
    int cp = pkt_len < sp ? pkt_len : sp;
    if (cp > 0) memcpy(accum + *accum_len, pkt, (size_t)cp);
    *accum_len += cp;
    accum[*accum_len] = '\0';

    if (text_accum_is_complete_cal(accum, *accum_len))       return TEXT_ACCUM_CAL;
    if (text_accum_is_complete_inf(accum))                   return TEXT_ACCUM_INF;
    if (text_accum_is_complete_tcpot(accum, *accum_len))     return TEXT_ACCUM_TCPOT;
    if (text_accum_is_complete_short_ack(accum, *accum_len)) { *accum_len = 0; return TEXT_ACCUM_SHORT_ACK; }
    if (*accum_len >= cap - 128)                             { *accum_len = 0; return TEXT_ACCUM_OVERFLOW; }
    return TEXT_ACCUM_NONE;
}
