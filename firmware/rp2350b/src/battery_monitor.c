/*
 * battery_monitor.c — ADC Battery & Temperature Monitor for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements battery voltage monitoring and die temperature
 * reading using the RP2350B's onboard ADC peripheral.
 *
 * Hardware setup:
 *   - Battery voltage: 3.7V-4.2V Li-Po via resistor divider on ADC0
 *     R1 = 100kΩ (VBAT → ADC0), R2 = 33kΩ (ADC0 → GND)
 *     V_ADC = VBAT × R2 / (R1 + R2) = VBAT × 33/133 = VBAT × 0.248
 *     At VBAT=4.2V: V_ADC = 1.043V (within 0-3.3V ADC range)
 *     At VBAT=3.0V: V_ADC = 0.744V
 *
 *   - Die temperature: Internal temperature sensor on ADC4
 *     Formula: T = 27°C - (V_TEMP - 0.706V) / 0.001721 V/°C
 *     (RP2350B datasheet, Section 4.9.3)
 *
 * Reference: RP2350B Datasheet, Section 4 (ADC)
 */

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * RP2350B ADC Register Base
 * ======================================================================== */

#define RP2350B_ADC_BASE      0x50041000UL

#define ADC_CS                0x00   /* Control and Status */
#define ADC_RESULT            0x04   /* Conversion Result */
#define ADC_FCS               0x08   /* FIFO Control and Status */
#define ADC_FIFO              0x0C   /* FIFO Data */
#define ADC_DIV               0x10   /* Clock Divider */

/* ADC_CS bits */
#define ADC_CS_EN             (1 << 0)    /* ADC enable */
#define ADC_CS_TS_EN          (1 << 1)    /* Temperature sensor enable */
#define ADC_CS_START_ONCE     (1 << 2)    /* Start one conversion */
#define ADC_CS_START_MANY     (1 << 3)    /* Start continuous conversions */
#define ADC_CS_READY          (1 << 8)    /* Conversion ready */
#define ADC_CS_ERR_STICKY     (1 << 10)  /* Sticky error flag */

/* ADC_FCS bits */
#define ADC_FCS_DREQ_EN       (1 << 0)   /* DMA request enable */
#define ADC_FCS_THRESH_SHIFT  24         /* FIFO threshold */
#define ADC_FCS_EMPTY         (1 << 8)   /* FIFO empty */
#define ADC_FCS_FULL          (1 << 9)   /* FIFO full */
#define ADC_FCS_LEVEL_SHIFT   16         /* FIFO level */

/* Channel selection (in ADC_CS bits 12:17) */
#define ADC_CH0               0   /* GPIO 26 = ADC0 (battery voltage) */
#define ADC_CH4               4   /* Internal temperature sensor */

/* ========================================================================
 * Voltage Divider Constants
 * ======================================================================== */

/*
 * Resistor divider for battery voltage measurement:
 *
 *   VBAT ──[R1=100kΩ]──┬──[R2=33kΩ]── GND
 *                       │
 *                    ADC0 (GPIO 26)
 *
 * V_ADC = VBAT × R2 / (R1 + R2)
 * VBAT  = V_ADC × (R1 + R2) / R2
 *
 * With R1=100kΩ and R2=33kΩ:
 *   Divider ratio = (100000 + 33000) / 33000 = 133000 / 33000 = 4.0303
 *
 * For a 12-bit ADC with VREF = 3.3V:
 *   V_ADC = ADC_RESULT × 3.3 / 4095
 *   VBAT  = V_ADC × 4.0303
 *   VBAT  = ADC_RESULT × 3.3 × 4.0303 / 4095
 *   VBAT  = ADC_RESULT × 0.003249 V/bit
 *   VBAT_mV = ADC_RESULT × 3.249
 *
 * For efficiency, we use fixed-point arithmetic:
 *   VBAT_mV ≈ (ADC_RESULT × 3249) / 1000
 *   Or more precisely: VBAT_mV = (ADC_RESULT × 3249 + 500) / 1000
 */

