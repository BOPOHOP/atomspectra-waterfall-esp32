#pragma once
// #AWF-F4: решение «какой файл каталога SPEC_DIR удалить на старте как осиротевший tmp импорта».
// Чистая функция без ESP-IDF (host-тест: tests/host/test_import_tmp_plan.c).
// Удаляется ТОЛЬКО точное имя import.tmp (его пишет spectrum_import_to_flash через
// atomic_write_snapshot). Всё прочее (spec_NNNN.bin, saved.html, import.tmp.bak, ...) — не наше.

#include <stdbool.h>
#include <string.h>

#define IMPORT_TMP_NAME "import.tmp"

static inline bool import_tmp_is_orphan(const char *name)
{
    return name != NULL && strcmp(name, IMPORT_TMP_NAME) == 0;
}
