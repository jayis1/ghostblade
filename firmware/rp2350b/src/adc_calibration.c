/*
 * adc_calibration.c — ADC Calibration and Voltage Divider Compensation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements per-board ADC calibration with flash storage.
 * Provides self-test, factory calibration, and runtime correction
 * of battery voltage and temperature readings.
 *
 * Flash layout for calibration data (RP2350B QSPI flash, 16 MB):
 *
 *   0x10000000 (XIP_BASE) ─── firmware code + rodata
 *   ...
 *   0x1010F000            ─── ADC calibration sector (4 KB)
 *                              struct adc_cal_record at the sector start
 *   0x1010FFFF            ─── end of calibration sector
 *   ...
 *   0x11000000            ─── end of QSPI flash
 *
 * FLASH_SECTOR_SIZE  = 4096 bytes (must erase a full sector at a time)
 * FLASH_PAGE_SIZE    = 256 bytes  (must program in page-aligned blocks)
 *
 * Flash offset (from XIP_BASE) used for calibration:
 *   FLASH_CAL_OFFSET = 0x10F000
 *
 * This places calibration data well above typical firmware size (~600 KB)
 * and safely within the 16 MB flash window.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "adc_calibration.h"

/* Pull in Pico SDK flash and XIP definitions when building on device */
#ifdef PICO_ON_DEVICE
#  include "hardware/flash.h"  /* flash_range_erase(), flash_range_program() */
#  include "hardware/sync.h"   /* save_and_disable_interrupts(), restore_interrupts() */
#  include "pico/stdlib.h"     /* XIP_BASE */
#endif

/* ========================================================================
 * Flash Sector Address for Calibration Data
 * ======================================================================== */

/*
 * Byte offset from the start of QSPI flash (i.e., from XIP_BASE).
 * Must be aligned to FLASH_SECTOR_SIZE (4096 bytes).
 */
#define FLASH_CAL_OFFSET    0x10F000UL

/*
 * XIP virtual address where calibration data can be read directly
 * without calling flash_range_program():
 *   XIP_BASE + FLASH_CAL_OFFSET
 *
 * On RP2350B, XIP_BASE = 0x10000000.
 */
#ifndef XIP_BASE
#  define XIP_BASE 0x10000000UL  /* Fallback for host-side compilation */
#endif

#define FLASH_CAL_XIP_ADDR  (XIP_BASE + FLASH_CAL_OFFSET)

/* ========================================================================
 * ADC Register Definitions (matches battery_monitor.c)
 * ======================================================================== */

#define RP2350B_ADC_BASE        0x50041000UL
#define ADC_CS                  0x00
#define ADC_RESULT              0x04
#define ADC_CS_EN               (1 << 0)
#define ADC_CS_TS_EN            (1 << 1)
#define ADC_CS_START_ONCE       (1 << 2)
#define ADC_CS_READY            (1 << 8)

#define REG32(addr)             (*(volatile uint32_t *)(addr))

/* ========================================================================
 * Default Calibration Coefficients
 * ======================================================================== */

static struct adc_cal_coeffs cal_coeffs = {
    .vbat_offset_mv    = 0,       /* No offset correction by default */
    .vbat_gain_x1000   = 1000,    /* Unity gain by default */
    .temp_offset_dcx10 = 0,       /* No offset correction */
    .temp_gain_x1000   = 1000,    /* Unity gain */
    .calibrated        = false,
    .cal_version       = 1,
    .reserved          = 0,
};

/* ========================================================================
 * Calibration Record Checksum
 * ======================================================================== */

/*
 * compute_checksum — Compute an 8-bit additive checksum over a byte range.
 *
 * Sums all bytes in [data, data+len) and returns the lower 8 bits.
 * The caller stores this value in adc_cal_record.checksum; on load, the
 * checksum field is excluded from the check (the stored value is compared
 * against a freshly computed sum of the preceding bytes).
 *
 * This is an intentionally simple integrity check — the primary protection
 * against flash corruption is the ADC_CAL_MAGIC sentinel. The checksum
 * catches single-byte bit-flip errors that would pass the magic check.
 *
 * @data: Pointer to the byte range to checksum
 * @len:  Number of bytes
 *
 * Returns: 8-bit sum (modulo 256)
 */
