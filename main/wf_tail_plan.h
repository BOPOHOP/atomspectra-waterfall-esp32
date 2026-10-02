#pragma once
// #RST-TAIL (1.2.30): строка водопада перед «Сбросом». Плата отправляет -rst сразу, прибор обнуляет счётчики, и импульсы
// с последней записанной строки (до одного шага записи) пропадали. Теперь обработчик Сброса просит wf_task закрыть
// внеочередную строку (spectrogram_flush_tail) и только потом шлёт команду. Решение «писать ли строку на этом тике» — здесь
// (host-pure, tests/host/test_wf_tail_plan.c).
#include <stdbool.h>
#include <stdint.h>

// force=false — обычный тик: строка закрывается, когда живое время прибора ушло вперёд не меньше чем на iv секунд
// (или откатилось/стоит на месте за пределами окна — прежнее поведение `now >= prev && now-prev < iv → ждём`).
// force=true — перед Сбросом: писать, если время прибора хоть немного продвинулось; иначе хвоста нет (dur=0).
static inline bool wf_tail_should_row(bool force, uint32_t now_time, uint32_t prev_time, uint32_t iv)
{
    if (force) return now_time > prev_time;
    return !(now_time >= prev_time && now_time - prev_time < iv);
}
