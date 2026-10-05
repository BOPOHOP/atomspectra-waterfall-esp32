#include "../../main/import_body_plan.h"
#include <stdio.h>
#include <stdint.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    CHECK(import_len_action(32896) == IMP_LEN_OK);
    CHECK(import_len_action(32895) == IMP_LEN_DRAIN_400);
    CHECK(import_len_action(32897) == IMP_LEN_DRAIN_400);
    CHECK(import_len_action(0) == IMP_LEN_DRAIN_400);
    CHECK(import_len_action(100) == IMP_LEN_DRAIN_400);
    CHECK(import_len_action(40960) == IMP_LEN_DRAIN_400);
    CHECK(import_len_action(40961) == IMP_LEN_CLOSE_400);
    CHECK(import_len_action(1000000) == IMP_LEN_CLOSE_400);

    uint64_t start = 1000000;
    CHECK(import_recv_deadline_passed(start, start + 29999999) == false);
    CHECK(import_recv_deadline_passed(start, start + 30000000) == true);
    CHECK(import_recv_deadline_passed(start, start + 90000000) == true);
    CHECK(import_recv_deadline_passed(start, start - 5) == false);

    CHECK(import_drain_chunk(100, 512) == 100);
    CHECK(import_drain_chunk(5000, 512) == 512);
    CHECK(import_drain_chunk(512, 512) == 512);
    CHECK(import_drain_chunk(0, 512) == 0);
    CHECK(import_drain_chunk(100, 0) == 0);

    if (fails == 0) {
        printf("test_import_body_plan: OK\n");
        return 0;
    } else {
        printf("test_import_body_plan: FAILED\n");
        return 1;
    }
}
