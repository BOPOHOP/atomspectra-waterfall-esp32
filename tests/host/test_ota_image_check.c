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
}
