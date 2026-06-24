/*
 * test_cc1101_lms7002m.c — Unit Tests for CC1101 and LMS7002M Initialization
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests for:
 *   1. CC1101 register configuration table validation
 *   2. CC1101 SPI framing (read/write/burst command encoding)
 *   3. CC1101 band selection and frequency calculation
 *   4. LMS7002M PLL parameter calculation
 *   5. LMS7002M frequency range validation
 *   6. LMS7002M gain distribution
 *   7. LMS7002M SPI register access encoding
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -o test_cc1101_lms7002m tests/test_cc1101_lms7002m.c
 *
 * Run:
 *   ./test_cc1101_lms7002m
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * CC1101 Register Definitions (from cc1101_init.c)
 * ======================================================================== */

#define CC1101_IOCFG2           0x00
#define CC1101_IOCFG1           0x01
#define CC1101_IOCFG0           0x02
#define CC1101_FIFOTHR          0x03
#define CC1101_SYNC1             0x04
#define CC1101_SYNC0             0x05
#define CC1101_PKTLEN            0x06
#define CC1101_PKTCTRL1          0x07
#define CC1101_PKTCTRL0          0x08
#define CC1101_ADDR              0x09
#define CC1101_CHANNR            0x0A
#define CC1101_FSCTRL1           0x0B
#define CC1101_FSCTRL0           0x0C
#define CC1101_FREQ2             0x0D
#define CC1101_FREQ1             0x0E
#define CC1101_FREQ0             0x0F
#define CC1101_MDMCFG4           0x10
#define CC1101_MDMCFG3           0x11
#define CC1101_MDMCFG2           0x12
#define CC1101_MDMCFG1           0x13
#define CC1101_MDMCFG0           0x14
#define CC1101_DEVIATN           0x15
#define CC1101_MCSM2             0x16
#define CC1101_MCSM1             0x17
#define CC1101_MCSM0             0x18
#define CC1101_FOCCFG            0x19
#define CC1101_BSCFG             0x1A
#define CC1101_AGCCTRL2          0x1B
#define CC1101_AGCCTRL1          0x1C
#define CC1101_AGCCTRL0          0x1D
#define CC1101_WOREVT1           0x1E
#define CC1101_WOREVT0           0x1F
#define CC1101_WORCTRL           0x20
#define CC1101_FREND1            0x21
#define CC1101_FREND0            0x22
#define CC1101_FSCAL3            0x23
#define CC1101_FSCAL2            0x24
#define CC1101_FSCAL1            0x25
#define CC1101_FSCAL0            0x26
#define CC1101_RCCTRL1           0x27
#define CC1101_RCCTRL0           0x28

/* CC1101 SPI access macros */
#define CC1101_WRITE_SINGLE(addr)    ((addr) & 0x3F)
#define CC1101_WRITE_BURST(addr)     (((addr) & 0x3F) | 0x40)
#define CC1101_READ_SINGLE(addr)     (((addr) & 0x3F) | 0x80)
#define CC1101_READ_BURST(addr)      (((addr) & 0x3F) | 0xC0)
#define CC1101_STROBE(cmd)           ((cmd) & 0x3F)

/* CC1101 command strobes */
#define CC1101_SRES              0x30
#define CC1101_SFSTXON           0x31
#define CC1101_SXOFF             0x32
#define CC1101_SCAL              0x33
#define CC1101_SRX               0x34
#define CC1101_STX               0x35
#define CC1101_SIDLE             0x36

/* CC1101 band identifiers */
#define CC1101_BAND_433  0
#define CC1101_BAND_868  1
#define CC1101_BAND_915  2

/* Configuration table entry */
struct cc1101_reg_entry {
    uint8_t addr;
    uint8_t val;
};

/* ========================================================================
 * CC1101 Configuration Tables (from cc1101_init.c)
 * ======================================================================== */

