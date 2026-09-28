#pragma once
// AWF-5: разбор SHA256SUMS.txt релиза GitHub. Формат (подтверждён живой
// выгрузкой firmware-v1.2.25): построчно "<64 hex><SP>[*| ]<имя файла>\n"
// (классический вывод `sha256sum` в бинарном (*) или текстовом (пробел)
// режиме -- поддерживаем оба). Пустые строки и строки, не начинающиеся с
// 64 hex-символов, пропускаются.
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

// Ищет строку файла filename в содержимом SHA256SUMS.txt (buf, длина len,
// без обязательного завершающего нуля). При успехе копирует 64-символьный
// hex-хеш (без завершающего нуля добавляет сам, out_hex64 должен быть
// >= 65 байт) в out_hex64 и возвращает true. Регистр hex не нормализует
// (сравнивать через strcasecmp на стороне вызывающего), имя файла сравнивает
// точным совпадением байт (регистрозависимо -- имена ассетов GitHub
// регистрозависимы).
static inline bool ota_gh_sha256sums_find(const char *buf, size_t len,
                                           const char *filename, char *out_hex64)
{
    if (!buf || !filename || !out_hex64) return false;
    size_t fname_len = strlen(filename);
    size_t i = 0;
    while (i < len) {
        size_t line_start = i;
        while (i < len && buf[i] != '\n') i++;
        size_t line_len = i - line_start;
        // срезать возможный '\r' в конце строки (CRLF)
        if (line_len > 0 && buf[line_start + line_len - 1] == '\r') line_len--;
        if (i < len) i++; // пропустить '\n'

        if (line_len >= 64) {
            bool hex_ok = true;
            for (size_t k = 0; k < 64; k++) {
                if (!isxdigit((unsigned char)buf[line_start + k])) { hex_ok = false; break; }
            }
            if (hex_ok && line_len > 64) {
                size_t p = line_start + 64;
                size_t line_end = line_start + line_len;
                if (p < line_end && buf[p] == ' ') {
                    p++;
                    if (p < line_end && (buf[p] == '*' || buf[p] == ' ')) p++;
                    size_t name_len = line_end - p;
                    if (name_len == fname_len && memcmp(buf + p, filename, fname_len) == 0) {
                        memcpy(out_hex64, buf + line_start, 64);
                        out_hex64[64] = '\0';
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
