#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
// AWF-4 (Wi-Fi OTA): проверка заголовка образа ДО esp_ota_end() — чистая
// функция, свободная от ESP-IDF. Layout esp_image_header_t
// (bootloader_support/include/esp_app_format.h, ESP-IDF v5.4): offset 0
// magic(u8)=0xE9, offset 12-13 chip_id(u16 LE). ESP_CHIP_ID_ESP32S3=9.
#define OTA_IMAGE_HEADER_MAGIC      0xE9u
#define OTA_IMAGE_HEADER_MIN_LEN    14u
#define OTA_IMAGE_CHIP_ID_OFFSET    12u

// true, если header[0..len) содержит правдоподобный esp_image_header_t для
// expected_chip_id. len<14 -> false (заголовок не читается целиком, решать
// нечем — caller обязан ОТКАЗАТЬ, не пропустить).
static inline bool ota_image_header_is_valid(const uint8_t *header, size_t len,
                                              uint16_t expected_chip_id)
{
    if (!header || len < OTA_IMAGE_HEADER_MIN_LEN) return false;
    if (header[0] != OTA_IMAGE_HEADER_MAGIC) return false;
    uint16_t chip_id = (uint16_t)header[OTA_IMAGE_CHIP_ID_OFFSET] |
                        ((uint16_t)header[OTA_IMAGE_CHIP_ID_OFFSET + 1] << 8);
    return chip_id == expected_chip_id;
}

// D1 (2026-09-27, P0, verify-awf4-2026-09-27.md разд.5): handle_ota сверял
// принятые байты ТОЛЬКО с клиентским Content-Length — под контролем
// клиента. Walker разбирает esp_image_header_t(24Б)+N сегментов(8Б
// load_addr+data_len) В ПОТОКЕ, тем же порядком байт, что esp_ota_write().
typedef enum {
    OTA_WALK_HEADER = 0,
    OTA_WALK_SEG_HEADER,
    OTA_WALK_SEG_DATA,
    OTA_WALK_DONE,
    OTA_WALK_ERROR
} ota_walk_state_t;
typedef struct {
    ota_walk_state_t state;
    uint8_t  hdrbuf[24];
    uint8_t  hdrbuf_len;
    uint8_t  seghdrbuf[8];
    uint8_t  seghdrbuf_len;
    uint8_t  segment_count;
    uint8_t  segments_done;
    uint32_t seg_remaining;
    uint64_t total_len;
} ota_image_walker_t;
static inline void ota_image_walker_init(ota_image_walker_t *w)
{
    w->state = OTA_WALK_HEADER;
    w->hdrbuf_len = 0;
    w->seghdrbuf_len = 0;
    w->segment_count = 0;
    w->segments_done = 0;
    w->seg_remaining = 0;
    w->total_len = 0;
}
// false -> структурно битый образ — caller обязан отказать немедленно.
static inline bool ota_image_walker_feed(ota_image_walker_t *w, const uint8_t *data, size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (w->state == OTA_WALK_ERROR) return false;
        if (w->state == OTA_WALK_HEADER) {
            size_t need = (size_t)(24 - w->hdrbuf_len);
            size_t take = (len - i) < need ? (len - i) : need;
            memcpy(w->hdrbuf + w->hdrbuf_len, data + i, take);
            w->hdrbuf_len = (uint8_t)(w->hdrbuf_len + take);
            i += take; w->total_len += take;
            if (w->hdrbuf_len == 24) {
                if (w->hdrbuf[0] != OTA_IMAGE_HEADER_MAGIC) { w->state = OTA_WALK_ERROR; return false; }
                w->segment_count = w->hdrbuf[1];
                if (w->segment_count == 0) { w->state = OTA_WALK_ERROR; return false; }
                w->state = OTA_WALK_SEG_HEADER;
            }
        }
        else if (w->state == OTA_WALK_SEG_HEADER) {
            size_t need = (size_t)(8 - w->seghdrbuf_len);
            size_t take = (len - i) < need ? (len - i) : need;
            memcpy(w->seghdrbuf + w->seghdrbuf_len, data + i, take);
            w->seghdrbuf_len = (uint8_t)(w->seghdrbuf_len + take);
            i += take; w->total_len += take;
            if (w->seghdrbuf_len == 8) {
                uint32_t data_len = (uint32_t)w->seghdrbuf[4] | ((uint32_t)w->seghdrbuf[5] << 8) |
                                     ((uint32_t)w->seghdrbuf[6] << 16) | ((uint32_t)w->seghdrbuf[7] << 24);
                w->seghdrbuf_len = 0;
                w->seg_remaining = data_len;
                w->state = OTA_WALK_SEG_DATA;
                if (data_len == 0) {
                    w->segments_done++;
                    w->state = (w->segments_done == w->segment_count) ? OTA_WALK_DONE : OTA_WALK_SEG_HEADER;
                }
            }
        }
        else if (w->state == OTA_WALK_SEG_DATA) {
            size_t take = (len - i) < w->seg_remaining ? (len - i) : w->seg_remaining;
            i += take; w->total_len += take;
            w->seg_remaining -= (uint32_t)take;
            if (w->seg_remaining == 0) {
                w->segments_done++;
                w->state = (w->segments_done == w->segment_count) ? OTA_WALK_DONE : OTA_WALK_SEG_HEADER;
            }
        } else { // OTA_WALK_DONE: хвост (checksum+sha256) — не часть структурной проверки
            size_t take = len - i;
            i += take; w->total_len += take;
        }
    }
    return true;
}

static inline bool ota_image_walker_is_complete(const ota_image_walker_t *w)
{
    return w->state == OTA_WALK_DONE;
}
