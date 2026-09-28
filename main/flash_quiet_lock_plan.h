#pragma once
#include <stdbool.h>

/* У3 (раунд 3): захват лока писателя LittleFS в тихом окне после пакета гистограммы. Окно
 * ждём ДО лока и проверяем ПОВТОРНО после: лок мог освободиться, когда окно уже закрылось, —
 * тогда отпускаем и ждём следующее. Не дождались за max_polls опросов или после 3 повторных
 * захватов — пишем под локом без окна, как 1.2.27. Ввод-вывод подставляет вызывающий. */
typedef struct {
    bool (*usb_live)(void *ctx);    /* прибор на USB (иначе окно не нужно) */
    bool (*can_start)(void *ctx);   /* тихое окно открыто */
    bool (*lock)(void *ctx);        /* захват лока писателя с таймаутом */
    void (*unlock)(void *ctx);
    void (*sleep_poll)(void *ctx);  /* пауза между опросами окна */
} flash_quiet_lock_ops_t;

static inline bool flash_quiet_lock_in_window(const flash_quiet_lock_ops_t *o, void *ctx, int max_polls)
{
    int polls = 0, relocks = 0;
    for (;;) {
        while (polls < max_polls && o->usb_live(ctx) && !o->can_start(ctx)) {
            o->sleep_poll(ctx);
            polls++;
        }
        if (!o->lock(ctx)) return false;
        if (polls >= max_polls || relocks >= 3 || !o->usb_live(ctx) || o->can_start(ctx)) return true;
        o->unlock(ctx);   /* окно закрылось, пока ждали лок */
        relocks++;
    }
}
