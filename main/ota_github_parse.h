#pragma once
// AWF-5: два независимых парсера без внешних зависимостей (используются и в
// host-тестах, и в прошивке -- единый код).
//
// 1) ota_gh_releases_pick_best() -- сканирует "сырой" JSON-массив ответа
//    GET /repos/.../releases?per_page=10 (объекты релизов на верхнем уровне
//    массива) и находит релиз с максимальным тегом "firmware-vX.Y.Z[-rcN]",
//    отбрасывая draft всегда и prerelease при want_prerelease=false. Сканер
//    НЕ полный JSON-парсер: ищет по подстрокам "tag_name"/"draft"/
//    "prerelease" внутри границ каждого объекта верхнего уровня массива,
//    границы объектов находятся счётчиком фигурных скобок с учётом строк
//    в кавычках и экранирования (\\" и \\\\ внутри строки не закрывают её) --
//    поэтому тексты release notes с кавычками/скобками внутри "body" не
//    сбивают разбор.
// 2) ota_gh_find_asset_url() -- по имени сканирует массив "assets":[...]
//    внутри уже найденной границы объекта релиза и достаёт
//    "browser_download_url" ближайшего asset-объекта с заданным "name".
//
// Обе функции работают на буфере в памяти (буфер целиком должен быть уже
// скачан) и не модифицируют его, кроме временной установки '\0' на границах
// найденных строк через выходные length-параметры (без изменения буфера).
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "ota_github_version.h"

// Находит следующее вхождение literal-подстроки key начиная с pos, но только
// ВНЕ строковых литералов JSON. Возвращает указатель на начало найденного key
// или NULL. end -- конец разрешённой области поиска (не включая).
static inline const char *ota_gh__find_key(const char *pos, const char *end, const char *key)
{
    bool in_str = false;
    size_t klen = strlen(key);
    for (const char *p = pos; p + klen <= end; p++) {
        if (in_str) {
            if (*p == '\\') { p++; continue; }
            if (*p == '"') in_str = false;
            continue;
        }
        // Матч проверяем ДО переключения in_str: key сам начинается с '"'
        // (например "tag_name"), и без этого его открывающая кавычка сразу
        // переводила бы сканер в режим "внутри строки", так и не дав
        // сравнить memcmp() в этой позиции -- ни один ключ никогда бы не
        // нашёлся (баг найден host-тестом на реальной выгрузке GitHub).
        if (memcmp(p, key, klen) == 0) return p;
        if (*p == '"') { in_str = true; continue; }
    }
    return NULL;
}

// Возвращает границы [obj_start, obj_end) top-level-объекта в JSON-массиве,
// index-ного по счёту (0-based). obj_end указывает на символ ПОСЛЕ '}'.
// Возвращает false, если объекта с таким индексом нет (мусор/конец массива).
static inline bool ota_gh__nth_object(const char *json, size_t len, size_t index,
                                       const char **obj_start, const char **obj_end)
{
    const char *end = json + len;
    const char *p = json;
    bool in_str = false;
    int depth = 0;
    const char *cur_start = NULL;
    size_t seen = 0;
    for (; p < end; p++) {
        char c = *p;
        if (in_str) {
            if (c == '\\') { p++; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') {
            if (depth == 0) cur_start = p;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && cur_start) {
                if (seen == index) {
                    *obj_start = cur_start;
                    *obj_end = p + 1;
                    return true;
                }
                seen++;
                cur_start = NULL;
            }
        }
    }
    return false;
}

// Извлекает строковое значение поля "key":"value" (без учёта экранирования
// внутри value, кроме \" -- останавливается на первой неэкранированной
// кавычке). out_len -- длина value (без завершающего нуля). Возвращает
// указатель на начало value внутри исходного буфера, или NULL.
static inline const char *ota_gh__string_field(const char *obj_start, const char *obj_end,
                                                 const char *key, size_t *out_len)
{
    const char *k = ota_gh__find_key(obj_start, obj_end, key);
    if (!k) return NULL;
    const char *p = k + strlen(key);
    while (p < obj_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= obj_end || *p != ':') return NULL;
    p++;
    while (p < obj_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= obj_end || *p != '"') return NULL;
    p++;
    const char *val_start = p;
    while (p < obj_end) {
        if (*p == '\\') { p += 2; continue; }
        if (*p == '"') break;
        p++;
    }
    if (p >= obj_end) return NULL;
    *out_len = (size_t)(p - val_start);
    return val_start;
}

