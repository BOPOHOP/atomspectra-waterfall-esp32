// WP5 (1.2.30): планировщик кусков выгрузки журнала (main/debug_log_chunk_plan.h).
#include "debug_log_chunk_plan.h"
#include "test_util.h"

void test_debug_log_chunk(void)
{
    size_t off = 99, len = 99;
    // кольцо: записано 1000 байт всего, в кольце последние 400 (oldest=600); срез [700,1000)
    CHECK(dbglog_chunk_plan(1000, 400, 700, 1000, 128, &off, &len) == DBGLOG_CHUNK_OK && off == 100 && len == 128);
    CHECK(dbglog_chunk_plan(1000, 400, 956, 1000, 128, &off, &len) == DBGLOG_CHUNK_OK && off == 356 && len == 44);   // хвост короче куска
    CHECK(dbglog_chunk_plan(1000, 400, 1000, 1000, 128, &off, &len) == DBGLOG_CHUNK_DONE);                        // всё отдано
    CHECK(dbglog_chunk_plan(1000, 400, 599, 1000, 128, &off, &len) == DBGLOG_CHUNK_OVERWRITTEN);                  // старейший байт уже затёрт
    CHECK(dbglog_chunk_plan(1000, 400, 600, 1000, 128, &off, &len) == DBGLOG_CHUNK_OK && off == 0);               // ровно старейший — цел
    // пока слали, записали ещё 300 байт (total=1300, oldest=900): срез [700,1000) → байты 700..899 затёрты
    CHECK(dbglog_chunk_plan(1300, 400, 700, 1000, 128, &off, &len) == DBGLOG_CHUNK_OVERWRITTEN);
    CHECK(dbglog_chunk_plan(1300, 400, 900, 1000, 128, &off, &len) == DBGLOG_CHUNK_OK && off == 0 && len == 100);
    CHECK(dbglog_chunk_plan(5000000000ULL, 100, 4999999950ULL, 5000000000ULL, 64, &off, &len) == DBGLOG_CHUNK_OK && off == 50 && len == 50);   // 64-бит
}
