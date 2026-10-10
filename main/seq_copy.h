// 1.2.36 H1 (A): снимок под seqlock без мьютекса (host-pure). Писатель: seq++ (нечётный) + барьер,
// запись, барьер + seq++ (чётный) — макросы SPEC_LOCK/SPEC_UNLOCK в spectrum.c. Читатель: копия годна,
// только если seq чётный до копии и не изменился после неё; иначе повтор, после tries — false (запасной путь).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool seq_copy(const volatile uint32_t *seq, void *dst, const void *src, size_t n, int tries,
                            void (*copy)(void *, const void *, size_t), void (*wait)(void), uint32_t *retries)
{
    for (int i = 0; i < tries; i++) {
        uint32_t s1 = *seq;
        __sync_synchronize();   // acquire: чтение seq раньше чтения данных
        if (s1 & 1) { wait(); continue; }
        copy(dst, src, n);
        __sync_synchronize();   // данные прочитаны раньше повторного чтения seq
        uint32_t s2 = *seq;
        if (s1 == s2) return true;
        (*retries)++;
    }
    return false;
}
