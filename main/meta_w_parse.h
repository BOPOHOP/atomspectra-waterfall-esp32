#pragma once
/* З-5 (release-gate-1.2.36-code-pass1): разбор ?w= запроса meta.json (host-pure). Раньше строка длиннее q[32] молча отключала SMA,
 * а strtoul принимал "-5" и пробелы. Теперь поведение определено: строка запроса длиннее META_Q_MAX-1 -> 414 (web_server.c);
 * ключа w нет -> META_W_NONE (прежний meta.json, байты те же); значение не из одних десятичных цифр (пусто, знак, пробел,
 * дробь, экспонента) -> META_W_BAD (400); больше UINT32_MAX -> насыщение до UINT32_MAX (дальше monitor_window клампит). */
#include <stddef.h>
#include <stdint.h>

#define META_Q_MAX 128   /* буфер строки запроса meta.json (с NUL) */

typedef enum { META_W_NONE = 0, META_W_OK = 1, META_W_BAD = 2 } meta_w_t;

/* q - строка запроса без '?', с NUL; ключ "w" - в начале строки или после '&' (первое вхождение). */
static inline meta_w_t meta_w_parse(const char *q, uint32_t *w)
{
    for (const char *p = q; *p; p++) {
        if ((p == q || p[-1] == '&') && p[0] == 'w' && p[1] == '=') {
            const char *v = p + 2;
            uint64_t acc = 0;
            if (*v < '0' || *v > '9') return META_W_BAD;
            for (; *v && *v != '&'; v++) {
                if (*v < '0' || *v > '9') return META_W_BAD;
                acc = acc * 10u + (uint64_t)(*v - '0');
                if (acc > UINT32_MAX) acc = (uint64_t)UINT32_MAX + 1u;
            }
            *w = acc > UINT32_MAX ? UINT32_MAX : (uint32_t)acc;
            return META_W_OK;
        }
    }
    return META_W_NONE;
}
