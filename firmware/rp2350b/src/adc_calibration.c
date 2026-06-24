/*
 * adc_calibration.c — ADC Calibration and Voltage Divider Compensation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Implements ADC self-calibration, multi-channel differential sampling,
 * and voltage divider compensation for accurate battery voltage and
 * temperature readings on the RP2350B.
 *
 * The RP2350B's ADC has typical ±2 LSB offset error and ±1% gain error.
 * Combined with ±1% resistor tolerance in the voltage divider, this
 * gives a worst-case error of ~4.1% without calibration, or ~0.5% with
 * two-point calibration.
 *
 * Calibration is stored in flash (last sector) and persists across
 * power cycles. If no calibration data is found, nominal values are
 * used (offset = 0, gain = 1.000×).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "adc_calibration.h"
#include "board_pins.h"

/* ── Flash storage for calibration data ───────────────────────────────────── */

/*
 * Calibration data is stored in the last 4 KiB sector of flash.
 * The RP2350B has 2 MiB of flash; the calibration sector starts at
 * offset (2 MiB - 4 KiB) = 0x1FF000.
 *
 * WARNING: Writing to flash requires erasing the entire sector (4 KiB).
 * This means we must read-modify-write if other data shares the sector.
 * In practice, the calibration record is the only data in this sector,
 * so we can erase and rewrite directly.
 */
#define ADC_CAL_FLASH_OFFSET    0x1FF000
#define ADC_CAL_FLASH_SIZE      4096    /* One sector */

/* ── Module state ─────────────────────────────────────────────────────────── */

static struct {
    struct adc_cal_coeffs coeffs;
    bool loaded;             /* True if calibration data has been loaded */
    uint16_t vref_mv;        /* Computed actual VREF in mV */
} cal_state;

/* ── Internal ADC helpers ──────────────────────────────────────────────────── */

/**
 * adc_read_channel_averaged — Read an ADC channel with oversampling
 *
 * Performs num_samples reads and returns the average. This reduces
 * noise by a factor of sqrt(num_samples).
 *
 * @channel: ADC channel number (0-4)
 * @num_samples: Number of samples to average (1-256)
 *
 * Returns: Averaged raw ADC value (0-4095)
 */
static uint16_t adc_read_channel_averaged(uint channel, uint num_samples)
{
    uint32_t sum = 0;
    uint i;

    if (channel > 4 || num_samples == 0)
        return 0;

    /* Select the ADC channel */
    adc_select_input(channel);

    /* Discard first reading (may be stale after channel switch) */
    (void)adc_read();

    for (i = 0; i < num_samples; i++) {
        sum += adc_read();
    }

    return (uint16_t)(sum / num_samples);
}

/**
 * compute_checksum — Simple additive checksum for calibration record
 *
 * @record: Pointer to the calibration record
 * Returns: Checksum byte (sum of all bytes excluding the checksum field)
 */
static uint8_t compute_checksum(const struct adc_cal_record *record)
{
    const uint8_t *ptr = (const uint8_t *)record;
    uint8_t sum = 0;
    size_t i;
    /* Checksum covers everything except the checksum field itself,
     * which is the last byte of the struct. */
    for (i = 0; i < sizeof(struct adc_cal_record) - sizeof(uint8_t); i++) {
        sum += ptr[i];
    }
    return sum;
}

/* ── Public API implementation ─────────────────────────────────────────────── */

int adc_cal_init(void)
{
    const struct adc_cal_record *flash_record;
    bool valid = false;

    /* Initialize ADC hardware (if not already initialized) */
    adc_init();

    /* Enable temperature sensor channel */
    adc_set_temp_sensor_enabled(true);

    /* Point to the flash sector containing calibration data */
    flash_record = (const struct adc_cal_record *)
        (XIP_BASE + ADC_CAL_FLASH_OFFSET);

    /* Validate the calibration record */
    if (flash_record->magic == ADC_CAL_MAGIC &&
        flash_record->version == 1 &&
        flash_record->checksum == compute_checksum(flash_record) &&
        flash_record->coeffs.calibrated) {
        /* Valid calibration data found — copy it */
        memcpy(&cal_state.coeffs, &flash_record->coeffs,
               sizeof(cal_state.coeffs));
        valid = true;
    }

    if (!valid) {
        /* No valid calibration — use nominal values (unity gain, zero offset) */
        cal_state.coeffs.vbat_offset_mv = 0;
        cal_state.coeffs.vbat_gain_x1000 = 1000;  /* 1.000× */
        cal_state.coeffs.temp_offset_dcx10 = 0;
        cal_state.coeffs.temp_gain_x1000 = 1000;   /* 1.000× */
        cal_state.coeffs.calibrated = false;
        cal_state.coeffs.cal_version = 1;
        cal_state.coeffs.reserved = 0;
    }

    /* Compute actual VREF from internal reference */
    cal_state.vref_mv = adc_cal_compute_vref();
    cal_state.loaded = true;

    return 0;
}