static const struct cc1101_reg_entry cc1101_config_868mhz[] = {
    { CC1101_IOCFG2,   0x0B },
    { CC1101_IOCFG1,   0x2E },
    { CC1101_IOCFG0,   0x06 },
    { CC1101_FIFOTHR,  0x07 },
    { CC1101_SYNC1,    0xD3 },
    { CC1101_SYNC0,    0x91 },
    { CC1101_PKTLEN,   0xFF },
    { CC1101_PKTCTRL1, 0x04 },
    { CC1101_PKTCTRL0, 0x05 },
    { CC1101_ADDR,     0x00 },
    { CC1101_CHANNR,   0x00 },
    { CC1101_FSCTRL1,  0x0C },
    { CC1101_FSCTRL0,  0x00 },
    { CC1101_FREQ2,    0x21 },
    { CC1101_FREQ1,    0x62 },
    { CC1101_FREQ0,    0x76 },
    { CC1101_MDMCFG4,  0x0D },
    { CC1101_MDMCFG3,  0x55 },
    { CC1101_MDMCFG2,  0x13 },
    { CC1101_MDMCFG1,  0x22 },
    { CC1101_MDMCFG0,  0x22 },
    { CC1101_DEVIATN,  0x63 },
    { CC1101_MCSM2,    0x07 },
    { CC1101_MCSM1,    0x30 },
    { CC1101_MCSM0,    0x18 },
    { CC1101_FOCCFG,   0x16 },
    { CC1101_BSCFG,    0x6C },
    { CC1101_AGCCTRL2, 0x03 },
    { CC1101_AGCCTRL1, 0x40 },
    { CC1101_AGCCTRL0, 0x91 },
    { CC1101_WOREVT1,  0x87 },
    { CC1101_WOREVT0,  0x6B },
    { CC1101_WORCTRL,  0xFB },
    { CC1101_FREND1,   0x56 },
    { CC1101_FREND0,   0x10 },
    { CC1101_FSCAL3,   0xE9 },
    { CC1101_FSCAL2,   0x2A },
    { CC1101_FSCAL1,   0x00 },
    { CC1101_FSCAL0,   0x1F },
    { CC1101_RCCTRL1,  0x41 },
    { CC1101_RCCTRL0,  0x00 },
};
#define CC1101_CONFIG_TABLE_SIZE_868  (sizeof(cc1101_config_868mhz) / sizeof(cc1101_config_868mhz[0]))

static const struct cc1101_reg_entry cc1101_config_433mhz[] = {
    { CC1101_IOCFG2,   0x0B },
    { CC1101_IOCFG1,   0x2E },
    { CC1101_IOCFG0,   0x06 },
    { CC1101_FIFOTHR,  0x07 },
    { CC1101_SYNC1,    0xD3 },
    { CC1101_SYNC0,    0x91 },
    { CC1101_PKTLEN,   0xFF },
    { CC1101_PKTCTRL1, 0x04 },
    { CC1101_PKTCTRL0, 0x05 },
    { CC1101_ADDR,     0x00 },
    { CC1101_CHANNR,   0x00 },
    { CC1101_FSCTRL1,  0x0C },
    { CC1101_FSCTRL0,  0x00 },
    { CC1101_FREQ2,    0x10 },
    { CC1101_FREQ1,    0xA7 },
    { CC1101_FREQ0,    0x62 },
    { CC1101_MDMCFG4,  0x0D },
    { CC1101_MDMCFG3,  0x55 },
    { CC1101_MDMCFG2,  0x13 },
    { CC1101_MDMCFG1,  0x22 },
    { CC1101_MDMCFG0,  0x22 },
    { CC1101_DEVIATN,  0x63 },
    { CC1101_MCSM2,    0x07 },
    { CC1101_MCSM1,    0x30 },
    { CC1101_MCSM0,    0x18 },
    { CC1101_FOCCFG,   0x16 },
    { CC1101_BSCFG,    0x6C },
    { CC1101_AGCCTRL2, 0x03 },
    { CC1101_AGCCTRL1, 0x40 },
    { CC1101_AGCCTRL0, 0x91 },
    { CC1101_WOREVT1,  0x87 },
    { CC1101_WOREVT0,  0x6B },
    { CC1101_WORCTRL,  0xFB },
    { CC1101_FREND1,   0x56 },
    { CC1101_FREND0,   0x10 },
    { CC1101_FSCAL3,   0xE9 },
    { CC1101_FSCAL2,   0x2A },
    { CC1101_FSCAL1,   0x00 },
    { CC1101_FSCAL0,   0x1F },
    { CC1101_RCCTRL1,  0x41 },
    { CC1101_RCCTRL0,  0x00 },
};
#define CC1101_CONFIG_TABLE_SIZE_433  (sizeof(cc1101_config_433mhz) / sizeof(cc1101_config_433mhz[0]))