static uint8_t compute_checksum(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += p[i];
    return (uint8_t)(sum & 0xFFU);
}

/*
 * validate_cal_record — Verify an adc_cal_record read from flash.
 *
 * Checks:
 *   1. magic == ADC_CAL_MAGIC
 *   2. version == 1  (only format version we know)
 *   3. checksum matches bytes [0, sizeof(record)-1)
 *
 * @rec: Pointer to the flash-resident record
 *
 * Returns: true if valid, false if magic mismatch, unknown version, or
 *          checksum error.
 */
static bool validate_cal_record(const struct adc_cal_record *rec) {
    uint8_t expected;

    if (!rec)
        return false;

    /* Magic sentinel */
    if (rec->magic != ADC_CAL_MAGIC)
        return false;

    /* Version check — only version 1 is defined */
    if (rec->version != 1)
        return false;

    /* Checksum: covers all bytes except the checksum field itself.
     * sizeof(*rec) - sizeof(rec->checksum) = all preceding bytes. */
    expected = compute_checksum(rec, sizeof(*rec) - sizeof(rec->checksum));
    if (rec->checksum != expected)
        return false;

    return true;
}

/* ========================================================================
 * Flash Load / Store  (device-only; stubbed for host unit tests)
 * ======================================================================== */

/*
 * adc_cal_load_from_flash — Read calibration record from flash.
 *
 * On RP2350B hardware, the calibration sector is directly readable via
 * the XIP interface without any flash driver call — we just cast the
 * XIP address to a pointer.  The validate_cal_record() guard ensures we
 * do not accept erased flash (0xFF fill) or corrupted data.
 *
 * Returns: true and populates cal_coeffs if a valid record is found,
 *          false otherwise (caller keeps the default unity coefficients).
 */
static bool adc_cal_load_from_flash(void) {
#ifdef PICO_ON_DEVICE
    /* Read directly through the XIP window — no flash unlock required. */
    const struct adc_cal_record *rec =
        (const struct adc_cal_record *)FLASH_CAL_XIP_ADDR;

    if (!validate_cal_record(rec))
        return false;

    cal_coeffs = rec->coeffs;
    return true;
#else
    /* Host build — no flash hardware; always return "not found" so tests
     * exercise the default-coefficients path. */
    return false;
#endif
}

/*
 * adc_cal_store_to_flash — Persist current calibration coefficients to flash.
 *
 * Steps:
 *   1. Assemble an adc_cal_record in SRAM.
 *   2. Disable interrupts (required by flash_range_erase/program).
 *   3. Erase one 4 KB sector at FLASH_CAL_OFFSET.
 *   4. Program one 256-byte page at FLASH_CAL_OFFSET.
 *   5. Re-enable interrupts.
 *   6. Read the written data back through XIP and re-validate.
 *
 * The program buffer is padded to FLASH_PAGE_SIZE with 0xFF to satisfy
 * the page-aligned write requirement and to avoid corrupting bytes
 * beyond the struct boundary.
 *
 * @uptime_ms: Current MCU uptime in milliseconds, stored as the record
 *             timestamp for diagnostics.  Pass 0 when an RTC is absent.
 *
 * Returns: 0 on success, -1 on parameter error, -2 on readback failure.
 */