uint16_t adc_cal_apply_vbat(uint16_t raw_mv)
{
    int32_t calibrated;

    if (!cal_state.loaded) {
        /* Module not initialized — return raw value */
        return raw_mv;
    }

    /* Apply offset: calibrated = raw + offset */
    calibrated = (int32_t)raw_mv + cal_state.coeffs.vbat_offset_mv;

    /* Apply gain: calibrated = calibrated × gain / 1000 */
    calibrated = (calibrated * (int32_t)cal_state.coeffs.vbat_gain_x1000) / 1000;

    /* Clamp to valid range */
    if (calibrated < 0)
        calibrated = 0;
    if (calibrated > 65535)
        calibrated = 65535;

    return (uint16_t)calibrated;
}

int16_t adc_cal_apply_temp(int16_t raw_temp_c_x10)
{
    int32_t calibrated;

    if (!cal_state.loaded) {
        return raw_temp_c_x10;
    }

    /* Apply gain: calibrated = raw × gain / 1000 */
    calibrated = ((int32_t)raw_temp_c_x10 *
                   (int32_t)cal_state.coeffs.temp_gain_x1000) / 1000;

    /* Apply offset: calibrated = calibrated + offset */
    calibrated += cal_state.coeffs.temp_offset_dcx10;

    return (int16_t)calibrated;
}

void adc_cal_get_coeffs(struct adc_cal_coeffs *out)
{
    if (out && cal_state.loaded) {
        memcpy(out, &cal_state.coeffs, sizeof(*out));
    }
}

