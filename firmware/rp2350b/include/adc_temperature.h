/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 jayis1 */
#ifndef ADC_TEMPERATURE_H
#define ADC_TEMPERATURE_H

#include <stdint.h>

/* Convert a 12-bit ADC sample to tenths of a degree Celsius.
 * T = 27 - (V - 0.706) / 0.001721, with a nominal 3.3 V reference.
 * Widen before multiplying: raw * 3300000 exceeds 32 bits. Keep the
 * temperature delta in tenths, including its sign, until final division.
 */
static inline int16_t adc_temperature_dc10(uint16_t raw)
{
    int64_t delta_uv = (int64_t)raw * 3300000 / 4095 - 706000;

    return (int16_t)(270 - delta_uv * 10 / 1721);
}

#endif /* ADC_TEMPERATURE_H */