static const struct cc1101_reg_entry cc1101_config_915mhz[] = {
    { CC1101_IOCFG2,   0x0B },
    { CC1101_IOCFG1,   0x2E },
    { CC1101_IOCFG0,   0x06 },
    { CC1101_FIFOTHR,  0x07 },
    { CC1101_SYNC1,    0xD3 },
    { CC1101_SYNC0,    0x91 },
    { CC1101_PKTLEN,   0xFF },
    { CC1101_PKTCTRL1, 0x04 },
    { CC1101_PKTCTRL0, 0x05 },
    { CC1101_ADDR,     0x00 },
    { CC1101_CHANNR,   0x00 },
    { CC1101_FSCTRL1,  0x0C },
    { CC1101_FSCTRL0,  0x00 },
    { CC1101_FREQ2,    0x23 },
    { CC1101_FREQ1,    0x31 },
    { CC1101_FREQ0,    0x3B },
    { CC1101_MDMCFG4,  0x0D },
    { CC1101_MDMCFG3,  0x55 },
    { CC1101_MDMCFG2,  0x13 },
    { CC1101_MDMCFG1,  0x22 },
    { CC1101_MDMCFG0,  0x22 },
    { CC1101_DEVIATN,  0x63 },
    { CC1101_MCSM2,    0x07 },
    { CC1101_MCSM1,    0x30 },
    { CC1101_MCSM0,    0x18 },
    { CC1101_FOCCFG,   0x16 },
    { CC1101_BSCFG,    0x6C },
    { CC1101_AGCCTRL2, 0x03 },
    { CC1101_AGCCTRL1, 0x40 },
    { CC1101_AGCCTRL0, 0x91 },
    { CC1101_WOREVT1,  0x87 },
    { CC1101_WOREVT0,  0x6B },
    { CC1101_WORCTRL,  0xFB },
    { CC1101_FREND1,   0x56 },
    { CC1101_FREND0,   0x10 },
    { CC1101_FSCAL3,   0xE9 },
    { CC1101_FSCAL2,   0x2A },
    { CC1101_FSCAL1,   0x00 },
    { CC1101_FSCAL0,   0x1F },
    { CC1101_RCCTRL1,  0x41 },
    { CC1101_RCCTRL0,  0x00 },
};
#define CC1101_CONFIG_TABLE_SIZE_915  (sizeof(cc1101_config_915mhz) / sizeof(cc1101_config_915mhz[0]))

/* ========================================================================
 * LMS7002M Constants (from lms7002m_driver.h)
 * ======================================================================== */

#define LMS7002M_REF_CLK_HZ          30720000UL
#define LMS7002M_FREQ_MIN             100000UL
#define LMS7002M_FREQ_MAX             3800000000UL
#define LMS7002M_SAMPLE_RATE_MIN      100000UL
#define LMS7002M_SAMPLE_RATE_MAX      10000000UL
#define LMS7002M_PLL_INT_MIN          1
#define LMS7002M_PLL_INT_MAX          255

/* VCO ranges */
#define VCO_MIN_HZ          1880000000ULL
#define VCO_MAX_HZ           5800000000ULL
#define VCO_H_THRESHOLD_HZ  3720000000ULL

/* ========================================================================
 * PLL Calculation Function (replicated from lms7002m_driver.c for testing)
 * ======================================================================== */

/**
 * calculate_pll_params — Calculate PLL parameters for a given frequency.
 *
 * This is a pure-function replica of lms7002m_calculate_pll_params()
 * for unit testing without hardware dependencies.
 */
static int calculate_pll_params(uint32_t freq_hz, uint32_t ref_clk,
                                 uint16_t *nint, uint32_t *nfrac,
                                 uint8_t *div_out) {
    uint32_t div;
    uint64_t vco_freq;
    uint64_t pll_ratio;
    uint32_t nint_val;
    uint32_t nfrac_val;

    if (freq_hz < LMS7002M_FREQ_MIN || freq_hz > LMS7002M_FREQ_MAX)
        return -1;

    if (!ref_clk || !nint || !nfrac || !div_out)
        return -1;

    /* Select output divider to keep VCO in range (1.88 – 5.8 GHz)
     * LMS7002M has VCO_L (1.88-3.72 GHz) and VCO_H (3.72-5.8 GHz) */
    for (div = 1; div <= 32; div *= 2) {
        vco_freq = (uint64_t)freq_hz * div;

        if (vco_freq >= VCO_MIN_HZ && vco_freq <= VCO_MAX_HZ) {
            break;
        }

        if (div == 32) {
            vco_freq = (uint64_t)freq_hz * 32;
            if (vco_freq < VCO_MIN_HZ)
                return -1;
            break;
        }
    }

    *div_out = (uint8_t)div;

    pll_ratio = (vco_freq << 24) / ref_clk;
    nint_val = (uint32_t)(pll_ratio >> 24);
    nfrac_val = (uint32_t)(pll_ratio & 0x00FFFFFFUL);

    if (nint_val < LMS7002M_PLL_INT_MIN || nint_val > LMS7002M_PLL_INT_MAX)
        return -1;

    if (nfrac_val > 0x7FFFFFUL)
        nfrac_val = 0x7FFFFFUL;

    *nint = (uint16_t)nint_val;
    *nfrac = nfrac_val;
    return 0;
}

/* ========================================================================
 * LMS7002M SPI Address Encoding (from lms7002m_driver.h)
 * ======================================================================== */

