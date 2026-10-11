#pragma once
/* З-5 (release-gate-1.2.36-code-pass1) + З-1 (pass2): разбор ?w= запроса meta.json (host-pure).
 * Совместимость с 3c73979 (pass2 З-1): запрос без w ЛЮБОЙ длины и w, не являющееся целым из десятичных цифр
 * ("1e+21" от главной при огромном интервале, "-5", "12.5", пусто) - прежний meta.json без SMA (200, байты те же);
 * w из одних цифр - SMA, больше UINT32_MAX - насыщение до UINT32_MAX (дальше monitor_window клампит к ёмкости кольца).
 * Отказ один - META_W_CUT (414 в meta_w_handler.h): строка запроса длиннее буфера META_Q_MAX-1 (q обрезана, trunc != 0)
 * и именно w в него не поместилось - цифры значения доходят до места обрезки, либо обрезка прошла по ключу ("...&w").
 * Ключа w в уместившейся части обрезанной строки нет - META_W_NONE (как в 3c73979: дальше буфера не смотрели). */
#include <stddef.h>
#include <stdint.h>

#define META_Q_MAX 128   /* буфер строки запроса meta.json (с NUL) */

typedef enum { META_W_NONE = 0, META_W_OK = 1, META_W_BAD = 2, META_W_CUT = 3 } meta_w_t;

/* q - строка запроса без '?', с NUL (возможно обрезанная: trunc != 0); ключ "w" - в начале строки или после '&' (первое вхождение). */

static inline meta_w_t meta_w_parse(const char *q, int trunc, uint32_t *w)
{
    const char *p = q;
    for (; *p; p++) {
        if ((p == q || p[-1] == '&') && p[0] == 'w' && p[1] == '=') {
            const char *v = p + 2;
            uint64_t acc = 0;
            int digits = 1;
            for (; *v && *v != '&'; v++) {
                if (*v < '0' || *v > '9') { digits = 0; continue; }
                acc = acc * 10u + (uint64_t)(*v - '0');
                if (acc > UINT32_MAX) acc = (uint64_t)UINT32_MAX + 1u;
            }
            if (!digits) return META_W_BAD;                    /* not an integer: meta without SMA */
            if (*v == '\0' && trunc) return META_W_CUT;        /* the value reaches the cut: w does not fit the buffer */
            if (v == p + 2) return META_W_BAD;                 /* empty value */
            *w = acc > UINT32_MAX ? UINT32_MAX : (uint32_t)acc;
            return META_W_OK;
        }
    }
    /* the cut went through the key itself: "...&w" or a lone "w" at the very end */
    if (trunc && p > q && p[-1] == 'w' && (p - 1 == q || p[-2] == '&')) return META_W_CUT;
    return META_W_NONE;
}