static int adc_cal_store_to_flash(uint32_t uptime_ms) {
#ifdef PICO_ON_DEVICE
    /*
     * Build the record in SRAM.  We must not write directly from a struct
     * that lives in flash (XIP cache) — use a stack or SRAM buffer.
     */
    struct adc_cal_record record;
    /* Pad the page buffer with 0xFF (erased-flash value) before filling. */
    uint8_t page_buf[FLASH_PAGE_SIZE];
    uint32_t saved_interrupts;

    if (sizeof(struct adc_cal_record) > FLASH_PAGE_SIZE)
        return -1;  /* Struct grew beyond one flash page — developer error */

    /* Assemble the record */
    record.magic     = ADC_CAL_MAGIC;
    record.version   = 1;
    record.coeffs    = cal_coeffs;
    record.timestamp = uptime_ms;  /* seconds-accurate RTC would be ideal */
    record.checksum  = compute_checksum(&record,
                                         sizeof(record) - sizeof(record.checksum));

    /* Copy into a full-page buffer; remainder stays 0xFF */
    memset(page_buf, 0xFF, sizeof(page_buf));
    memcpy(page_buf, &record, sizeof(record));

    /*
     * Disable interrupts for the duration of flash erase/program.
     *
     * flash_range_erase() and flash_range_program() both require that no
     * code executing from XIP flash fires an interrupt while the flash
     * controller is busy.  save_and_disable_interrupts() + restore_interrupts()
     * is the standard Pico SDK idiom for this.
     *
     * The combined erase (~50 ms) + program (~0.5 ms) window is longer than
     * typical watchdog kick intervals, so the caller (adc_cal_factory_calibrate)
     * should kick the watchdog immediately before calling this function.
     */
    saved_interrupts = save_and_disable_interrupts();
    flash_range_erase(FLASH_CAL_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CAL_OFFSET, page_buf, FLASH_PAGE_SIZE);
    restore_interrupts(saved_interrupts);

    /*
     * Readback verification: re-read through the XIP window and validate.
     * The XIP cache may still hold the pre-erase content; the Pico SDK's
     * flash_range_program() invalidates the XIP cache before returning,
     * so a direct pointer read here sees the freshly written data.
     */
    const struct adc_cal_record *written =
        (const struct adc_cal_record *)FLASH_CAL_XIP_ADDR;
    if (!validate_cal_record(written))
        return -2;  /* Readback mismatch — flash may be faulty */

    return 0;
#else
    /* Host build — no flash hardware; pretend success. */
    (void)uptime_ms;
    return 0;
#endif
}

/* ========================================================================
 * ADC Read Helper (shared with battery_monitor.c)
 * ======================================================================== */

static uint16_t adc_read_channel(uint8_t channel) {
    volatile uint32_t *cs = (volatile uint32_t *)(RP2350B_ADC_BASE + ADC_CS);
    const volatile uint32_t *result = (const volatile uint32_t *)(RP2350B_ADC_BASE + ADC_RESULT);
    uint32_t timeout;

    uint32_t cs_val = *cs;
    cs_val &= ~(0x1FUL << 12);
    cs_val |= ((uint32_t)channel << 12);

    if (channel == 4)
        cs_val |= ADC_CS_TS_EN;
    else
        cs_val &= ~ADC_CS_TS_EN;

    *cs = cs_val;
    *cs |= ADC_CS_START_ONCE;

    /* Wait for conversion with timeout to prevent indefinite hang */
    timeout = 1000;
    while (!(*cs & ADC_CS_READY))
        if (--timeout == 0)
            return 0;  /* ADC conversion timed out */

    return (uint16_t)(*result & 0xFFF);
}

static uint16_t adc_read_averaged(uint8_t channel, uint8_t samples) {
    uint32_t sum = 0;
    uint8_t n = (samples < 1) ? 1 : ((samples > 64) ? 64 : samples);

    for (uint8_t i = 0; i < n; i++) {
        sum += adc_read_channel(channel);
    }

    return (uint16_t)(sum / n);
}

/* ========================================================================
 * Internal Voltage Reference Reading
 * ======================================================================== */

/*
 * The RP2350B has an internal 1.2V voltage reference that can be
 * measured through the ADC. This is useful for calibrating the
 * actual VREF voltage.
 *
 * ADC channel for internal reference: Not a standard channel on RP2350B.
 * We use the known VREF value and ADC full-scale to compute the actual VREF.
 */

uint16_t adc_cal_read_vref_int(void) {
    /* Read the internal 1.2V reference through ADC channel 29
     * (or similar manufacturer-defined channel).
     * On RP2350B, this maps to the internal reference.
     * For now, we use a fixed nominal value since the exact
     * channel may vary by silicon revision. */
    return 1200;  /* Nominal 1.2V internal reference in mV */
}