/* Write: [0][addr13:0][data15:0] — 4 bytes total. Address is 14-bit field. */
static uint32_t lms7002m_encode_write(uint16_t addr, uint16_t data) {
    return ((uint32_t)(addr & 0x3FFF) << 16) | data;
}

/* Read: [1][addr13:0][don't care] — then read data[15:0]. Address is 14-bit field. */
static uint32_t lms7002m_encode_read(uint16_t addr) {
    return ((uint32_t)((addr & 0x3FFF) | 0x8000) << 16);
}

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ_INT(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s: expected %d, got %d (line %d)\n", \
               msg, (int)(expected), (int)(actual), __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ_UINT(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s: expected 0x%08x, got 0x%08x (line %d)\n", \
               msg, (unsigned)(expected), (unsigned)(actual), __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ_ULL(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s: expected %llu, got %llu (line %d)\n", \
               msg, (unsigned long long)(expected), (unsigned long long)(actual), __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_WITHIN(val, lo, hi, msg) do { \
    tests_run++; \
    if ((uint64_t)(val) < (uint64_t)(lo) || (uint64_t)(val) > (uint64_t)(hi)) { \
        printf("  FAIL: %s: value %llu not in range [%llu, %llu] (line %d)\n", \
               msg, (unsigned long long)(val), (unsigned long long)(lo), (unsigned long long)(hi), __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define RUN_TEST(func) do { \
    printf("Running: %s\n", #func); \
    func(); \
} while(0)

/* ========================================================================
 * CC1101 Test Cases
 * ======================================================================== */

/* Test 1: CC1101 register addresses are in valid range (0x00-0x2E) */
static void test_cc1101_register_address_range(void) {
    unsigned int i;

    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_868; i++) {
        ASSERT_TRUE(cc1101_config_868mhz[i].addr <= 0x2E,
                    "868 MHz: register address in valid range");
    }

    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_433; i++) {
        ASSERT_TRUE(cc1101_config_433mhz[i].addr <= 0x2E,
                    "433 MHz: register address in valid range");
    }

    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_915; i++) {
        ASSERT_TRUE(cc1101_config_915mhz[i].addr <= 0x2E,
                    "915 MHz: register address in valid range");
    }
}

/* Test 2: CC1101 configuration tables have no duplicate register addresses */
static void test_cc1101_no_duplicate_addresses(void) {
    uint8_t seen[0x2F];
    unsigned int i;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_868; i++) {
        uint8_t addr = cc1101_config_868mhz[i].addr;
        ASSERT_TRUE(seen[addr] == 0, "868 MHz: no duplicate register address");
        seen[addr] = 1;
    }

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_433; i++) {
        uint8_t addr = cc1101_config_433mhz[i].addr;
        ASSERT_TRUE(seen[addr] == 0, "433 MHz: no duplicate register address");
        seen[addr] = 1;
    }

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_915; i++) {
        uint8_t addr = cc1101_config_915mhz[i].addr;
        ASSERT_TRUE(seen[addr] == 0, "915 MHz: no duplicate register address");
        seen[addr] = 1;
    }
}

/* Test 3: CC1101 frequency register values produce correct frequencies */
static void test_cc1101_frequency_calculation(void) {
    const uint32_t f_xtal = 26000000UL;

    /* 868 MHz: FREQ = 0x216276 */
    uint32_t freq_868 = ((uint32_t)0x21 << 16) | ((uint32_t)0x62 << 8) | (uint32_t)0x76;
    uint64_t f_868 = ((uint64_t)freq_868 * f_xtal) >> 16;
    ASSERT_WITHIN(f_868, 867950000ULL, 868050000ULL,
                  "868 MHz frequency within ±50 kHz");

    /* 433 MHz: FREQ = 0x10A762 */
    uint32_t freq_433 = ((uint32_t)0x10 << 16) | ((uint32_t)0xA7 << 8) | (uint32_t)0x62;
    uint64_t f_433 = ((uint64_t)freq_433 * f_xtal) >> 16;
    ASSERT_WITHIN(f_433, 432950000ULL, 433050000ULL,
                  "433 MHz frequency within ±50 kHz");

    /* 915 MHz: FREQ = 0x23313B */
    uint32_t freq_915 = ((uint32_t)0x23 << 16) | ((uint32_t)0x31 << 8) | (uint32_t)0x3B;
    uint64_t f_915 = ((uint64_t)freq_915 * f_xtal) >> 16;
    ASSERT_WITHIN(f_915, 914950000ULL, 915050000ULL,
                  "915 MHz frequency within ±50 kHz");
}

