#pragma once
#include <stdbool.h>
#include <string.h>
#include "atomspectra.h"
/* 1.2.36 H12: поля хвоста снимка для /api/device (~110 Б на стеке httpd вместо malloc 33 КБ). */
typedef struct {
    double calibration[CALIB_COEFFS];
    int    calib_order;
    bool   calib_valid;
    bool   valid;
    char   serial_number[64];
} spectrum_calib_meta_t;
/* Снимок этих полей под SPEC_LOCK (spectrum.c); true == спектр валиден. bins не копируются. */
bool spectrum_get_calib_meta(spectrum_calib_meta_t *m);
static inline void spectrum_calib_meta_from(const spectrum_data_t *s, spectrum_calib_meta_t *m)
{
    memcpy(m->calibration, s->calibration, sizeof(m->calibration));
    m->calib_order = s->calib_order;
    m->calib_valid = s->calib_valid;
    m->valid = s->valid;
    memcpy(m->serial_number, s->serial_number, sizeof(m->serial_number));
}
