// AWF-2a финал (решение оператора 25.09): чистое представление того, что
// означает отсутствующий ключ boot-флага в NVS — тестируется на хосте без
// NVS/ESP-IDF. Используется boot_config.c::get_flag() для ЛЮБОГО флага
// (autostart_spectrum, clear_spectrum, ..., field_ap_fallback_enabled);
// критично именно для field_ap_fallback_enabled — решение оператора: у этой
// настройки отсутствующий ключ (апгрейд со старой прошивки, где её не было)
// обязан дать ВЫКЛ, не ВКЛ.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// #DOSE-TOGGLE: положительный флаг с умолчанием ВКЛ (живая МД, NVS-ключ dose_live_en): ключа нет (апгрейд, чистая NVS,
// ошибка чтения) = ВКЛ при любом raw_value; ключ есть — по значению (0 = ВЫКЛ). Обратная семантика к boot_flag_from_nvs().
static inline bool boot_flag_default_on_from_nvs(bool key_present, uint8_t raw_value)
{
    return !key_present || raw_value != 0;
}

static inline bool boot_flag_from_nvs(bool key_present, uint8_t raw_value)
{
    return key_present && raw_value != 0;
}