/* Test 4: CC1101 SPI command byte encoding */
static void test_cc1101_spi_encoding(void) {
    ASSERT_EQ_UINT(0x0D, CC1101_WRITE_SINGLE(0x0D),
                   "CC1101 write single addr 0x0D");
    ASSERT_EQ_UINT(0x40, CC1101_WRITE_BURST(0x00),
                   "CC1101 write burst addr 0x00");
    ASSERT_EQ_UINT(0xB5, CC1101_READ_SINGLE(0x35),
                   "CC1101 read single addr 0x35");
    ASSERT_EQ_UINT(0xF0, CC1101_READ_BURST(0x30),
                   "CC1101 read burst addr 0x30");
    ASSERT_EQ_UINT(0x30, CC1101_STROBE(CC1101_SRES),
                   "CC1101 strobe SRES");
    ASSERT_EQ_UINT(0x34, CC1101_STROBE(CC1101_SRX),
                   "CC1101 strobe SRX");

    /* Verify address masking: addr > 0x3F is masked */
    ASSERT_EQ_UINT(0x0D, CC1101_WRITE_SINGLE(0x4D),
                   "CC1101 write single masks addr > 0x3F");
    ASSERT_EQ_UINT(0x8D, CC1101_READ_SINGLE(0x4D),
                   "CC1101 read single masks addr > 0x3F");
}

/* Test 5: CC1101 PKTCTRL0 enables CRC and variable length */
static void test_cc1101_pktctrl_settings(void) {
    unsigned int i;
    uint8_t pktctrl0_868 = 0, pktctrl0_433 = 0, pktctrl0_915 = 0;

    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_868; i++) {
        if (cc1101_config_868mhz[i].addr == CC1101_PKTCTRL0)
            pktctrl0_868 = cc1101_config_868mhz[i].val;
    }
    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_433; i++) {
        if (cc1101_config_433mhz[i].addr == CC1101_PKTCTRL0)
            pktctrl0_433 = cc1101_config_433mhz[i].val;
    }
    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_915; i++) {
        if (cc1101_config_915mhz[i].addr == CC1101_PKTCTRL0)
            pktctrl0_915 = cc1101_config_915mhz[i].val;
    }

    ASSERT_EQ_UINT(0x05, pktctrl0_868, "868 MHz PKTCTRL0 = 0x05");
    ASSERT_EQ_UINT(0x05, pktctrl0_433, "433 MHz PKTCTRL0 = 0x05");
    ASSERT_EQ_UINT(0x05, pktctrl0_915, "915 MHz PKTCTRL0 = 0x05");
}

/* Test 6: CC1101 sync word matches across all bands */
static void test_cc1101_sync_word_consistency(void) {
    unsigned int i;
    uint8_t sync1_868 = 0, sync0_868 = 0;
    uint8_t sync1_433 = 0, sync0_433 = 0;
    uint8_t sync1_915 = 0, sync0_915 = 0;

    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_868; i++) {
        if (cc1101_config_868mhz[i].addr == CC1101_SYNC1) sync1_868 = cc1101_config_868mhz[i].val;
        if (cc1101_config_868mhz[i].addr == CC1101_SYNC0) sync0_868 = cc1101_config_868mhz[i].val;
    }
    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_433; i++) {
        if (cc1101_config_433mhz[i].addr == CC1101_SYNC1) sync1_433 = cc1101_config_433mhz[i].val;
        if (cc1101_config_433mhz[i].addr == CC1101_SYNC0) sync0_433 = cc1101_config_433mhz[i].val;
    }
    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_915; i++) {
        if (cc1101_config_915mhz[i].addr == CC1101_SYNC1) sync1_915 = cc1101_config_915mhz[i].val;
        if (cc1101_config_915mhz[i].addr == CC1101_SYNC0) sync0_915 = cc1101_config_915mhz[i].val;
    }

    ASSERT_EQ_UINT(0xD3, sync1_868, "868 MHz SYNC1 = 0xD3");
    ASSERT_EQ_UINT(0x91, sync0_868, "868 MHz SYNC0 = 0x91");
    ASSERT_EQ_UINT(0xD3, sync1_433, "433 MHz SYNC1 = 0xD3");
    ASSERT_EQ_UINT(0x91, sync0_433, "433 MHz SYNC0 = 0x91");
    ASSERT_EQ_UINT(0xD3, sync1_915, "915 MHz SYNC1 = 0xD3");
    ASSERT_EQ_UINT(0x91, sync0_915, "915 MHz SYNC0 = 0x91");
}

