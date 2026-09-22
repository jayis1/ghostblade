/*
 * adc_calibration.h — ADC Calibration and Voltage Divider Compensation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Provides ADC self-calibration, multi-channel differential sampling,
 * and voltage divider compensation for accurate battery voltage and
 * temperature readings.
 *
 * The RP2350B ADC has 12-bit resolution (0-4095) with a 3.3V reference.
 * The battery voltage is measured through a resistor divider:
 *
 *   VBAT ──[R1=100kΩ]──┬──[R2=33kΩ]── GND
 *                        │
 *                     ADC0 (GPIO 26)
 *
 * Production units require per-board ADC calibration to compensate for:
 *   - Resistor divider tolerance (±1% for 100kΩ/33kΩ)
 *   - ADC reference voltage variation (±2% for 3.3V)
 *   - ADC offset and gain errors
 *   - Temperature drift of the voltage divider
 *
 * Calibration procedure:
 *   1. Apply a known precision voltage (e.g., 3.600V ±0.01%) to VBAT
 *   2. Read ADC raw value at that voltage
 *   3. Compute the calibration coefficient
 *   4. Store the coefficient in flash for subsequent boots
 *
 * This module also provides temperature compensation for the divider,
 * since the resistor values drift with temperature:
 *   - R1 (100kΩ): ±100 ppm/°C
 *   - R2 (33kΩ): ±100 ppm/°C
 *   - Net effect: ~0.02%/°C, negligible below 60°C
 */

#ifndef ADC_CALIBRATION_H
#define ADC_CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>

/* ── ADC calibration constants ──────────────────────────────────────────── */

/** ADC reference voltage in millivolts (nominal 3.3V) */
#define ADC_CAL_REF_MV                 3300

/** ADC full-scale value (12-bit) */
#define ADC_CAL_FULL_SCALE             4095

/** Voltage divider resistors (ohms) */
#define ADC_CAL_R1_OHM                 100000UL
#define ADC_CAL_R2_OHM                 33000UL

/** Computed divider ratio: VBAT = V_ADC × (R1 + R2) / R2 */
#define ADC_CAL_DIVIDER_RATIO          ((ADC_CAL_R1_OHM + ADC_CAL_R2_OHM) / \
                                         (float)ADC_CAL_R2_OHM)

/**
 * Ideal multiplier: VBAT_mV = ADC_raw × ADC_CAL_VBAT_MULTIPLIER / 1000
 *
 * VBAT = ADC × VREF / 4095 × (R1 + R2) / R2
 * VBAT_mV = ADC × 3300 / 4095 × 133000 / 33000
 * VBAT_mV ≈ ADC × 3249.7 / 1000
 *
 * Using integer math: VBAT_mV = (ADC_raw × 3249700 + 500) / 1000000
 */
#define ADC_CAL_VBAT_NUMERATOR         3249700UL
#define ADC_CAL_VBAT_DENOMINATOR       1000000UL
#define ADC_CAL_VBAT_ROUNDING          500UL

/** Number of ADC samples to average for calibration measurement */
#define ADC_CAL_CALIBRATION_SAMPLES    64

/** Number of ADC channels to calibrate */
#define ADC_CAL_CHANNEL_COUNT          2

/* ── Calibration data structure ─────────────────────────────────────────── */

/**
 * struct adc_cal_coeffs — Per-board ADC calibration coefficients
 *
 * Stored in flash after factory calibration. Each coefficient adjusts
 * the raw ADC reading to compensate for board-specific variations.
 *
 * @vbat_offset_mv:    Offset correction for VBAT channel (mV)
 * @vbat_gain_x1000:   Gain correction (multiplied by 1000, 1000 = unity)
 * @temp_offset_dcx10: Offset correction for temperature (°C × 10)
 * @temp_gain_x1000:   Gain correction for temperature (× 1000, 1000 = unity)
 * @calibrated:        True if calibration data is valid
 * @cal_version:        Calibration data format version (1 = current)
 * @reserved:          Reserved for future use (must be 0)
 */