#define VBAT_DIVIDER_NUM      133000UL   /* R1 + R2 in ohms */
#define VBAT_DIVIDER_DEN      33000UL    /* R2 in ohms */
#define VBAT_ADC_VREF_MV      3300UL    /* ADC reference voltage in mV */
#define VBAT_ADC_MAX          4095UL    /* 12-bit ADC full scale */

/* Pre-computed: VBAT_mV = ADC_RESULT × VBAT_SCALE_NUM / VBAT_SCALE_DEN */
#define VBAT_SCALE_NUM        (VBAT_ADC_VREF_MV * VBAT_DIVIDER_NUM)
#define VBAT_SCALE_DEN        (VBAT_ADC_MAX * VBAT_DIVIDER_DEN)
/* = 3300 × 133000 / (4095 × 33000) = 438900000 / 135135000 ≈ 3.2488 */

/* Simplified scale factor (mV per LSB): ~3.249 */
#define VBAT_MV_PER_LSB_X1000   3249   /* mV × 1000 per LSB */

/* ========================================================================
 * Temperature Sensor Constants
 * ======================================================================== */

/*
 * RP2350B internal temperature sensor:
 *   V_TEMP = 0.706V at 27°C (T0)
 *   Slope: -1.721 mV/°C (V_TEMP decreases with temperature)
 *
 * T = 27 - (V_TEMP - 0.706) / 0.001721
 *
 * For ADC reading:
 *   V_TEMP = ADC_RESULT × 3.3 / 4095
 *   T = 27 - (ADC_RESULT × 3.3 / 4095 - 0.706) / 0.001721
 *
 * Fixed-point calculation:
 *   V_TEMP_mV = ADC_RESULT × 3300 / 4095
 *   T_cC = 27000 - (V_TEMP_mV - 706) × 1000 / 1.721
 *
 * Simplified for integer math:
 *   T_cC = 27000 - (ADC_RESULT × 3300000 / 4095 - 706000) / 1721
 */

#define TEMP_T0_C             27       /* Reference temperature in °C */
#define TEMP_V0_MV            706     /* Voltage at T0 in mV */
#define TEMP_SLOPE_UV_PER_C   1721    /* Slope in μV/°C (absolute value) */

/* ========================================================================
 * ADC Access Helpers
 * ======================================================================== */

#define REG32(addr)           (*(volatile uint32_t *)(addr))

/* ========================================================================
 * Low-Level ADC Operations
 * ======================================================================== */

/**
 * adc_read_channel — Perform a single ADC conversion on the specified channel
 *
 * @channel: ADC channel number (0-4, where 4 = temperature sensor)
 * Returns: 12-bit ADC result (0-4095)
 */
static uint16_t adc_read_channel(uint8_t channel) {
    volatile uint32_t *adc_cs = (volatile uint32_t *)(RP2350B_ADC_BASE + ADC_CS);
    volatile uint32_t *adc_result = (volatile uint32_t *)(RP2350B_ADC_BASE + ADC_RESULT);

    /* Select channel (bits 12-17 in CS) */
    uint32_t cs_val = *adc_cs;
    cs_val &= ~(0x1F << 12);       /* Clear channel select */
    cs_val |= (channel << 12);     /* Set new channel */

    /* Enable temperature sensor if reading channel 4 */
    if (channel == 4)
        cs_val |= ADC_CS_TS_EN;
    else
        cs_val &= ~ADC_CS_TS_EN;

    *adc_cs = cs_val;

    /* Start single conversion */
    *adc_cs |= ADC_CS_START_ONCE;

    /* Wait for conversion to complete */
    while (!(*adc_cs & ADC_CS_READY))
        ;

    /* Read result (12-bit value, bits 0-11) */
    return (uint16_t)(*adc_result & 0xFFF);
}

/**
 * adc_read_channel_averaged — Read an ADC channel with N-sample averaging
 *
 * @channel: ADC channel number
 * @samples: Number of samples to average (1-16)
 * Returns: Averaged 12-bit ADC result
 */