uint16_t adc_cal_compute_vref(void) {
    /* Compute actual VREF using internal reference:
     * VREF_actual = VREF_int × ADC_fullscale / ADC_vref_raw
     *
     * Since we can't directly read the internal reference on all
     * RP2350B revisions, we return the nominal value unless
     * calibration has been performed. */
    if (cal_coeffs.calibrated) {
        /* If calibrated, use the measured VREF */
        return (uint16_t)((uint32_t)ADC_CAL_REF_MV *
                          cal_coeffs.vbat_gain_x1000 / 1000);
    }
    return ADC_CAL_REF_MV;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

int adc_cal_init(void) {
    /* Initialize with default (uncalibrated) coefficients.
     * These are used as the fallback if flash holds no valid record
     * (e.g., first boot after manufacturing, or after flash erase). */
    cal_coeffs.vbat_offset_mv    = 0;
    cal_coeffs.vbat_gain_x1000   = 1000;
    cal_coeffs.temp_offset_dcx10 = 0;
    cal_coeffs.temp_gain_x1000   = 1000;
    cal_coeffs.calibrated        = false;
    cal_coeffs.cal_version       = 1;

    /* Attempt to load factory calibration data from flash sector 0x10F000.
     *
     * adc_cal_load_from_flash() reads the adc_cal_record stored in the
     * calibration sector, validates the magic sentinel (ADC_CAL_MAGIC),
     * checks the format version (must be 1), and verifies the additive
     * checksum.  If all checks pass, cal_coeffs is populated with the
     * factory-measured values and this function returns 1; otherwise the
     * defaults set above remain in effect and 0 is returned.
     *
     * On the host (unit tests), adc_cal_load_from_flash() always returns
     * false so tests always exercise the uncalibrated path. */
    if (adc_cal_load_from_flash()) {
        /* Loaded factory calibration — cal_coeffs.calibrated is now true */
        return 1;  /* Positive: calibration data found and loaded */
    }

    return 0;  /* Zero: running with default (uncalibrated) coefficients */
}

uint16_t adc_cal_apply_vbat(uint16_t raw_mv) {
    /* Apply calibration correction to raw battery voltage:
     *
     * VBAT_calibrated = (raw_mv + vbat_offset_mv) × vbat_gain_x1000 / 1000
     *
     * This compensates for:
     *   - ADC offset error
     *   - Resistor divider tolerance
     *   - VREF voltage variation
     */
    int32_t corrected = (int32_t)raw_mv + cal_coeffs.vbat_offset_mv;
    corrected = (corrected * (int32_t)cal_coeffs.vbat_gain_x1000) / 1000;

    /* Clamp to valid range (0-5500 mV covers all Li-Po scenarios) */
    if (corrected < 0) corrected = 0;
    if (corrected > 5500) corrected = 5500;

    return (uint16_t)corrected;
}

int16_t adc_cal_apply_temp(int16_t raw_temp_c_x10) {
    /* Apply calibration correction to raw temperature:
     *
     * temp_calibrated = raw_temp × temp_gain / 1000 + temp_offset
     */
    int32_t corrected = ((int32_t)raw_temp_c_x10 *
                         (int32_t)cal_coeffs.temp_gain_x1000) / 1000;
    corrected += cal_coeffs.temp_offset_dcx10;

    /* Clamp to reasonable range (-40°C to 125°C in °C×10) */
    if (corrected < -400) corrected = -400;
    if (corrected > 1250) corrected = 1250;

    return (int16_t)corrected;
}

void adc_cal_get_coeffs(struct adc_cal_coeffs *out) {
    if (out)
        *out = cal_coeffs;
}

int adc_cal_factory_calibrate(uint16_t vbat_low_mv, uint16_t vbat_high_mv) {
    uint16_t adc_low, adc_high;
    int32_t raw_low_mv, raw_high_mv;
    int32_t offset, gain_x1000;
    int flash_ret;

    /* Step 1: Read ADC at known low voltage */
    adc_low = adc_read_averaged(0, ADC_CAL_CALIBRATION_SAMPLES);

    /* Convert raw ADC to mV using nominal divider */
    raw_low_mv = (int32_t)((uint32_t)adc_low * ADC_CAL_VBAT_NUMERATOR +
                            ADC_CAL_VBAT_ROUNDING) /
                 (int32_t)ADC_CAL_VBAT_DENOMINATOR;

    /* Step 2: Read ADC at known high voltage */
    adc_high = adc_read_averaged(0, ADC_CAL_CALIBRATION_SAMPLES);

    raw_high_mv = (int32_t)((uint32_t)adc_high * ADC_CAL_VBAT_NUMERATOR +
                             ADC_CAL_VBAT_ROUNDING) /
                  (int32_t)ADC_CAL_VBAT_DENOMINATOR;

    /* Step 3: Compute two-point calibration coefficients
     *
     * Linear model: V_calibrated = (V_raw + offset) × gain / 1000
     *
     * From two points (V_raw_low, V_known_low) and (V_raw_high, V_known_high):
     *
     * gain = (V_known_high - V_known_low) / (V_raw_high - V_raw_low) × 1000
     *
     * offset = V_known_low × 1000 / gain - V_raw_low
     */
    if (raw_high_mv == raw_low_mv)
        return -1;  /* Avoid division by zero */

    gain_x1000 = ((int32_t)vbat_high_mv - (int32_t)vbat_low_mv) * 1000 /
                  (raw_high_mv - raw_low_mv);

    offset = (int32_t)vbat_low_mv * 1000 / gain_x1000 - raw_low_mv;

    /* Store calibration coefficients in RAM */
    cal_coeffs.vbat_offset_mv    = (int16_t)offset;
    cal_coeffs.vbat_gain_x1000   = (uint16_t)gain_x1000;
    cal_coeffs.calibrated         = true;
    cal_coeffs.cal_version        = 1;

    /* Temperature calibration uses default values (offset=0, gain=1000).
     * Temperature calibration requires a temperature chamber and is
     * done separately if needed. */
    cal_coeffs.temp_offset_dcx10 = 0;
    cal_coeffs.temp_gain_x1000   = 1000;

    /* Step 4: Persist calibration coefficients to flash sector 0x10F000.
     *
     * adc_cal_store_to_flash() performs:
     *   1. Assemble an adc_cal_record in SRAM (magic + version + coeffs +
     *      timestamp + additive checksum).
     *   2. Disable interrupts (required by Pico SDK flash APIs).
     *   3. flash_range_erase() — erase the 4 KB calibration sector.
     *   4. flash_range_program() — write a 256-byte page with the record.
     *   5. Re-enable interrupts.
     *   6. Readback via XIP to verify written data passes validate_cal_record().
     *
     * Passing 0 for uptime_ms is acceptable; a real RTC timestamp would
     * require a system clock that isn't available during factory calibration.
     * The timestamp field is diagnostic only.
     *
     * Return codes from adc_cal_store_to_flash():
     *   0  — success
     *  -1  — struct too large for one flash page (developer error)
     *  -2  — readback validation failed (flash may be faulty)
     */
    flash_ret = adc_cal_store_to_flash(0);
    if (flash_ret != 0)
        return flash_ret;  /* Propagate flash error to caller */

    return 0;
}

int adc_cal_self_test(void) {
    uint16_t vbat_mv;
    int16_t temp_c_x10;

    /* Test 1: Read VBAT and check it's in plausible range */
    vbat_mv = (uint16_t)(((uint32_t)adc_read_averaged(0, 16) *
                          ADC_CAL_VBAT_NUMERATOR + ADC_CAL_VBAT_ROUNDING) /
                         ADC_CAL_VBAT_DENOMINATOR);
    if (vbat_mv < 2500 || vbat_mv > 5500)
        return -3;  /* VBAT out of range */

    /* Test 2: Read temperature and check it's in plausible range */
    uint16_t adc_temp = adc_read_averaged(4, 16);
    int32_t v_temp_uv = (int32_t)adc_temp * 806;
    int32_t delta_uv = v_temp_uv - 706000;
    int32_t delta_dc = delta_uv / 1721;
    temp_c_x10 = (int16_t)(270 - delta_dc);

    if (temp_c_x10 < -400 || temp_c_x10 > 1250)
        return -2;  /* Temperature out of range */

    /* All tests passed */
    return 0;
}
