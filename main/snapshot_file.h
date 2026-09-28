#pragma once
#include <stdbool.h>
#include <stdio.h>

/* У4 (раунд 3): слепок пишется во временный файл и заменяет прежний только целиком
 * записанным (rename атомарен): сбой записи оставляет прежний слепок нетронутым, а GET
 * не может отдать обрезанный файл. true — новый слепок на месте. */
static inline bool snapshot_file_write_atomic(const char *tmp_path, const char *final_path,
        const char *stamp, const char *info_line, const char *tcpot_line)
{
    FILE *f = fopen(tmp_path, "w");
    bool ok = f != NULL;
    if (ok) ok = fprintf(f, "# AtomSpectra DSP snapshot %s\r\n", stamp) > 0;
    if (ok) ok = fputs(info_line, f) >= 0 && fputs("\r\n", f) >= 0;
    if (ok) ok = fputs(tcpot_line, f) >= 0 && fputs("\r\n", f) >= 0;
    if (f) ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(tmp_path, final_path) == 0;
    if (!ok) remove(tmp_path);
    return ok;
}