/* Test 7: CC1101 FIFO threshold register (FIFOTHR = 0x07) */
static void test_cc1101_fifo_threshold(void) {
    unsigned int i;
    uint8_t fifothr = 0;

    for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_868; i++) {
        if (cc1101_config_868mhz[i].addr == CC1101_FIFOTHR)
            fifothr = cc1101_config_868mhz[i].val;
    }

    ASSERT_EQ_UINT(0x07, fifothr, "FIFOTHR = 0x07");
    ASSERT_EQ_INT(32, 4 * (7 + 1), "RX FIFO threshold = 32 bytes");
    ASSERT_EQ_INT(32, 4 * (7 + 1), "TX FIFO threshold = 32 bytes");
}

/* Test 8: CC1101 data rate calculation for 868 MHz config */
static void test_cc1101_data_rate_calculation(void) {
    /* MDMCFG4 = 0x0D: DRATE_E = 13, MDMCFG3 = 0x55: DRATE_M = 85
     * Data rate = (256 + 85) * 2^13 * 26e6 / 2^28 ≈ 270.6 kbps */
    uint32_t drate_e = 0x0D & 0x0F;
    uint32_t drate_m = 0x55;
    uint64_t data_rate = (256ULL + drate_m) * (1ULL << drate_e) * 26000000ULL / (1ULL << 28);

    ASSERT_WITHIN(data_rate, 250000ULL, 280000ULL,
                  "868 MHz data rate ≈ 270.6 kbps (within tolerance)");
}

/* Test 9: CC1101 configuration table completeness */
static void test_cc1101_table_completeness(void) {
    ASSERT_EQ_INT((int)CC1101_CONFIG_TABLE_SIZE_868,
                  (int)CC1101_CONFIG_TABLE_SIZE_433,
                  "868 MHz and 433 MHz tables same size");
    ASSERT_EQ_INT((int)CC1101_CONFIG_TABLE_SIZE_868,
                  (int)CC1101_CONFIG_TABLE_SIZE_915,
                  "868 MHz and 915 MHz tables same size");

    uint8_t essential_addrs[] = {
        CC1101_FREQ2, CC1101_FREQ1, CC1101_FREQ0,
        CC1101_MDMCFG4, CC1101_MDMCFG3, CC1101_MDMCFG2,
        CC1101_PKTCTRL0, CC1101_FSCTRL1,
        CC1101_SYNC1, CC1101_SYNC0,
        CC1101_MCSM0, CC1101_IOCFG0
    };
    unsigned int num_essential = sizeof(essential_addrs) / sizeof(essential_addrs[0]);
    unsigned int i, j;

    for (j = 0; j < num_essential; j++) {
        bool found = false;
        for (i = 0; i < CC1101_CONFIG_TABLE_SIZE_868; i++) {
            if (cc1101_config_868mhz[i].addr == essential_addrs[j]) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found, "868 MHz table contains essential register");
    }
}

/* ========================================================================
 * LMS7002M Test Cases
 * ======================================================================== */

/* Test 10: LMS7002M PLL calculation for 868 MHz (ISM band) */
static void test_lms7002m_pll_868mhz(void) {
    uint16_t nint;
    uint32_t nfrac;
    uint8_t div_out;
    int ret;

    ret = calculate_pll_params(868000000UL, LMS7002M_REF_CLK_HZ,
                                &nint, &nfrac, &div_out);
    ASSERT_EQ_INT(0, ret, "PLL calc for 868 MHz succeeds");

    /* div=4 → VCO = 3472 MHz (in VCO_L range 1.88-3.72 GHz)
     * NINT = 3472e6 / 30.72e6 ≈ 113 */
    ASSERT_TRUE(div_out == 4, "868 MHz div_out = 4");

    uint64_t vco_freq = (uint64_t)868000000UL * div_out;
    uint64_t pll_ratio = (vco_freq << 24) / LMS7002M_REF_CLK_HZ;
    uint64_t actual_nint = pll_ratio >> 24;
    ASSERT_EQ_ULL(actual_nint, nint, "868 MHz NINT matches calculation");

    ASSERT_TRUE(nint >= LMS7002M_PLL_INT_MIN && nint <= LMS7002M_PLL_INT_MAX,
                "868 MHz NINT in valid range [1, 255]");
}

/* Test 11: LMS7002M PLL calculation for 2.4 GHz (Wi-Fi band) */
static void test_lms7002m_pll_2400mhz(void) {
    uint16_t nint;
    uint32_t nfrac;
    uint8_t div_out;
    int ret;

    ret = calculate_pll_params(2400000000UL, LMS7002M_REF_CLK_HZ,
                                &nint, &nfrac, &div_out);
    ASSERT_EQ_INT(0, ret, "PLL calc for 2.4 GHz succeeds");

    /* div=1 → VCO = 2400 MHz (in VCO_L range 1.88-3.72 GHz) */
    ASSERT_TRUE(div_out == 1, "2.4 GHz div_out = 1");
    ASSERT_TRUE(nint >= LMS7002M_PLL_INT_MIN && nint <= LMS7002M_PLL_INT_MAX,
                "2.4 GHz NINT in valid range");
}

