/*
 * battery_monitor.h — ADC Battery and Temperature Monitoring API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Provides battery voltage and die temperature readings via the
 * RP2350B's 12-bit ADC. The battery voltage is measured through
 * a resistor divider on PIN_ADC_VBAT (GPIO 0), and the die
 * temperature is read from the internal temperature sensor on
 * ADC channel 4.
 *
 * Voltage divider: VBAT → R1 (100kΩ) → ADC → R2 (100kΩ) → GND
 * ADC reads VBAT/2, so VBAT = ADC_raw × (3.3 × 2) / 4095
 *
 * Brownout detection: When VBAT drops below 3.0V (3000 mV), the
 * firmware marks a brownout flag in the watchdog scratch register
 * and prepares for an imminent power loss.
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* ── Battery monitor constants ──────────────────────────────────────────── */

/** ADC reference voltage in millivolts */
#define BATT_ADC_REF_MV             3300

/** ADC resolution (12-bit) */
#define BATT_ADC_RESOLUTION         4095

/*
 * Voltage divider for battery voltage measurement:
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
 *   VBAT_mV ≈ (ADC_RESULT × 3249 + 500) / 1000
 */
#define BATT_R1_OHM                 100000UL   /* Upper resistor in ohms */
#define BATT_R2_OHM                 33000UL    /* Lower resistor in ohms */
#define BATT_DIVIDER_NUMERATOR      (BATT_R1_OHM + BATT_R2_OHM)
#define BATT_DIVIDER_DENOMINATOR    BATT_R2_OHM

/** Brownout detection threshold in millivolts
 *
 * Set to 2800 mV (below 3.0V critical, above RP2350B minimum 1.8V)
 * to catch deep voltage sags caused by high-current bursts (SDR TX, NFC field).
 */
#define BATT_BROWNOUT_THRESHOLD_MV  2800

/** Brownout recovery hysteresis in millivolts
 *
 * A brownout condition clears only when VBAT rises above
 * BROWNOUT_THRESHOLD + BROWNOUT_HYSTERESIS = 2800 + 200 = 3000 mV.
 */
#define BATT_BROWNOUT_HYSTERESIS_MV 200

/** Brownout recovery threshold in millivolts (computed) */
#define BATT_BROWNOUT_RECOVERY_MV   (BATT_BROWNOUT_THRESHOLD_MV + BATT_BROWNOUT_HYSTERESIS_MV)

/** Number of ADC samples to average for each reading */
#define BATT_ADC_OVERSAMPLE          8

/** Battery full voltage (Li-Po 100%) */
#define BATT_FULL_MV                 4200

/** Battery empty voltage (Li-Po 0%) */
#define BATT_EMPTY_MV                3000

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * battery_monitor_init — Initialize ADC for battery and temperature reading
 *
 * Configures ADC channel 0 (VBAT divider) and channel 4 (internal
 * temperature sensor). Enables the ADC clock and calibrates the
 * internal voltage reference.
 *
 * Returns: 0 on success, negative on error
 */
int battery_monitor_init(void);

/**
 * battery_monitor_update — Trigger a new ADC reading cycle
 *
 * Starts an oversampled ADC conversion on both VBAT and temperature
 * channels. The results are cached and available via
 * battery_monitor_get_vbat_mv() and battery_monitor_get_temp_c_x10().
 * Should be called every 1 second from the main loop.
 */
void battery_monitor_update(void);

/**
 * battery_monitor_get_vbat_mv — Get the latest battery voltage in mV
 *
 * Returns the averaged ADC reading converted to millivolts.
 * If no reading has been taken yet, returns 0.
 *
 * Returns: Battery voltage in mV (3000–4200 typical)
 */
uint16_t battery_monitor_get_vbat_mv(void);

/**
 * battery_monitor_get_temp_c_x10 — Get the latest die temperature
 *
 * Returns the RP2350B die temperature in °C × 10.
 * Uses the internal temperature sensor with oversampling.
 *
 * Returns: Temperature in 0.1 °C units (e.g., 275 = 27.5 °C)
 */
int16_t battery_monitor_get_temp_c_x10(void);

/**
 * battery_is_brownout — Check if the battery voltage indicates a brownout
 *
 * Uses hysteresis to prevent oscillation near the threshold:
 *   - Enters brownout when VBAT < 3000 mV
 *   - Exits brownout when VBAT > 3300 mV
 *
 * @vbat_mv:        Current battery voltage in mV
 * @brownout_active: Pointer to persistent brownout state (must be static)
 *                   Updated in-place with the new state.
 *
 * Returns: true if a brownout condition is active (VBAT < threshold)
 */
bool battery_is_brownout(uint16_t vbat_mv, bool *brownout_active);

/**
 * battery_voltage_to_percent — Convert battery voltage to charge percentage
 *
 * Uses a piecewise linear approximation of the Li-Po discharge curve:
 *   4200 mV = 100%
 *   3700 mV = 50%
 *   3300 mV = 10%
 *   3000 mV = 0%
 *
 * @vbat_mv: Battery voltage in mV
 * Returns: Estimated charge percentage (0–100)
 */
uint8_t battery_voltage_to_percent(uint16_t vbat_mv);

#endif /* BATTERY_MONITOR_H */