// Извлекает bool-значение поля "key":true|false. Возвращает false при
// отсутствии поля (осторожный дефолт -- как будто draft/prerelease не стоит).
static inline bool ota_gh__bool_field(const char *obj_start, const char *obj_end, const char *key)
{
    const char *k = ota_gh__find_key(obj_start, obj_end, key);
    if (!k) return false;
    const char *p = k + strlen(key);
    while (p < obj_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= obj_end || *p != ':') return false;
    p++;
    while (p < obj_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return (p + 4 <= obj_end) && memcmp(p, "true", 4) == 0;
}

// Перебирает top-level-объекты массива releases, отбрасывает draft всегда и
// (если want_prerelease==false) prerelease, парсит "tag_name" через
// ota_gh_version_parse_tag() и выбирает максимальную версию. На успехе
// возвращает true и заполняет best_obj_start/best_obj_end (границы объекта
// релиза для последующего ota_gh_find_asset_url) и best_ver. false --
// подходящих релизов нет (пустой массив/только draft/только скрытый
// prerelease/ни один тег не распознан).
static inline bool ota_gh_releases_pick_best(const char *json, size_t len, bool want_prerelease,
                                              const char **best_obj_start, const char **best_obj_end,
                                              ota_gh_version_t *best_ver)
{
    bool have_best = false;
    for (size_t i = 0; ; i++) {
        const char *os, *oe;
        if (!ota_gh__nth_object(json, len, i, &os, &oe)) break;
        if (ota_gh__bool_field(os, oe, "\"draft\"")) continue;
        if (!want_prerelease && ota_gh__bool_field(os, oe, "\"prerelease\"")) continue;
        size_t taglen;
        const char *tag = ota_gh__string_field(os, oe, "\"tag_name\"", &taglen);
        if (!tag || taglen == 0 || taglen >= 64) continue;
        char tagbuf[64];
        memcpy(tagbuf, tag, taglen);
        tagbuf[taglen] = '\0';
        ota_gh_version_t v;
        if (!ota_gh_version_parse_tag(tagbuf, &v)) continue;
        if (!have_best || ota_gh_version_cmp(&v, best_ver) > 0) {
            *best_ver = v;
            *best_obj_start = os;
            *best_obj_end = oe;
            have_best = true;
        }
    }
    return have_best;
}

// Внутри объекта релиза [obj_start,obj_end) ищет в подмассиве "assets":[...]
// первый asset-объект с "name":"asset_name" и возвращает его
// "browser_download_url" (out_len -- длина без нуля). NULL, если не найдено.
static inline const char *ota_gh_find_asset_url(const char *obj_start, const char *obj_end,
                                                  const char *asset_name, size_t *out_len)
{
    const char *assets_key = ota_gh__find_key(obj_start, obj_end, "\"assets\"");
    if (!assets_key) return NULL;
    const char *p = assets_key;
    const char *arr_end = obj_end;
    size_t name_len = strlen(asset_name);
    for (size_t i = 0; ; i++) {
        const char *os, *oe;
        if (!ota_gh__nth_object(p, (size_t)(arr_end - p), i, &os, &oe)) break;
        size_t nlen;
        const char *name = ota_gh__string_field(os, oe, "\"name\"", &nlen);
        if (name && nlen == name_len && memcmp(name, asset_name, name_len) == 0) {
            size_t ulen;
            const char *url = ota_gh__string_field(os, oe, "\"browser_download_url\"", &ulen);
            if (url) { *out_len = ulen; return url; }
        }
    }
    return NULL;
}