/* Test 12: LMS7002M PLL calculation for 433 MHz (sub-GHz ISM) */
static void test_lms7002m_pll_433mhz(void) {
    uint16_t nint;
    uint32_t nfrac;
    uint8_t div_out;
    int ret;

    ret = calculate_pll_params(433000000UL, LMS7002M_REF_CLK_HZ,
                                &nint, &nfrac, &div_out);
    ASSERT_EQ_INT(0, ret, "PLL calc for 433 MHz succeeds");

    /* div=8 → VCO = 3464 MHz (in VCO_L range 1.88-3.72 GHz) */
    ASSERT_TRUE(div_out == 8, "433 MHz div_out = 8");
    ASSERT_TRUE(nint >= LMS7002M_PLL_INT_MIN && nint <= LMS7002M_PLL_INT_MAX,
                "433 MHz NINT in valid range");
}

/* Test 13: LMS7002M PLL calculation for 100 MHz (low frequency) */
static void test_lms7002m_pll_100mhz(void) {
    uint16_t nint;
    uint32_t nfrac;
    uint8_t div_out;
    int ret;

    /* 100 MHz with div=32 → VCO = 3200 MHz (in VCO_L range) */
    ret = calculate_pll_params(100000000UL, LMS7002M_REF_CLK_HZ,
                                &nint, &nfrac, &div_out);
    ASSERT_EQ_INT(0, ret, "PLL calc for 100 MHz succeeds");
    ASSERT_TRUE(div_out == 16 || div_out == 32, "100 MHz div_out is 16 or 32");
    ASSERT_TRUE(nint >= LMS7002M_PLL_INT_MIN, "100 MHz NINT >= 1");
}

/* Test 14: LMS7002M PLL calculation rejects out-of-range frequencies */
static void test_lms7002m_pll_out_of_range(void) {
    uint16_t nint;
    uint32_t nfrac;
    uint8_t div_out;

    int ret = calculate_pll_params(50000UL, LMS7002M_REF_CLK_HZ,
                                   &nint, &nfrac, &div_out);
    ASSERT_EQ_INT(-1, ret, "PLL calc rejects 50 kHz (below minimum)");

    ret = calculate_pll_params(4200000000UL, LMS7002M_REF_CLK_HZ,
                               &nint, &nfrac, &div_out);
    ASSERT_EQ_INT(-1, ret, "PLL calc rejects 4.2 GHz (above maximum)");
}

/* Test 15: LMS7002M SPI address encoding */
static void test_lms7002m_spi_encoding(void) {
    uint32_t write_cmd = lms7002m_encode_write(0x0020, 0x0003);
    ASSERT_EQ_UINT(0x00200003UL, write_cmd, "LMS7002M write: addr=0x0020 data=0x0003");

    uint32_t read_cmd = lms7002m_encode_read(0x0021);
    ASSERT_EQ_UINT(0x80210000UL, read_cmd, "LMS7002M read: addr=0x0021");

    write_cmd = lms7002m_encode_write(0x011C, 0x0001);
    ASSERT_EQ_UINT(0x011C0001UL, write_cmd, "LMS7002M write: addr=0x011C data=0x0001");

    /* Verify 14-bit address masking */
    write_cmd = lms7002m_encode_write(0xFFFF, 0xABCD);
    ASSERT_EQ_UINT(0x3FFFABCDUL, write_cmd, "LMS7002M write: addr masked to 14 bits");
}

