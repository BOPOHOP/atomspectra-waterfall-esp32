#pragma once
#include <stdint.h>
/* Host glue stubs: time is set by the test (glue_now_us), it does not move by itself. */
extern int64_t glue_now_us;
int64_t esp_timer_get_time(void);
