/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 jayis1
 * Include the production implementation to exercise private arithmetic without
 * substituting copied helpers or accessing ADC/flash hardware.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../firmware/rp2350b/src/adc_calibration.c"

static void test_saturation(void)
{
    const int16_t offsets[] = { INT16_MIN, -1, 0, 1, INT16_MAX };
    const uint16_t gains[] = { 0, 1, 1000, 32768, UINT16_MAX };

    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        for (size_t j = 0; j < sizeof(gains) / sizeof(gains[0]); j++) {
            cal_coeffs.vbat_offset_mv = offsets[i];
            cal_coeffs.vbat_gain_x1000 = gains[j];
            for (uint32_t raw = 0; raw <= UINT16_MAX; raw++) {
                int64_t expected = ((int64_t)raw + offsets[i]) * gains[j] / 1000;
                if (expected < 0)
                    expected = 0;
                if (expected > 5500)
                    expected = 5500;
                assert(adc_cal_apply_vbat((uint16_t)raw) == expected);
            }
        }
    }
}

static void test_raw_conversion(void)
{
    /* All possible ADC samples; explicit reference independent of the macros. */
    for (uint16_t raw = 0; raw <= 4095; raw++) {
        uint16_t expected = (uint16_t)(((uint64_t)raw * 3249700 + 500) / 1000000);
        assert(adc_cal_raw_to_mv(raw) == expected);
    }
    assert(adc_cal_raw_to_mv(1500) == 4874);
    assert(adc_cal_raw_to_mv(4095) == 13307);
}

static void test_coefficients(void)
{
    const uint16_t invalid[][4] = {
        {3300, 3300, 3300, 4100}, /* equal references */
        {4100, 3300, 3300, 4100}, /* reversed references */
        {3300, 4100, 3300, 3300}, /* equal measurements */
        {3300, 4100, 4100, 3300}, /* reversed measurements */
        {3300, 3301, 1000, 3000}, /* gain truncates to zero */
        {0, UINT16_MAX, 0, 1},   /* gain cannot fit uint16_t */
        {3300, 3301, 0, 1000},   /* positive offset cannot fit int16_t */
        {0, 1, 60000, 61000},    /* negative offset cannot fit int16_t */
    };
    int16_t offset;
    uint16_t gain;

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        offset = 123;
        gain = 456;
        assert(adc_cal_compute_vbat_coeffs(invalid[i][0], invalid[i][1],
                   invalid[i][2], invalid[i][3], &offset, &gain) == -1);
        assert(offset == 123 && gain == 456);
    }
    assert(adc_cal_compute_vbat_coeffs(3300, 4100, 3300, 4100, &offset, &gain) == 0);
    assert(offset == 0 && gain == 1000);
    assert(adc_cal_compute_vbat_coeffs(3300, 4100, 3200, 4000, &offset, &gain) == 0);
    assert(offset == 100 && gain == 1000);
    assert(adc_cal_compute_vbat_coeffs(3000, 4000, 1500, 2000, &offset, &gain) == 0);
    assert(offset == 0 && gain == 2000);
    assert(adc_cal_compute_vbat_coeffs(0, UINT16_MAX, 0, 1000, &offset, &gain) == 0);
    assert(offset == 0 && gain == UINT16_MAX);
    assert(adc_cal_compute_vbat_coeffs(0, 1, 0, 1000, &offset, &gain) == 0);
    assert(offset == 0 && gain == 1);
    assert(adc_cal_compute_vbat_coeffs(32767, 32768, 0, 1, &offset, &gain) == 0);
    assert(offset == INT16_MAX && gain == 1000);
    assert(adc_cal_compute_vbat_coeffs(0, 1, 32768, 32769, &offset, &gain) == 0);
    assert(offset == INT16_MIN && gain == 1000);
}

static void test_self_test_temperature(void)
{
    /* Register backing only: no ADC timing or electrical behavior emulation. */
    long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size > 0);
    void *registers = mmap((void *)RP2350B_ADC_BASE, (size_t)page_size,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                          -1, 0);
    assert(registers != MAP_FAILED);
    assert(registers == (void *)RP2350B_ADC_BASE);
    REG32(RP2350B_ADC_BASE + ADC_CS) = ADC_CS_READY;
    REG32(RP2350B_ADC_BASE + ADC_RESULT) = 876;
    assert(adc_cal_self_test() == 0);
    /* Valid battery voltage but an implausibly cold temperature. The old
     * unit mismatch incorrectly accepted this sample as about -15 C. */
    REG32(RP2350B_ADC_BASE + ADC_RESULT) = 1400;
    assert(adc_cal_self_test() == -2);
    REG32(RP2350B_ADC_BASE + ADC_RESULT) = 0;
    assert(adc_cal_self_test() == -3);
    assert(munmap(registers, (size_t)page_size) == 0);
}

int main(void)
{
    struct adc_cal_coeffs before, after;

    assert(adc_cal_init() == 0);
    adc_cal_get_coeffs(&before);
    /* These calls must reject references before attempting hardware access. */
    assert(adc_cal_factory_calibrate(3300, 3300) == -1);
    assert(adc_cal_factory_calibrate(4100, 3300) == -1);
    adc_cal_get_coeffs(&after);
    assert(before.vbat_offset_mv == after.vbat_offset_mv);
    assert(before.vbat_gain_x1000 == after.vbat_gain_x1000);
    assert(before.temp_offset_dcx10 == after.temp_offset_dcx10);
    assert(before.temp_gain_x1000 == after.temp_gain_x1000);
    assert(before.calibrated == after.calibrated);
    assert(before.cal_version == after.cal_version);
    assert(before.reserved == after.reserved);
    assert(!validate_cal_record(NULL));
    test_saturation();
    test_raw_conversion();
    test_coefficients();
    test_self_test_temperature();
    puts("ADC calibration production arithmetic tests passed");
    return 0;
}