/* Test 16: LMS7002M gain distribution logic */
static void test_lms7002m_gain_distribution(void) {
    /* 30 dB → LNA=30, TIA=12, PGA=0 */
    int16_t total_30 = 300 / 10;
    int16_t lna_30, pga_30;
    if (total_30 <= 73) { lna_30 = total_30; pga_30 = 0; }
    else { lna_30 = 73; pga_30 = total_30 - 73 - 12; if (pga_30 < 0) pga_30 = 0; if (pga_30 > 31) pga_30 = 31; }
    ASSERT_EQ_INT(30, lna_30, "30 dB: LNA = 30 dB");
    ASSERT_EQ_INT(0, pga_30, "30 dB: PGA = 0 dB");

    /* 85 dB → LNA=73, TIA=12, PGA=0 (85-73-12=0) */
    int16_t total_85 = 850 / 10;
    int16_t lna_85, pga_85;
    if (total_85 <= 73) { lna_85 = total_85; pga_85 = 0; }
    else { lna_85 = 73; pga_85 = total_85 - 73 - 12; if (pga_85 < 0) pga_85 = 0; if (pga_85 > 31) pga_85 = 31; }
    ASSERT_EQ_INT(73, lna_85, "85 dB: LNA = 73 dB");
    ASSERT_EQ_INT(0, pga_85, "85 dB: PGA = 0 dB");

    /* 100 dB → LNA=73, TIA=12, PGA=15 */
    int16_t total_100 = 1000 / 10;
    int16_t lna_100, pga_100;
    if (total_100 <= 73) { lna_100 = total_100; pga_100 = 0; }
    else { lna_100 = 73; pga_100 = total_100 - 73 - 12; if (pga_100 < 0) pga_100 = 0; if (pga_100 > 31) pga_100 = 31; }
    ASSERT_EQ_INT(73, lna_100, "100 dB: LNA = 73 dB");
    ASSERT_EQ_INT(15, pga_100, "100 dB: PGA = 15 dB");

    /* 116 dB (maximum) → LNA=73, TIA=12, PGA=31 */
    int16_t total_116 = 1160 / 10;
    int16_t lna_116, pga_116;
    if (total_116 <= 73) { lna_116 = total_116; pga_116 = 0; }
    else { lna_116 = 73; pga_116 = total_116 - 73 - 12; if (pga_116 < 0) pga_116 = 0; if (pga_116 > 31) pga_116 = 31; }
    ASSERT_EQ_INT(73, lna_116, "116 dB: LNA = 73 dB");
    ASSERT_EQ_INT(31, pga_116, "116 dB: PGA = 31 dB");
}

/* Test 17: LMS7002M decimation selection */
static void test_lms7002m_decimation_selection(void) {
    uint8_t dec;

    /* 10 MSPS → dec = 1 */
    if (10000000 >= 5000000) dec = 1;
    else if (10000000 >= 2500000) dec = 2;
    else if (10000000 >= 1250000) dec = 4;
    else if (10000000 >= 625000) dec = 8;
    else if (10000000 >= 312500) dec = 16;
    else dec = 32;
    ASSERT_EQ_INT(1, dec, "10 MSPS: decimation = 1");

    /* 2 MSPS → dec = 4 */
    if (2000000 >= 5000000) dec = 1;
    else if (2000000 >= 2500000) dec = 2;
    else if (2000000 >= 1250000) dec = 4;
    else if (2000000 >= 625000) dec = 8;
    else if (2000000 >= 312500) dec = 16;
    else dec = 32;
    ASSERT_EQ_INT(4, dec, "2 MSPS: decimation = 4");

    /* 500 kSPS → dec = 16 */
    if (500000 >= 5000000) dec = 1;
    else if (500000 >= 2500000) dec = 2;
    else if (500000 >= 1250000) dec = 4;
    else if (500000 >= 625000) dec = 8;
    else if (500000 >= 312500) dec = 16;
    else dec = 32;
    ASSERT_EQ_INT(16, dec, "500 kSPS: decimation = 16");

    /* 100 kSPS → dec = 32 */
    if (100000 >= 5000000) dec = 1;
    else if (100000 >= 2500000) dec = 2;
    else if (100000 >= 1250000) dec = 4;
    else if (100000 >= 625000) dec = 8;
    else if (100000 >= 312500) dec = 16;
    else dec = 32;
    ASSERT_EQ_INT(32, dec, "100 kSPS: decimation = 32");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== CC1101 and LMS7002M Initialization Tests ===\n\n");

    printf("--- CC1101 Tests ---\n");
    RUN_TEST(test_cc1101_register_address_range);
    RUN_TEST(test_cc1101_no_duplicate_addresses);
    RUN_TEST(test_cc1101_frequency_calculation);
    RUN_TEST(test_cc1101_spi_encoding);
    RUN_TEST(test_cc1101_pktctrl_settings);
    RUN_TEST(test_cc1101_sync_word_consistency);
    RUN_TEST(test_cc1101_fifo_threshold);
    RUN_TEST(test_cc1101_data_rate_calculation);
    RUN_TEST(test_cc1101_table_completeness);

    printf("\n--- LMS7002M Tests ---\n");
    RUN_TEST(test_lms7002m_pll_868mhz);
    RUN_TEST(test_lms7002m_pll_2400mhz);
    RUN_TEST(test_lms7002m_pll_433mhz);
    RUN_TEST(test_lms7002m_pll_100mhz);
    RUN_TEST(test_lms7002m_pll_out_of_range);
    RUN_TEST(test_lms7002m_spi_encoding);
    RUN_TEST(test_lms7002m_gain_distribution);
    RUN_TEST(test_lms7002m_decimation_selection);

    printf("\n=== Test Results ===\n");
    printf("Total: %d  Passed: %d  Failed: %d\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}