static uint16_t adc_read_channel_averaged(uint8_t channel, uint8_t samples) {
    uint32_t sum = 0;
    uint8_t n = (samples < 1) ? 1 : ((samples > 16) ? 16 : samples);

    for (uint8_t i = 0; i < n; i++) {
        sum += adc_read_channel(channel);
    }

    return (uint16_t)(sum / n);
}

/* ========================================================================
 * Battery Voltage Measurement
 * ======================================================================== */

/**
 * battery_read_voltage_mv — Read the battery voltage in millivolts
 *
 * Uses the resistor divider network to measure the Li-Po battery voltage.
 * The ADC reads the divided voltage and the result is scaled back up
 * using the divider ratio.
 *
 * The battery voltage range is 3.0V-4.2V (Li-Po operating range).
 * Low battery threshold: 3.3V (about 10% remaining capacity)
 * Critical threshold: 3.0V (must shut down)
 *
 * Returns: Battery voltage in mV (e.g., 3850 = 3.85V)
 */
uint16_t battery_read_voltage_mv(void) {
    uint16_t adc_raw;
    uint32_t vbat_mv;

    /* Read ADC channel 0 with 8-sample averaging for noise reduction */
    adc_raw = adc_read_channel_averaged(0, 8);

    /* Calculate battery voltage using fixed-point arithmetic:
     *
     * VBAT_mV = ADC_RAW × VBAT_SCALE_NUM / VBAT_SCALE_DEN
     *
     * To avoid overflow with 32-bit integers:
     *   VBAT_SCALE_NUM = 3300 × 133000 = 438,900,000
     *   VBAT_SCALE_DEN = 4095 × 33000  = 135,135,000
     *
     * Simplify: divide both by 15000:
     *   NUM = 29,260, DEN = 9,009
     *
     * Further: GCD(29260, 9009) ≈ 1 (coprime)
     *
     * So: VBAT_mV = ADC_RAW × 29260 / 9009
     *
     * For ADC_RAW up to 4095: max product = 4095 × 29260 = 119,739,700
     * This fits in uint32_t (max 4,294,967,295). Good.
     */
    vbat_mv = ((uint32_t)adc_raw * 29260UL + 4504UL) / 9009UL;
    /* +4504 is half of 9009 for rounding */

    return (uint16_t)vbat_mv;
}

/**
 * battery_get_percent — Estimate battery charge percentage
 *
 * Uses a simple linear model based on Li-Po discharge curve.
 * This is approximate; for accurate SOC, use a fuel gauge IC.
 *
 * @vbat_mv: Battery voltage in mV
 * Returns: Estimated charge percentage (0-100)
 */
uint8_t battery_get_percent(uint16_t vbat_mv) {
    /* Simple linear model for Li-Po:
     * 4.2V = 100%
     * 3.7V = 50%  (nominal)
     * 3.3V = 10%  (low battery warning)
     * 3.0V = 0%   (cutoff)
     *
     * Using piecewise linear approximation:
     */
    if (vbat_mv >= 4200)
        return 100;
    if (vbat_mv <= 3000)
        return 0;

    if (vbat_mv >= 3700) {
        /* 3.7V - 4.2V → 50% - 100% */
        return (uint8_t)((uint32_t)(vbat_mv - 3700) * 50 / 500 + 50);
    } else if (vbat_mv >= 3300) {
        /* 3.3V - 3.7V → 10% - 50% */
        return (uint8_t)((uint32_t)(vbat_mv - 3300) * 40 / 400 + 10);
    } else {
        /* 3.0V - 3.3V → 0% - 10% */
        return (uint8_t)((uint32_t)(vbat_mv - 3000) * 10 / 300);
    }
}

/**
 * battery_is_low — Check if battery is below low threshold
 *
 * @vbat_mv: Battery voltage in mV
 * Returns: true if battery is low (< 3300 mV)
 */
bool battery_is_low(uint16_t vbat_mv) {
    return vbat_mv < 3300;
}

/**
 * battery_is_critical — Check if battery is at critical level
 *
 * @vbat_mv: Battery voltage in mV
 * Returns: true if battery is critical (< 3000 mV)
 */
