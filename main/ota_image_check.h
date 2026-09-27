#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
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
