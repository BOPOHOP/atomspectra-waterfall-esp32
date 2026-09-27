#include "ota_image_check.h"
#include "test_util.h"
#include <string.h>
#define S3 9u   // ESP_CHIP_ID_ESP32S3

// Валидный esp32s3-заголовок: magic=0xE9, chip_id(offset12-13 LE)=9.
static void mk_hdr(uint8_t *h, uint8_t magic, uint16_t chip_id)
{
    memset(h, 0, 24);
    h[0] = magic;
    h[12] = (uint8_t)(chip_id & 0xFF);
    h[13] = (uint8_t)(chip_id >> 8);
}

// D1: собрать синтетический образ в буфер: header(24, magic+segcount) +
// N сегментов (8Б load_addr+data_len LE + data_len байт данных, значение
// байт не важно walker'у). buf должен быть >= требуемого размера.
static size_t mk_image(uint8_t *buf, uint8_t segcount, const uint32_t *seg_lens)
{
    size_t p = 0;
    memset(buf, 0, 24);
    buf[0] = 0xE9; buf[1] = segcount;
    p = 24;
    for (uint8_t s = 0; s < segcount; s++) {
        buf[p+0]=0; buf[p+1]=0; buf[p+2]=0; buf[p+3]=0;   // load_addr, не важен
        uint32_t dl = seg_lens[s];
        buf[p+4]=(uint8_t)dl; buf[p+5]=(uint8_t)(dl>>8);
        buf[p+6]=(uint8_t)(dl>>16); buf[p+7]=(uint8_t)(dl>>24);
        p += 8;
        memset(buf+p, 0xAB, dl);
        p += dl;
    }
    return p;
}

// D1: полный образ (заявленные сегменты сошлись целиком) -> DONE.
static void ota_walker_full_image_complete(void)
{
    uint8_t buf[512];
    uint32_t lens[2] = {100, 200};
    size_t n = mk_image(buf, 2, lens);
    ota_image_walker_t w;
    ota_image_walker_init(&w);
    CHECK(ota_image_walker_feed(&w, buf, n));
    CHECK(ota_image_walker_is_complete(&w));
}

// D1 (живой баг T3, verify-awf4): реальный образ 200КБ из 1.6МБ — заголовок
// заявляет 2 больших сегмента, но поток обрывается СЕРЕДИНЕ первого
// сегмента. Content-Length у прежнего кода совпадал с фактически принятым
// (в этом и был баг — сервер не знал истинного размера) — walker обязан
// сказать "неполно", т.к. заявленный segment_count не пройден целиком.
static void ota_walker_truncated_image_incomplete(void)
{
    uint8_t full[1024];
    uint32_t lens[2] = {600, 300};
    size_t n = mk_image(full, 2, lens);
    size_t truncated_n = 24 + 8 + 200;  // header+seg1_hdr+только 200 из 600Б данных
    CHECK(truncated_n < n);

    ota_image_walker_t w;
    ota_image_walker_init(&w);
    CHECK(ota_image_walker_feed(&w, full, truncated_n));
    CHECK(!ota_image_walker_is_complete(&w));   // ключевая проверка D1
}

// Порядок-независимость: тот же полный образ, поданный кусками по 37 байт
// (как в handle_ota, буфер 4096, но здесь мельче — чтобы задеть все границы
// header/seg_header/seg_data хотя бы раз) — итог должен совпасть с одноразовой
// подачей.
static void ota_walker_chunked_feed_same_result(void)
{
    uint8_t buf[512];
    uint32_t lens[3] = {50, 0, 77};   // средний сегмент нулевой длины — граничный случай
    size_t n = mk_image(buf, 3, lens);
    ota_image_walker_t w;
    ota_image_walker_init(&w);
    size_t off = 0;
    while (off < n) {
        size_t chunk = (n - off) < 37 ? (n - off) : 37;
        CHECK(ota_image_walker_feed(&w, buf + off, chunk));
        off += chunk;
    }
    CHECK(ota_image_walker_is_complete(&w));
    CHECK(w.total_len == n);
}

static void ota_walker_bad_magic_errors_immediately(void)
{
    uint8_t buf[64];
    uint32_t lens[1] = {10};
    size_t n = mk_image(buf, 1, lens);
    buf[0] = 0x00;   // испортить магию
    ota_image_walker_t w;
    ota_image_walker_init(&w);
    CHECK(!ota_image_walker_feed(&w, buf, n));
    CHECK(!ota_image_walker_is_complete(&w));
}

// Все сегменты нулевой длины — вырожденный, но структурно валидный случай.
static void ota_walker_zero_length_segment(void)
{
    uint8_t buf[64];
    uint32_t lens[2] = {0, 0};
    size_t n = mk_image(buf, 2, lens);
    ota_image_walker_t w;
    ota_image_walker_init(&w);
    CHECK(ota_image_walker_feed(&w, buf, n));
    CHECK(ota_image_walker_is_complete(&w));
}

void ota_image_check_suite(void)
{
    uint8_t h[24];

    mk_hdr(h, 0xE9, S3);
    CHECK(ota_image_header_is_valid(h, sizeof(h), S3));           // валиден

    mk_hdr(h, 0x00, S3);   // не 0xE9 (не образ / битый заголовок)
    CHECK(!ota_image_header_is_valid(h, sizeof(h), S3));

    mk_hdr(h, 0xE9, 0);    // chip_id=ESP32 (обычный), а плата esp32s3
    CHECK(!ota_image_header_is_valid(h, sizeof(h), S3));

    mk_hdr(h, 0xE9, S3);
    CHECK(!ota_image_header_is_valid(h, 13, S3));                 // < 14 байт
    CHECK(!ota_image_header_is_valid(NULL, sizeof(h), S3));

    ota_walker_full_image_complete();
    ota_walker_truncated_image_incomplete();
    ota_walker_chunked_feed_same_result();
    ota_walker_bad_magic_errors_immediately();
    ota_walker_zero_length_segment();
}