int adc_cal_factory_calibrate(uint16_t vbat_low_mv, uint16_t vbat_high_mv)
{
    uint16_t raw_low, raw_high;
    uint16_t measured_low_mv, measured_high_mv;
    int32_t offset_mv;
    int32_t gain_x1000;
    struct adc_cal_record record;
    const uint8_t *flash_ptr;
    uint32_t saved_irq;

    /* Validate input voltages */
    if (vbat_low_mv < 2800 || vbat_low_mv > 4500)
        return -1;
    if (vbat_high_mv < 2800 || vbat_high_mv > 4500)
        return -1;
    if (vbat_high_mv <= vbat_low_mv)
        return -1;

    /* Read ADC with heavy oversampling for calibration accuracy.
     * Use 64 samples to reduce noise to < 1 LSB. */
    adc_select_input(0);  /* VBAT channel */
    (void)adc_read();     /* Discard stale reading */
    raw_low = adc_read_channel_averaged(0, ADC_CAL_CALIBRATION_SAMPLES);

    /* Convert raw ADC to voltage using nominal coefficients */
    measured_low_mv = (uint16_t)(((uint32_t)raw_low *
                    ADC_CAL_VBAT_NUMERATOR + ADC_CAL_VBAT_ROUNDING) /
                    ADC_CAL_VBAT_DENOMINATOR);

    /* Read high voltage point */
    raw_high = adc_read_channel_averaged(0, ADC_CAL_CALIBRATION_SAMPLES);
    measured_high_mv = (uint16_t)(((uint32_t)raw_high *
                    ADC_CAL_VBAT_NUMERATOR + ADC_CAL_VBAT_ROUNDING) /
                    ADC_CAL_VBAT_DENOMINATOR);

    /* Compute calibration coefficients using two-point linear regression.
     *
     * Ideal relationship: V_actual = V_measured × gain + offset
     *
     * Two equations, two unknowns:
     *   vbat_low_mv  = measured_low_mv  × gain + offset
     *   vbat_high_mv = measured_high_mv × gain + offset
     *
     * Solving:
     *   gain   = (vbat_high_mv - vbat_low_mv) / (measured_high_mv - measured_low_mv)
     *   offset = vbat_low_mv - measured_low_mv × gain
     *
     * We express gain as gain_x1000 = gain × 1000 for integer math.
     */
    if (measured_high_mv == measured_low_mv) {
        /* Avoid division by zero — ADC readings are the same at both voltages.
         * This indicates a hardware fault (ADC stuck, divider shorted, etc.) */
        return -2;
    }

    gain_x1000 = ((int32_t)(vbat_high_mv - vbat_low_mv) * 1000) /
                  (int32_t)(measured_high_mv - measured_low_mv);

    offset_mv = (int32_t)vbat_low_mv -
                ((int32_t)measured_low_mv * gain_x1000) / 1000;

    /* Sanity check the calibration coefficients.
     * Gain should be between 0.9× and 1.1× (900-1100).
     * Offset should be less than ±200 mV. */
    if (gain_x1000 < 900 || gain_x1000 > 1100) {
        return -3;  /* Gain out of range — likely hardware fault */
    }
    if (offset_mv < -200 || offset_mv > 200) {
        return -4;  /* Offset out of range — likely hardware fault */
    }

    /* Temperature calibration: we only apply a small offset correction.
     * The RP2350B temperature sensor has ~±2°C accuracy at 25°C.
     * For a more accurate calibration, we'd need a reference temperature
     * source, but for now we keep gain at unity and zero offset.
     * A production calibration setup can improve this. */
    cal_state.coeffs.vbat_offset_mv = (int16_t)offset_mv;
    cal_state.coeffs.vbat_gain_x1000 = (uint16_t)gain_x1000;
    cal_state.coeffs.temp_offset_dcx10 = 0;
    cal_state.coeffs.temp_gain_x1000 = 1000;
    cal_state.coeffs.calibrated = true;
    cal_state.coeffs.cal_version = 1;
    cal_state.coeffs.reserved = 0;

    /* Build the flash record */
    memset(&record, 0, sizeof(record));
    record.magic = ADC_CAL_MAGIC;
    record.version = 1;
    memcpy(&record.coeffs, &cal_state.coeffs, sizeof(record.coeffs));
    record.timestamp = (uint32_t)time_us_32();  /* Relative timestamp */
    record.checksum = compute_checksum(&record);

    /* Write to flash — must disable interrupts during flash operation.
     *
     * WARNING: Flash write erases the entire sector (4 KiB) and rewrites
     * it. Any code running from flash (including this function) will be
     * unavailable during the erase/write operation. Since we write the
     * complete record in one shot and don't read from flash during the
     * write, this is safe.
     *
     * The flash sector must be page-aligned (256 bytes). Our 4 KiB sector
     * at 0x1FF000 is aligned.
     */
    saved_irq = save_and_disable_interrupts();

    flash_range_erase(ADC_CAL_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(ADC_CAL_FLASH_OFFSET,
                        (const uint8_t *)&record,
                        sizeof(record));

    restore_interrupts(saved_irq);

    /* Verify the write by reading back */
    flash_ptr = (uint8_t *)(XIP_BASE + ADC_CAL_FLASH_OFFSET);
    if (memcmp(flash_ptr, &record, sizeof(record)) != 0) {
        /* Flash write verification failed */
        return -5;
    }

    return 0;
}

int adc_cal_self_test(void)
{
    uint16_t vref_raw;
    uint16_t vref_mv;
    uint16_t vbat_mv;
    int16_t temp_c_x10;

    /* Ensure ADC is initialized */
    adc_init();
    adc_set_temp_sensor_enabled(true);

    /* Test 1: Internal voltage reference should read ~1.2V
     * ADC channel 4 on RP2350B is the internal temperature sensor.
     * The internal reference is 1.2V, which appears as:
     *   ADC_raw = 1200 × 4095 / VREF
     *   At VREF = 3.3V: ADC_raw ≈ 1489
     * Acceptable range: 1300-1700 (accounts for VREF variation) */
    vref_raw = adc_read_channel_averaged(4, 16);
    vref_mv = (uint16_t)(((uint32_t)vref_raw * ADC_CAL_REF_MV +
                 ADC_CAL_FULL_SCALE / 2) / ADC_CAL_FULL_SCALE);
    /* vref_mv is computed for diagnostic purposes but the validation
     * below uses vref_raw directly, which is more reliable since it
     * avoids compounding ADC gain/offset errors. */
    (void)vref_mv;
    if (vref_raw < 1300 || vref_raw > 1700) {
        /* Internal reference out of expected range */
        return -1;
    }

    /* Test 2: Temperature should be in plausible range (-40°C to 85°C)
     * RP2350B temperature sensor formula:
     *   T = 27 - (V_sensor - V_27°C) / slope
     * With V_sensor = ADC_raw × VREF / 4095
     * Typical range: -40°C to 85°C → -400 to 850 in 0.1°C units */
    temp_c_x10 = battery_monitor_get_temp_c_x10();
    if (temp_c_x10 < -400 || temp_c_x10 > 850) {
        return -2;
    }

    /* Test 3: VBAT reading should be in plausible range.
     * For a Li-Po battery: 2800 mV (deeply discharged) to 4500 mV (charging).
     * The ADC will read 0 if the battery is disconnected. */
    vbat_mv = battery_monitor_get_vbat_mv();
    if (vbat_mv > 0 && (vbat_mv < 2800 || vbat_mv > 4500)) {
        return -3;
    }

    /* All tests passed */
    return 0;
}

uint16_t adc_cal_read_vref_int(void)
{
    uint16_t vref_raw;

    /* Read internal reference through ADC.
     * On RP2350B, the internal voltage reference (1.2V ±3%) is
     * readable as an ADC channel. */
    vref_raw = adc_read_channel_averaged(4, 16);

    /* Convert raw value to millivolts using known 1.2V reference:
     *   VREF = 1200 × 4095 / ADC_raw */
    if (vref_raw == 0)
        return 0;

    return (uint16_t)((1200UL * 4095UL + vref_raw / 2) / vref_raw);
}

uint16_t adc_cal_compute_vref(void)
{
    /* Use the internal 1.2V reference to compute the actual VREF voltage.
     *
     * The RP2350B ADC resolution is 12 bits (0-4095) with VREF as
     * the positive reference. The internal 1.2V bandgap reference
     * appears at a specific ADC code:
     *
     *   ADC_code = 1200 / VREF_actual × 4095
     *   VREF_actual = 1200 × 4095 / ADC_code
     *
     * This allows us to compensate for VREF variations caused by
     * supply voltage changes (3.3V ±5%) or temperature drift. */
    return adc_cal_read_vref_int();
}