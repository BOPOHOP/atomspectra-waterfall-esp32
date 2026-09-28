#pragma once
#include <stdint.h>

// P-042 (sweep-A, 1.2.28): восстановление глобального номера сегмента seg_seq (#DATA-1b)
// после потери NVS. firmware.factory.bin (merge_bin от 0x0) накрывает раздел nvs —
// прошивка по 0x0 стирает NVS, settings_load() не находит "seg_seq", s_seg_seq = 0, и
// новые сегменты получали 1, 2, 3... поверх живых на flash (стенд 14.09: ~1983 -> 3, 4, 5).
// Правило: при старте номер = max(NVS, максимум seg_seq по шапкам живых сегментов).
// Максимум берётся ВСЕГДА, не только при пустом NVS: номер монотонный, и NVS меньше
// шапки = потеря/отставание NVS. seg_open_new делает s_seg_seq++ до сборки шапки ->
// следующий сегмент получит max+1.
// Предел: пустой каталог сегментов + стёртый NVS — восстанавливать не из чего.

static inline uint32_t wf_seg_seq_fold_max(uint32_t acc, uint32_t hdr_seq) {
    // Свёртка максимума по шапкам (0 = поля нет).
    if (hdr_seq > acc) {
        return hdr_seq;
    }
    return acc;
}

static inline uint32_t wf_seg_seq_resume(uint32_t nvs_seq, uint32_t flash_max_seq) {
    // Номер, с которого продолжать: max(NVS, flash).
    if (flash_max_seq > nvs_seq) {
        return flash_max_seq;
    }
    return nvs_seq;
}