bool battery_is_critical(uint16_t vbat_mv) {
    return vbat_mv < 3000;
}

/* ========================================================================
 * Temperature Measurement
 * ======================================================================== */

/**
 * temperature_read_dc10 — Read the RP2350B die temperature
 *
 * Uses the internal temperature sensor (ADC channel 4).
 *
 * Returns: Temperature in °C × 10 (e.g., 275 = 27.5°C)
 */
int16_t temperature_read_dc10(void) {
    uint16_t adc_raw;
    int32_t temp_dc10;

    /* Read ADC channel 4 (temperature sensor) with 8-sample averaging */
    adc_raw = adc_read_channel_averaged(4, 8);

    /* Calculate temperature using fixed-point arithmetic:
     *
     * V_TEMP_mV = ADC_RAW × 3300 / 4095
     * T_cC = 27000 - (V_TEMP_mV - 706) × 1000000 / 1721
     *
     * Simplified:
     * V_TEMP_uV = ADC_RAW × 3300000 / 4095
     * T_cC = 27000 - (V_TEMP_uV - 706000) / 1721
     *
     * Step by step:
     * V_TEMP_uV = ADC_RAW × 806  (approximately: 3300000/4095 ≈ 805.9)
     * delta_uV = V_TEMP_uV - 706000
     * delta_cC = delta_uV / 1721  (can be negative)
     * T_cC = 27000 - delta_cC
     * T_dc10 = T_cC / 10
     */
    int32_t v_temp_uv = (int32_t)adc_raw * 806;  /* μV, approximate */
    int32_t delta_uv = v_temp_uv - 706000;        /* μV difference from T0 */
    int32_t delta_dc = delta_uv / 1721;            /* °C × 10 difference from T0 */
    temp_dc10 = 270 - delta_dc;                    /* °C × 10 */

    return (int16_t)temp_dc10;
}

/**
 * temperature_is_overtemp — Check if die temperature exceeds safe limit
 *
 * @temp_dc10: Temperature in °C × 10
 * Returns: true if temperature exceeds 85°C (thermal throttle threshold)
 */
bool temperature_is_overtemp(int16_t temp_dc10) {
    return temp_dc10 > 850;  /* 85.0°C */
}

/**
 * temperature_is_critical — Check if die temperature is critically high
 *
 * @temp_dc10: Temperature in °C × 10
 * Returns: true if temperature exceeds 105°C (hard shutdown threshold)
 */
bool temperature_is_critical(int16_t temp_dc10) {
    return temp_dc10 > 1050;  /* 105.0°C */
}

/* ========================================================================
 * Combined Monitor
 * ======================================================================== */

/**
 * battery_monitor_read — Read all battery and temperature values at once
 *
 * @vbat_mv:   Output: Battery voltage in mV
 * @temp_dc10: Output: Die temperature in °C × 10
 * @batt_pct:  Output: Estimated battery percentage (0-100)
 * @flags:     Output: Status flags (low battery, overtemp, etc.)
 *
 * Flags:
 *   Bit 0: Battery low (< 3.3V)
 *   Bit 1: Battery critical (< 3.0V)
 *   Bit 2: Overtemperature (> 85°C)
 *   Bit 3: Critical temperature (> 105°C)
 */
void battery_monitor_read(uint16_t *vbat_mv, int16_t *temp_dc10,
                           uint8_t *batt_pct, uint8_t *flags) {
    uint16_t vbat = battery_read_voltage_mv();
    int16_t temp = temperature_read_dc10();
    uint8_t pct = battery_get_percent(vbat);
    uint8_t f = 0;

    if (battery_is_low(vbat))      f |= (1 << 0);
    if (battery_is_critical(vbat)) f |= (1 << 1);
    if (temperature_is_overtemp(temp)) f |= (1 << 2);
    if (temperature_is_critical(temp)) f |= (1 << 3);

    if (vbat_mv)    *vbat_mv   = vbat;
    if (temp_dc10)  *temp_dc10 = temp;
    if (batt_pct)   *batt_pct  = pct;
    if (flags)      *flags     = f;
}