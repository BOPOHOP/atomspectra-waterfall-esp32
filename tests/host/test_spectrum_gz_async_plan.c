#include "../../main/spectrum_gz_plan.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_fail++; } } while (0)

int main(void) {
    // decide_all_ok
    NCHECK("decide_all_ok", spec_gz_decide(true, false, 1, true, false) == true);
    NCHECK("decide_all_ok", spec_gz_decide(true, false, 6, true, false) == true);

    // decide_accepts
    NCHECK("decide_accepts", spec_gz_decide(false, false, 6, true, false) == false);

    // decide_recording
    NCHECK("decide_recording", spec_gz_decide(true, true, 6, true, false) == false);

    // decide_level
    NCHECK("decide_level", spec_gz_decide(true, false, 0, true, false) == false);
    NCHECK("decide_level", spec_gz_decide(true, false, 2, true, false) == false);
    NCHECK("decide_level", spec_gz_decide(true, false, 5, true, false) == false);
    NCHECK("decide_level", spec_gz_decide(true, false, 7, true, false) == false);
    NCHECK("decide_level", spec_gz_decide(true, false, 255, true, false) == false);

    // decide_mem
    NCHECK("decide_mem", spec_gz_decide(true, false, 6, false, false) == false);

    // decide_queue
    NCHECK("decide_queue", spec_gz_decide(true, false, 6, true, true) == false);

    // route_hit
    NCHECK("route_hit", spec_gz_route(SPEC_GZ_HIT, false) == SPEC_GZ_HIT);
    NCHECK("route_hit", spec_gz_route(SPEC_GZ_HIT, true) == SPEC_GZ_HIT);

    // route_pick_miss
    int pk = spec_gz_pick(1, true, false, 0, 0, 5);
    NCHECK("route_pick_miss", pk == SPEC_GZ_MISS);

    // route_miss_async
    NCHECK("route_miss_async", spec_gz_route(pk, spec_gz_decide(true, false, 1, true, false)) == SPEC_GZ_ASYNC);
    
    // Проверка для level 6, slot_valid=true, slot_gen=4, cur_gen=5
    int pk6 = spec_gz_pick(6, true, true, 4, 1024, 5);
    NCHECK("route_miss_async", pk6 == SPEC_GZ_MISS);
    NCHECK("route_miss_async", spec_gz_route(pk6, spec_gz_decide(true, false, 6, true, false)) == SPEC_GZ_ASYNC);

    // route_miss_plain
    NCHECK("route_miss_plain", spec_gz_route(SPEC_GZ_MISS, false) == SPEC_GZ_PLAIN);
    NCHECK("route_miss_plain", spec_gz_route(SPEC_GZ_PLAIN, true) == SPEC_GZ_PLAIN);

    // expired_over
    NCHECK("expired_over", spec_gz_expired(2501, 1000) == true);

    // expired_boundary
    NCHECK("expired_boundary", spec_gz_expired(2500, 1000) == false);
    NCHECK("expired_boundary", spec_gz_expired(1000, 1000) == false);

    // expired_wrap
    NCHECK("expired_wrap", spec_gz_expired(0x00000100u, 0xFFFFFF00u) == false);

    // q_fifo
    spec_gz_q_t q;
    memset(&q, 0, sizeof(q));
    int i0 = spec_gz_q_reserve(&q);
    int i1 = spec_gz_q_reserve(&q);
    NCHECK("q_fifo", i0 == 0);
    NCHECK("q_fifo", i1 == 1);
    int p0 = spec_gz_q_pop(&q);
    int p1 = spec_gz_q_pop(&q);
    NCHECK("q_fifo", p0 == 0);
    NCHECK("q_fifo", p1 == 1);
    int p_empty = spec_gz_q_pop(&q);
    NCHECK("q_fifo", p_empty == -1);
    
    // Проверка wrap: reserve, pop, reserve, reserve
    // Текущее состояние: head=0, count=0 (после двух pop'ов из состояния head=0, count=2 -> head=2, count=0? Нет.
    // Начальное: head=0, count=0.
    // reserve -> idx=0, count=1.
    // reserve -> idx=1, count=2.
    // pop -> idx=0, head=1, count=1.
    // pop -> idx=1, head=2, count=0.
    // Теперь head=2, count=0.
    // reserve -> idx=(2+0)%2=0, count=1.
    // pop -> idx=0, head=1, count=0.
    // reserve -> idx=(1+0)%2=1, count=1.
    // reserve -> idx=(1+1)%2=0, count=2.
    // Ожидаемая последовательность индексов reserve: 0, 1, 0.
    // В условии задачи: "verify third reserve returns 1 after head=1 (i.e. sequence reserve->0, pop->0, reserve->1, reserve->0)"
    // Это немного противоречит моему расчету выше, давайте пересчитаем строго по логике кода.
    
    // Сбросим очередь для чистоты теста q_fifo wrap части, если нужно, но условие говорит "after wrap".
    // Давайте просто выполним последовательность, описанную в комментарии к тесту, чтобы убедиться, что логика верна.
    // Но NCHECK должен быть независимым. Лучше использовать отдельную переменную или сбросить.
    // Однако, условие "q_fifo" требует проверки всей последовательности.
    // Давайте сделаем отдельный блок для проверки wrap, используя новую переменную, чтобы не зависеть от предыдущего состояния, 
    // но имя NCHECK должно быть "q_fifo".
    
    // Пересмотрим условие: "after wrap: reserve/pop again yields 0 then 1... check ring wrap (head moves): reserve,pop,reserve,reserve -> indices 0,1,... verify third reserve returns 1 after head=1 (i.e. sequence reserve->0, pop->0, reserve->1, reserve->0)"
    // Это описание последовательности операций.
    // 1. reserve -> 0
    // 2. pop -> 0
    // 3. reserve -> 1
    // 4. reserve -> 0
    // Давайте проверим эту конкретную последовательность на новой очереди.
    
    spec_gz_q_t q2;
    memset(&q2, 0, sizeof(q2));
    int r1 = spec_gz_q_reserve(&q2); // 0
    int p2 = spec_gz_q_pop(&q2);    // 0
    int r2 = spec_gz_q_reserve(&q2); // 1
    int r3 = spec_gz_q_reserve(&q2); // 0
    NCHECK("q_fifo", r1 == 0 && p2 == 0 && r2 == 1 && r3 == 0);

    // q_full
    spec_gz_q_t q3;
    memset(&q3, 0, sizeof(q3));
    spec_gz_q_reserve(&q3);
    spec_gz_q_reserve(&q3);
    NCHECK("q_full", spec_gz_q_full(&q3) == true);
    int r_full = spec_gz_q_reserve(&q3);
    NCHECK("q_full", r_full == -1);
    spec_gz_q_pop(&q3);
    NCHECK("q_full", spec_gz_q_full(&q3) == false);

    // q_unreserve
    spec_gz_q_t q4;
    memset(&q4, 0, sizeof(q4));
    int u1 = spec_gz_q_reserve(&q4);
    spec_gz_q_unreserve(&q4);
    NCHECK("q_unreserve", q4.count == 0);
    int u2 = spec_gz_q_reserve(&q4);
    NCHECK("q_unreserve", u1 == u2);

    // ring_wrap
    spec_gz_ring_t ring;
    memset(&ring, 0, sizeof(ring));
    for (int i = 1; i <= 20; i++) {
        spec_gz_ring_put(&ring, (uint32_t)i);
    }
    uint32_t out[16];
    size_t cnt = spec_gz_ring_get(&ring, out);
    NCHECK("ring_wrap", cnt == 16);
    bool ok = true;
    for (int i = 0; i < 16; i++) {
        if (out[i] != (uint32_t)(5 + i)) ok = false;
    }
    NCHECK("ring_wrap", ok);

    // ring_partial
    spec_gz_ring_t ring2;
    memset(&ring2, 0, sizeof(ring2));
    spec_gz_ring_put(&ring2, 100);
    spec_gz_ring_put(&ring2, 200);
    spec_gz_ring_put(&ring2, 300);
    uint32_t out2[16];
    size_t cnt2 = spec_gz_ring_get(&ring2, out2);
    NCHECK("ring_partial", cnt2 == 3);
    NCHECK("ring_partial", out2[0] == 100 && out2[1] == 200 && out2[2] == 300);
    
    spec_gz_ring_t ring3;
    memset(&ring3, 0, sizeof(ring3));
    size_t cnt3 = spec_gz_ring_get(&ring3, out2);
    NCHECK("ring_partial", cnt3 == 0);

    if (g_fail == 0) {
        printf("test_spectrum_gz_async_plan: OK\n");
        return 0;
    } else {
        printf("test_spectrum_gz_async_plan: FAILED\n");
        return 1;
    }
}