struct adc_cal_coeffs {
    int16_t  vbat_offset_mv;
    uint16_t vbat_gain_x1000;
    int16_t  temp_offset_dcx10;
    uint16_t temp_gain_x1000;
    bool     calibrated;
    uint8_t  cal_version;
    uint16_t reserved;
};

/* ── Calibration magic values ──────────────────────────────────────────── */

/** Magic number stored with calibration data to identify valid records */
#define ADC_CAL_MAGIC                  0xADCA  /* "ADCA" */

/**
 * FLASH_CAL_OFFSET — Byte offset from XIP_BASE for the calibration sector
 *
 * The calibration sector occupies one 4 KB flash sector at this XIP offset.
 * The physical QSPI flash address is XIP_BASE + FLASH_CAL_OFFSET.
 *
 * This address is well above typical GhostBlade firmware size (~600 KB)
 * and within the 16 MB QSPI flash window.  It is only used by
 * adc_cal_init() (load) and adc_cal_factory_calibrate() (store).
 *
 * If the firmware binary grows past this offset, the linker script
 * (rp2350b_memmap.ld) must be updated to move the calibration sector.
 */
#define FLASH_CAL_OFFSET               0x10F000UL

/**
 * struct adc_cal_record — Flash-stored calibration record
 *
 * Written to flash during factory calibration. Read at boot time.
 * Uses a magic number and checksum for data integrity verification.
 *
 * @magic:      ADC_CAL_MAGIC if valid
 * @version:    Calibration data format version (must be 1)
 * @coeffs:     Calibration coefficients
 * @timestamp:  Unix timestamp of calibration (seconds since epoch)
 * @checksum:   Simple additive checksum of all preceding bytes
 */
struct adc_cal_record {
    uint16_t magic;
    uint8_t  version;
    struct adc_cal_coeffs coeffs;
    uint32_t timestamp;
    uint8_t  checksum;
};

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * adc_cal_init — Initialize ADC calibration module
 *
 * Attempts to load per-board factory calibration coefficients from flash
 * sector 0x10F000 (XIP offset FLASH_CAL_OFFSET).  The record is validated
 * against ADC_CAL_MAGIC, version 1, and an 8-bit additive checksum before
 * the coefficients are accepted.
 *
 * If no valid calibration record exists (first boot, erased flash, or a
 * corrupted record), unity-gain / zero-offset defaults are used and the
 * module operates in uncalibrated mode.
 *
 * Callers may check adc_cal_is_calibrated() after init to determine whether
 * factory calibration data was successfully loaded.
 *
 * Returns:  1  — factory calibration loaded from flash
 *           0  — no valid flash record; running with default coefficients
 *          <0  — reserved for future flash-read error codes
 */
int adc_cal_init(void);

/**
 * adc_cal_is_calibrated — Query whether factory calibration is active
 *
 * Returns: true if cal_coeffs were loaded from a valid flash record,
 *          false if the module is using default (uncalibrated) coefficients.
 */
static inline bool adc_cal_is_calibrated(void) {
    struct adc_cal_coeffs c;
    extern void adc_cal_get_coeffs(struct adc_cal_coeffs *out);
    adc_cal_get_coeffs(&c);
    return c.calibrated;
}

/**
 * adc_cal_apply_vbat — Apply calibration to a raw VBAT ADC reading
 *
 * Converts raw ADC value to millivolts using stored calibration
 * coefficients. The conversion formula is:
 *
 *   VBAT_mV = (raw × VREF × divider_ratio / 4095 + offset) × gain / 1000
 *
 * In integer arithmetic with rounding:
 *   VBAT_mV = ((raw × 3249700 + 500) / 1000000 + vbat_offset_mv)
 *             × vbat_gain_x1000 / 1000
 *
 * @raw_mv: Raw battery voltage in mV (from battery_monitor_get_vbat_mv)
 * Returns: Calibrated battery voltage in mV
 */
uint16_t adc_cal_apply_vbat(uint16_t raw_mv);

/**
 * adc_cal_apply_temp — Apply calibration to a raw temperature reading
 *
 * Applies offset and gain corrections to the die temperature reading.
 *
 *   temp_c_x10_cal = (raw_temp_c_x10 × temp_gain_x1000 / 1000) + temp_offset_dcx10
 *
 * @raw_temp_c_x10: Raw temperature in °C × 10 (from battery_monitor_get_temp_c_x10)
 * Returns: Calibrated temperature in °C × 10
 */
