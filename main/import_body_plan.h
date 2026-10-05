#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef IMPORT_BODY_SIZE
#define IMPORT_BODY_SIZE 32896u
#endif

#ifndef IMPORT_DRAIN_MAX
#define IMPORT_DRAIN_MAX 40960u
#endif

#ifndef IMPORT_RECV_DEADLINE_MS
#define IMPORT_RECV_DEADLINE_MS 30000u
#endif

typedef enum {
    IMP_LEN_OK = 0,
    IMP_LEN_DRAIN_400 = 1,
    IMP_LEN_CLOSE_400 = 2
} imp_len_action_t;

static inline imp_len_action_t import_len_action(size_t content_len) {
    if (content_len == IMPORT_BODY_SIZE) {
        return IMP_LEN_OK;
    }
    if (content_len <= IMPORT_DRAIN_MAX) {
        return IMP_LEN_DRAIN_400;
    }
    return IMP_LEN_CLOSE_400;
}

static inline bool import_recv_deadline_passed(int64_t start_us, int64_t now_us) {
    if (now_us < start_us) {
        return false;
    }
    return (now_us - start_us) >= (int64_t)IMPORT_RECV_DEADLINE_MS * 1000;
}

static inline size_t import_drain_chunk(size_t remaining, size_t buf_cap) {
    if (buf_cap == 0) {
        return 0;
    }
    return (remaining < buf_cap) ? remaining : buf_cap;
}
