/* Фаза 2: фаззинг разборщика имён снимков backup_parse_name().
 *
 * Разбирает имя, пришедшее из HTTP-URI, и из разобранных чисел ВОССТАНАВЛИВАЕТСЯ
 * путь на файловой системе (spectrum.c). Значит любая строка, которую разборщик
 * принял ошибочно, превращается в обращение к файлу — цена дефекта здесь выше
 * обычного разбора.
 *
 * Проверяются три свойства на случайных и злонамеренных входах:
 *   1) не падает и не читает за границей (запускать под ASan: make asan);
 *   2) принятое имя ВСЕГДА восстанавливается обратно в самого себя
 *      (round-trip: parse -> printf -> parse даёт те же числа);
 *   3) принятое имя не содержит ничего, кроме "bk_<цифры>_<цифры>.bin",
 *      то есть в путь не может попасть ни '/', ни '.', ни '\0' в середине.
 *
 * Свойство 2 важнее прочих: именно оно ломалось на ведущих нулях (bk_03_1.bin
 * разбиралось в {3,1}, а удалять шли bk_3_1.bin — другой файл).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "backup_plan.h"
#include "test_util.h"

/* Детерминированный генератор: прогон должен воспроизводиться по номеру seed. */
static uint32_t rng_state = 0x1234567u;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Алфавит смещён к символам, опасным для путей и для разбора чисел. */
static char rnd_char(void)
{
    static const char pool[] = "0123456789_.bkin/\\%.-+ \t\r\n\x01\x7f";
    return pool[rnd() % (sizeof(pool) - 1)];
}

void fuzz_backup_name(void)
{
    int accepted = 0, rejected = 0;
    char buf[72];

    for (int iter = 0; iter < 200000; iter++) {
        /* Чистый шум почти никогда не собирает валидное имя: первый прогон дал
         * 0 принятых из 200000, то есть свойства round-trip не проверялись вовсе
         * (зелёный тест, не сделавший проверки). Поэтому три четверти итераций —
         * МУТАЦИИ годного имени: так вход остаётся рядом с границей приёма, где
         * дефекты разбора и живут. */
        if (iter % 4 == 0) {
            size_t len = rnd() % (sizeof(buf) - 1);
            for (size_t i = 0; i < len; i++) buf[i] = rnd_char();
            buf[len] = '\0';
        } else {
            snprintf(buf, sizeof(buf), BACKUP_NAME_FMT,
                     (uint32_t)(rnd() % 1000), (uint32_t)(rnd() % 1000));
            int mutations = 1 + (int)(rnd() % 3);
            for (int m = 0; m < mutations; m++) {
                size_t l = strlen(buf);
                if (l == 0) break;
                switch (rnd() % 3) {
                case 0:                                   /* заменить символ */
                    buf[rnd() % l] = rnd_char();
                    break;
                case 1: {                                 /* удалить символ */
                    /* Позиция берётся ОДИН раз, длина — остаток вместе с '\0'.
                     * В первой редакции стояли два независимых rnd()%l и длина l —
                     * ASan поймал запись за границей буфера (unknown-crash,
                     * WRITE of size 42). Дефект был в самом фаззере. */
                    size_t at = rnd() % l;
                    memmove(buf + at, buf + at + 1, l - at);
                    break;
                }
                default:                                  /* вставить символ */
                    if (l + 1 < sizeof(buf)) {
                        size_t at = rnd() % (l + 1);
                        memmove(buf + at + 1, buf + at, l - at + 1);
                        buf[at] = rnd_char();
                    }
                    break;
                }
            }
        }

        backup_id_t id;
        if (!backup_parse_name(buf, &id)) { rejected++; continue; }
        accepted++;

        /* Свойство 3: принятое имя состоит только из ожидаемых частей. */
        CHECK(strncmp(buf, "bk_", 3) == 0);
        CHECK(strstr(buf, ".bin") != NULL);
        CHECK(strchr(buf, '/') == NULL);
        CHECK(strchr(buf, '\\') == NULL);
        CHECK(strstr(buf, "..") == NULL);

        /* Свойство 2: round-trip. Имя, восстановленное из чисел, обязано
         * совпадать с исходным — иначе удалять/читать пойдут не тот файл. */
        char rebuilt[72];
        snprintf(rebuilt, sizeof(rebuilt), BACKUP_NAME_FMT, id.sess, id.seq);
        CHECK(strcmp(rebuilt, buf) == 0);

        backup_id_t id2;
        CHECK(backup_parse_name(rebuilt, &id2));
        CHECK(id2.sess == id.sess && id2.seq == id.seq);
    }

    /* Отдельно — заведомо злонамеренные строки, которые случайный генератор
     * почти наверняка не соберёт сам. Все обязаны быть отвергнуты. */
    static const char *evil[] = {
        "bk_1_1.bin/../../etc", "../bk_1_1.bin", "bk_1_1.bin%00.txt",
        "bk_/1.bin", "bk_1_1.bin.bin", "bk_1_1.BIN", "BK_1_1.bin",
        "bk_1_1.bi", "bk_1_1.binx", "bk_+1_1.bin", "bk_-1_1.bin",
        "bk_1_1.bin ", " bk_1_1.bin", "bk_4294967296_1.bin",
        "bk_1_4294967296.bin", "bk__1.bin", "bk_1__1.bin", "bk_1_1..bin",
        "bk_0x1_1.bin", "bk_1_1.bin\n", "bk_007_1.bin", "bk_1_007.bin",
    };
    for (size_t i = 0; i < sizeof(evil) / sizeof(evil[0]); i++) {
        backup_id_t id;
        if (backup_parse_name(evil[i], &id)) {
            printf("FAIL fuzz: принято злонамеренное имя \"%s\" -> {%u,%u}\n",
                   evil[i], (unsigned)id.sess, (unsigned)id.seq);
            g_failures++;
        }
    }

    /* Санитарная проверка самого фаззера: если он не принял НИ ОДНОГО
     * годного имени, значит он ничего не проверил (зелёный по недосмотру). */
    backup_id_t ok_id;
    CHECK(backup_parse_name("bk_7_3.bin", &ok_id) && ok_id.sess == 7 && ok_id.seq == 3);
    /* И жёстче: доля принятых должна быть осмысленной, иначе прогон холостой.
     * Порог 1000 из 200000 (0,5 %) — с запасом ниже наблюдаемого на мутациях,
     * но достаточно, чтобы поймать «фаззер перестал попадать в границу». */
    if (accepted < 1000) {
        printf("FAIL fuzz: принято всего %d годных имён — фаззинг холостой, "
               "свойства round-trip не проверены\n", accepted);
        g_failures++;
    }

    printf("fuzz_backup_name: 200000 случайных (принято %d, отвергнуто %d) + %zu злонамеренных\n",
           accepted, rejected, sizeof(evil) / sizeof(evil[0]));
}