int16_t adc_cal_apply_temp(int16_t raw_temp_c_x10);

/**
 * adc_cal_get_coeffs — Get the current calibration coefficients
 *
 * Returns a copy of the active calibration coefficients. Useful for
 * diagnostics and telemetry.
 *
 * @out: Pointer to output structure (must not be NULL)
 */
void adc_cal_get_coeffs(struct adc_cal_coeffs *out);

/**
 * adc_cal_factory_calibrate — Perform factory ADC calibration
 *
 * Two-point calibration using a precision voltage source:
 *   1. With VBAT = vbat_low_mv applied, read 64-sample ADC average.
 *   2. With VBAT = vbat_high_mv applied, read 64-sample ADC average.
 *   3. Compute linear offset and gain corrections from the two points.
 *   4. Persist the calibration record to flash at FLASH_CAL_OFFSET via
 *      adc_cal_store_to_flash():
 *        a. Assemble adc_cal_record (magic + version + coeffs + timestamp
 *           + additive checksum) in SRAM.
 *        b. Disable interrupts (required by Pico SDK flash APIs).
 *        c. flash_range_erase(FLASH_CAL_OFFSET, FLASH_SECTOR_SIZE).
 *        d. flash_range_program(FLASH_CAL_OFFSET, buf, FLASH_PAGE_SIZE).
 *        e. Re-enable interrupts.
 *        f. Read the written record back via XIP and re-validate.
 *
 * Prerequisites:
 *   - A precision voltage source (≤0.1% accuracy) must be connected to VBAT.
 *   - The watchdog must be kicked immediately before this call — the combined
 *     erase (~50 ms) + program (~0.5 ms) window runs with interrupts disabled
 *     and will exceed typical watchdog kick intervals if the watchdog is tight.
 *   - vbat_low_mv and vbat_high_mv must be distinct (vbat_high_mv > vbat_low_mv).
 *
 * @vbat_low_mv:  Known low reference voltage in mV (e.g., 3300 mV)
 * @vbat_high_mv: Known high reference voltage in mV (e.g., 4100 mV)
 *
 * Returns:  0  — success; calibration stored in flash and active
 *          -1  — vbat_low == vbat_high (division by zero guard), or struct
 *                too large for one flash page (developer error)
 *          -2  — flash readback validation failed (flash may be faulty)
 */
int adc_cal_factory_calibrate(uint16_t vbat_low_mv, uint16_t vbat_high_mv);

/**
 * adc_cal_self_test — Run ADC self-test
 *
 * Verifies ADC functionality by:
 *   1. Reading the internal voltage reference (should be ~1.2V)
 *   2. Reading the temperature sensor (should be 0-85°C)
 *   3. Checking that VBAT reading is within plausible range (2800-4500 mV)
 *
 * Returns: 0 if all tests pass, negative error code on failure
 *   -1: Internal reference out of range
 *   -2: Temperature reading out of range
 *   -3: VBAT reading out of range
 */
int adc_cal_self_test(void);

/**
 * adc_cal_read_vref_int — Read the ADC internal voltage reference
 *
 * The RP2350B has an internal 1.2V reference that can be measured
 * through the ADC. This is useful for checking ADC health and
 * computing the actual VREF voltage for more accurate conversions.
 *
 * Returns: Internal reference voltage in mV (nominal 1200 mV)
 */
uint16_t adc_cal_read_vref_int(void);

/**
 * adc_cal_compute_vref — Compute actual VREF voltage from internal reference
 *
 * Uses the internal 1.2V reference reading to compute the actual
 * VREF voltage. This compensates for VREF variations across
 * temperature and supply voltage:
 *
 *   VREF_actual = 1200 × 4095 / ADC_vref_raw
 *
 * @return: Actual VREF voltage in mV (nominal 3300 mV)
 */
uint16_t adc_cal_compute_vref(void);

#endif /* ADC_CALIBRATION_H */