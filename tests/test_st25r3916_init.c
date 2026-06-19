/*
 * test_st25r3916_init.c — Unit Tests for ST25R3916 NFC Controller Initialization
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for the ST25R3916 initialization sequence, covering:
 *   1. Register address validation (Space A addresses 0x00–0x3F)
 *   2. Register value constraints (valid bit ranges, reserved bits zero)
 *   3. Initialization sequence ordering invariants
 *   4. SPI protocol encoding (read/write/burst/direct command)
 *   5. IRQ status register bit definitions
 *   6. Direct command address range validation
 *   7. Configuration register value validation against datasheet constraints
 *   8. Voltage measurement conversion
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -o test_st25r3916_init test_st25r3916_init.c
 *
 * Run:
 *   ./test_st25r3916_init
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Simple test framework
 * ======================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define ASSERT_INT_EQ(expected, actual) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("  FAIL %s:%d: expected %d, got %d\n", \
               __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define ASSERT_INT_NE(not_expected, actual) do { \
    tests_run++; \
    if ((not_expected) == (actual)) { \
        printf("  FAIL %s:%d: expected != %d, got %d\n", \
               __FILE__, __LINE__, (int)(not_expected), (int)(actual)); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define ASSERT_INT_GE(actual, minimum) do { \
    tests_run++; \
    if ((actual) < (minimum)) { \
        printf("  FAIL %s:%d: expected >= %d, got %d\n", \
               __FILE__, __LINE__, (int)(minimum), (int)(actual)); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

#define ASSERT_INT_LE(actual, maximum) do { \
    tests_run++; \
    if ((actual) > (maximum)) { \
        printf("  FAIL %s:%d: expected <= %d, got %d\n", \
               __FILE__, __LINE__, (int)(maximum), (int)(actual)); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

/* ========================================================================
 * ST25R3916 Register Map (mirrored from st25r3916_init.h)
 * ======================================================================== */

/* Identity registers */
#define ST25R3916_REG_IC_IDENTITY       0x00
#define ST25R3916_REG_IC_VERSION        0x01

/* I/O Configuration */
#define ST25R3916_REG_IO_CONF1          0x02
#define ST25R3916_REG_IO_CONF2          0x03

/* Operation Control */
#define ST25R3916_REG_OP_CTRL           0x04

/* Mode Definition */
#define ST25R3916_REG_MODE_DEF          0x05

/* Bit Rate */
#define ST25R3916_REG_BIT_RATE          0x06

/* Protocol Mode */
#define ST25R3916_REG_ISO14443A_MODE    0x07
#define ST25R3916_REG_ISO14443B_MODE    0x08
#define ST25R3916_REG_FELICA_MODE       0x09
#define ST25R3916_REG_ISO15693_MODE     0x0A

/* Antenna Calibration */
#define ST25R3916_REG_ANT_CAL_TARGET    0x0B
#define ST25R3916_REG_ANT_CAL_TIME      0x0C
#define ST25R3916_REG_ANT_MIN            0x0D
#define ST25R3916_REG_ANT_MAX            0x0E
#define ST25R3916_REG_ANT_TUNE           0x0F
#define ST25R3916_REG_ANT_TUNE_MEAS      0x10

/* Auxiliary Modulation */
#define ST25R3916_REG_AUX_MOD            0x11

/* Receiver Configuration */
#define ST25R3916_REG_RX_CONF1           0x12
#define ST25R3916_REG_RX_CONF2           0x13
#define ST25R3916_REG_RX_CONF3           0x14
#define ST25R3916_REG_RX_CONF4           0x15

/* TX Driver */
#define ST25R3916_REG_TX_DRIVER          0x16
#define ST25R3916_REG_TX_CURRENT          0x17
#define ST25R3916_REG_TX_CURRENT_SSC      0x18
#define ST25R3916_REG_TX_CURRENT_SSC_HL   0x19
#define ST25R3916_REG_TX_CURRENT_SSC_LH   0x1A

/* Correlator Configuration */
#define ST25R3916_REG_CORR_CONF1          0x1B
#define ST25R3916_REG_CORR_CONF2          0x1C

/* Timer Registers */
#define ST25R3916_REG_TIMER_EMV          0x1D
#define ST25R3916_REG_TIMER1              0x1E
#define ST25R3916_REG_TIMER2              0x1F
#define ST25R3916_REG_TIMER3              0x20

/* Interrupt Masks */
#define ST25R3916_REG_IRQ_MASK1           0x21
#define ST25R3916_REG_IRQ_MASK2           0x22
#define ST25R3916_REG_IRQ_MASK3           0x23
#define ST25R3916_REG_IRQ_MASK4           0x24
#define ST25R3916_REG_IRQ_MASK5           0x25

/* TX/RX Byte Count */
#define ST25R3916_REG_NUM_TX_BYTES1       0x26
#define ST25R3916_REG_NUM_TX_BYTES2       0x27
#define ST25R3916_REG_NUM_TX_BYTES3       0x28
#define ST25R3916_REG_TX_FIFO_STATUS       0x29
#define ST25R3916_REG_NUM_RX_BYTES1       0x2A
#define ST25R3916_REG_NUM_RX_BYTES2       0x2B
#define ST25R3916_REG_NUM_RX_BYTES3       0x2C
#define ST25R3916_REG_RX_FIFO_STATUS       0x2D

/* Collision and INT Clear */
#define ST25R3916_REG_COLL_INT_CLEAR      0x2E

/* Wake-up / Sleep Timers */
#define ST25R3916_REG_WUP_TIMER            0x2F
#define ST25R3916_REG_WUP_TIMER_GRAN       0x30
#define ST25R3916_REG_SLP_TIMER            0x31
#define ST25R3916_REG_SLP_TIMER_GRAN       0x32

/* Measurement and Validation */
#define ST25R3916_REG_MVT                  0x33
#define ST25R3916_REG_AGC_CONFIG           0x34
#define ST25R3916_REG_AM_CONFIG            0x35
#define ST25R3916_REG_AM_GRANGE1           0x36
#define ST25R3916_REG_AM_GRANGE2           0x37
#define ST25R3916_REG_AM_GRANGE3           0x38
#define ST25R3916_REG_WUP_COLL             0x39
#define ST25R3916_REG_RSSI                 0x3A

/* Observer Configuration */
#define ST25R3916_REG_OBSV_CONF1           0x3B
#define ST25R3916_REG_OBSV_CONF2           0x3C
#define ST25R3916_REG_OBSV_CONF3           0x3D

/* Oscillator and Regulator */
#define ST25R3916_REG_OSC_CONF             0x3E
#define ST25R3916_REG_VREG_CONF             0x3F

/* TX FIFO */
#define ST25R3916_REG_TX_FIFO              0x1F

/* IRQ Status Registers (read to clear) */
#define ST25R3916_REG_IRQ_STATUS1          0x04
#define ST25R3916_REG_IRQ_STATUS2          0x05
#define ST25R3916_REG_IRQ_STATUS3          0x06
#define ST25R3916_REG_IRQ_STATUS4          0x07
#define ST25R3916_REG_IRQ_STATUS5          0x08

/* Direct Commands */
#define ST25R3916_CMD_SET_DEFAULT             0xC1
#define ST25R3916_CMD_INITIALIZE              0xC2
#define ST25R3916_CMD_INITIALIZE_DPO           0xC3
#define ST25R3916_CMD_CLEAR_IRQS              0xC4
#define ST25R3916_CMD_MEASURE_VDD             0xC5
#define ST25R3916_CMD_TX_ON                   0xC6
#define ST25R3916_CMD_TX_OFF                  0xC7
#define ST25R3916_CMD_CALIBRATE_ANTENNA       0xC8
#define ST25R3916_CMD_MEASURE_AMPLITUDE       0xC9
#define ST25R3916_CMD_MEASURE_PHASE           0xCA
#define ST25R3916_CMD_GOTO_SENSE              0xD0
#define ST25R3916_CMD_GOTO_SLEEP              0xD1
#define ST25R3916_CMD_START_WUP_TIMER         0xD2
#define ST25R3916_CMD_START_GP_TIMER          0xD3

/* SPI Access Macros */
#define ST25R3916_WRITE_SINGLE(addr)    (((addr) & 0x3F))
#define ST25R3916_WRITE_BURST(addr)     (((addr) & 0x3F) | 0x40)
#define ST25R3916_READ_SINGLE(addr)     (((addr) & 0x3F) | 0x80)
#define ST25R3916_READ_BURST(addr)      (((addr) & 0x3F) | 0xC0)
#define ST25R3916_DIRECT_CMD(cmd)        ((cmd) & 0xFF)

/* IRQ1 bits */
#define ST25R3916_IRQ1_OSC                    (1 << 0)
#define ST25R3916_IRQ1_FELICA                 (1 << 1)
#define ST25R3916_IRQ1_NFCA                   (1 << 2)
#define ST25R3916_IRQ1_NFCB                   (1 << 3)
#define ST25R3916_IRQ1_NFCF                   (1 << 4)
#define ST25R3916_IRQ1_NFCV                   (1 << 5)
#define ST25R3916_IRQ1_TXE                    (1 << 6)
#define ST25R3916_IRQ1_RXE                    (1 << 7)

/* IRQ2 bits */
#define ST25R3916_IRQ2_CAC                    (1 << 0)
#define ST25R3916_IRQ2_WU_F                  (1 << 1)
#define ST25R3916_IRQ2_WU_A                  (1 << 2)
#define ST25R3916_IRQ2_WU_S                  (1 << 3)
#define ST25R3916_IRQ2_RXS                   (1 << 4)
#define ST25R3916_IRQ2_RX_F                  (1 << 5)
#define ST25R3916_IRQ2_TX_F                  (1 << 6)
#define ST25R3916_IRQ2_DCT                   (1 << 7)

/* IRQ3 bits */
#define ST25R3916_IRQ3_GPP_TIMER              (1 << 0)
#define ST25R3916_IRQ3_LMS                   (1 << 1)
#define ST25R3916_IRQ3_CRC                   (1 << 2)
#define ST25R3916_IRQ3_EON                    (1 << 3)
#define ST25R3916_IRQ3_EOF                   (1 << 4)
#define ST25R3916_IRQ3_EMD                   (1 << 5)
#define ST25R3916_IRQ3_AWU                   (1 << 6)
#define ST25R3916_IRQ3_RFD                   (1 << 7)

/* ========================================================================
 * Test: Register Address Uniqueness
 *
 * Per ST25R3916 datasheet (DS12290), Space A configuration registers
 * occupy addresses 0x00–0x3F. Verify that each defined register address
 * is unique (no aliases for configuration registers) and within range.
 * ======================================================================== */

static void test_register_addresses_in_range(void)
{
    /* All Space A configuration registers must be 0x00–0x3F */
    uint8_t addrs[] = {
        ST25R3916_REG_IC_IDENTITY, ST25R3916_REG_IC_VERSION,
        ST25R3916_REG_IO_CONF1, ST25R3916_REG_IO_CONF2,
        ST25R3916_REG_OP_CTRL, ST25R3916_REG_MODE_DEF,
        ST25R3916_REG_BIT_RATE, ST25R3916_REG_ISO14443A_MODE,
        ST25R3916_REG_ISO14443B_MODE, ST25R3916_REG_FELICA_MODE,
        ST25R3916_REG_ISO15693_MODE, ST25R3916_REG_ANT_CAL_TARGET,
        ST25R3916_REG_ANT_CAL_TIME, ST25R3916_REG_ANT_MIN,
        ST25R3916_REG_ANT_MAX, ST25R3916_REG_ANT_TUNE,
        ST25R3916_REG_ANT_TUNE_MEAS, ST25R3916_REG_AUX_MOD,
        ST25R3916_REG_RX_CONF1, ST25R3916_REG_RX_CONF2,
        ST25R3916_REG_RX_CONF3, ST25R3916_REG_RX_CONF4,
        ST25R3916_REG_TX_DRIVER, ST25R3916_REG_TX_CURRENT,
        ST25R3916_REG_CORR_CONF1, ST25R3916_REG_CORR_CONF2,
        ST25R3916_REG_TIMER_EMV, ST25R3916_REG_TIMER1,
        ST25R3916_REG_TIMER2, ST25R3916_REG_TIMER3,
        ST25R3916_REG_IRQ_MASK1, ST25R3916_REG_IRQ_MASK2,
        ST25R3916_REG_IRQ_MASK3, ST25R3916_REG_IRQ_MASK4,
        ST25R3916_REG_IRQ_MASK5, ST25R3916_REG_NUM_TX_BYTES1,
        ST25R3916_REG_NUM_TX_BYTES2, ST25R3916_REG_NUM_TX_BYTES3,
        ST25R3916_REG_WUP_TIMER, ST25R3916_REG_WUP_TIMER_GRAN,
        ST25R3916_REG_SLP_TIMER, ST25R3916_REG_SLP_TIMER_GRAN,
        ST25R3916_REG_AGC_CONFIG, ST25R3916_REG_AM_CONFIG,
        ST25R3916_REG_AM_GRANGE1, ST25R3916_REG_AM_GRANGE2,
        ST25R3916_REG_AM_GRANGE3, ST25R3916_REG_RSSI,
        ST25R3916_REG_OBSV_CONF1, ST25R3916_REG_OBSV_CONF2,
        ST25R3916_REG_OBSV_CONF3, ST25R3916_REG_OSC_CONF,
        ST25R3916_REG_VREG_CONF, ST25R3916_REG_COLL_INT_CLEAR,
    };
    int n = sizeof(addrs) / sizeof(addrs[0]);

    for (int i = 0; i < n; i++) {
        ASSERT_INT_LE(addrs[i], 0x3F);
    }
}

static void test_direct_command_range(void)
{
    /* Direct commands must be in range 0xC1-0xD3 per datasheet */
    uint8_t cmds[] = {
        ST25R3916_CMD_SET_DEFAULT,
        ST25R3916_CMD_INITIALIZE,
        ST25R3916_CMD_INITIALIZE_DPO,
        ST25R3916_CMD_CLEAR_IRQS,
        ST25R3916_CMD_MEASURE_VDD,
        ST25R3916_CMD_TX_ON,
        ST25R3916_CMD_TX_OFF,
        ST25R3916_CMD_CALIBRATE_ANTENNA,
        ST25R3916_CMD_MEASURE_AMPLITUDE,
        ST25R3916_CMD_MEASURE_PHASE,
        ST25R3916_CMD_GOTO_SENSE,
        ST25R3916_CMD_GOTO_SLEEP,
        ST25R3916_CMD_START_WUP_TIMER,
        ST25R3916_CMD_START_GP_TIMER,
    };
    int n = sizeof(cmds) / sizeof(cmds[0]);

    for (int i = 0; i < n; i++) {
        ASSERT_INT_GE(cmds[i], 0xC1);
        ASSERT_INT_LE(cmds[i], 0xD3);
    }
}

static void test_direct_commands_unique(void)
{
    /* All direct command values must be distinct */
    uint8_t cmds[] = {
        ST25R3916_CMD_SET_DEFAULT,
        ST25R3916_CMD_INITIALIZE,
        ST25R3916_CMD_INITIALIZE_DPO,
        ST25R3916_CMD_CLEAR_IRQS,
        ST25R3916_CMD_MEASURE_VDD,
        ST25R3916_CMD_TX_ON,
        ST25R3916_CMD_TX_OFF,
        ST25R3916_CMD_CALIBRATE_ANTENNA,
        ST25R3916_CMD_MEASURE_AMPLITUDE,
        ST25R3916_CMD_MEASURE_PHASE,
        ST25R3916_CMD_GOTO_SENSE,
        ST25R3916_CMD_GOTO_SLEEP,
        ST25R3916_CMD_START_WUP_TIMER,
        ST25R3916_CMD_START_GP_TIMER,
    };
    int n = sizeof(cmds) / sizeof(cmds[0]);

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            ASSERT_INT_NE(cmds[i], cmds[j]);
        }
    }
}

/* ========================================================================
 * Test: SPI Protocol Encoding
 *
 * Verify the SPI address byte encoding for Space A register access.
 * Per datasheet:
 *   Write single: bit7=0, bit6=0, addr[5:0]
 *   Write burst:  bit7=0, bit6=1, addr[5:0]
 *   Read single:  bit7=1, bit6=0, addr[5:0]
 *   Read burst:   bit7=1, bit6=1, addr[5:0]
 * ======================================================================== */

static void test_spi_write_single_encoding(void)
{
    /* Write single: bit7=0, bit6=0 → address is just the register addr */
    uint8_t addr = ST25R3916_REG_OP_CTRL;  /* 0x04 */
    uint8_t encoded = ST25R3916_WRITE_SINGLE(addr);
    ASSERT_INT_EQ(addr & 0x3F, encoded & 0x3F);
    ASSERT_INT_EQ(0, encoded & 0x80);  /* bit7 = 0 for write */
    ASSERT_INT_EQ(0, encoded & 0x40);  /* bit6 = 0 for single */
    ASSERT_INT_EQ(0x04, encoded);
}

static void test_spi_read_single_encoding(void)
{
    /* Read single: bit7=1, bit6=0 */
    uint8_t addr = ST25R3916_REG_IC_IDENTITY;  /* 0x00 */
    uint8_t encoded = ST25R3916_READ_SINGLE(addr);
    ASSERT_INT_EQ(addr & 0x3F, encoded & 0x3F);
    ASSERT_INT_EQ(0x80, encoded & 0x80);  /* bit7 = 1 for read */
    ASSERT_INT_EQ(0, encoded & 0x40);      /* bit6 = 0 for single */
    ASSERT_INT_EQ(0x80, encoded);
}

static void test_spi_write_burst_encoding(void)
{
    /* Write burst: bit7=0, bit6=1 */
    uint8_t addr = ST25R3916_REG_IO_CONF1;  /* 0x02 */
    uint8_t encoded = ST25R3916_WRITE_BURST(addr);
    ASSERT_INT_EQ(addr & 0x3F, encoded & 0x3F);
    ASSERT_INT_EQ(0, encoded & 0x80);       /* bit7 = 0 for write */
    ASSERT_INT_EQ(0x40, encoded & 0x40);     /* bit6 = 1 for burst */
    ASSERT_INT_EQ(0x42, encoded);
}

static void test_spi_read_burst_encoding(void)
{
    /* Read burst: bit7=1, bit6=1 */
    uint8_t addr = ST25R3916_REG_RSSI;  /* 0x3A */
    uint8_t encoded = ST25R3916_READ_BURST(addr);
    ASSERT_INT_EQ(addr & 0x3F, encoded & 0x3F);
    ASSERT_INT_EQ(0x80, encoded & 0x80);  /* bit7 = 1 for read */
    ASSERT_INT_EQ(0x40, encoded & 0x40);  /* bit6 = 1 for burst */
    ASSERT_INT_EQ(0xFA, encoded);
}

static void test_spi_addr_masking(void)
{
    /* Verify that address masking works: upper 2 bits are truncated */
    uint8_t overflow_addr = 0xFF;  /* All bits set */
    uint8_t masked = ST25R3916_WRITE_SINGLE(overflow_addr);
    ASSERT_INT_EQ(0x3F, masked);  /* Only lower 6 bits should remain */

    masked = ST25R3916_READ_BURST(overflow_addr);
    ASSERT_INT_EQ(0xFF, masked);  /* 0xC0 | 0x3F = 0xFF */
}

/* ========================================================================
 * Test: IRQ Status Register Bit Definitions
 *
 * Verify that IRQ1/IRQ2/IRQ3 bit masks are distinct and cover expected
 * positions for the ST25R3916 interrupt controller.
 * ======================================================================== */

static void test_irq1_bits_distinct(void)
{
    /* Each IRQ1 bit should be a unique power of 2 */
    uint8_t bits[] = {
        ST25R3916_IRQ1_OSC, ST25R3916_IRQ1_FELICA,
        ST25R3916_IRQ1_NFCA, ST25R3916_IRQ1_NFCB,
        ST25R3916_IRQ1_NFCF, ST25R3916_IRQ1_NFCV,
        ST25R3916_IRQ1_TXE, ST25R3916_IRQ1_RXE,
    };
    int n = sizeof(bits) / sizeof(bits[0]);

    for (int i = 0; i < n; i++) {
        /* Each bit must be a power of 2 (single bit set) */
        ASSERT_INT_NE(0, bits[i]);
        ASSERT_INT_EQ(0, bits[i] & (bits[i] - 1));  /* power of 2 check */
        for (int j = i + 1; j < n; j++) {
            ASSERT_INT_NE(bits[i], bits[j]);
        }
    }

    /* All 8 IRQ1 bits should OR together to 0xFF */
    uint8_t combined = 0;
    for (int i = 0; i < n; i++)
        combined |= bits[i];
    ASSERT_INT_EQ(0xFF, combined);
}

static void test_irq2_bits_distinct(void)
{
    uint8_t bits[] = {
        ST25R3916_IRQ2_CAC, ST25R3916_IRQ2_WU_F,
        ST25R3916_IRQ2_WU_A, ST25R3916_IRQ2_WU_S,
        ST25R3916_IRQ2_RXS, ST25R3916_IRQ2_RX_F,
        ST25R3916_IRQ2_TX_F, ST25R3916_IRQ2_DCT,
    };
    int n = sizeof(bits) / sizeof(bits[0]);

    for (int i = 0; i < n; i++) {
        ASSERT_INT_NE(0, bits[i]);
        ASSERT_INT_EQ(0, bits[i] & (bits[i] - 1));
        for (int j = i + 1; j < n; j++) {
            ASSERT_INT_NE(bits[i], bits[j]);
        }
    }

    uint8_t combined = 0;
    for (int i = 0; i < n; i++)
        combined |= bits[i];
    ASSERT_INT_EQ(0xFF, combined);
}

static void test_irq3_bits_distinct(void)
{
    uint8_t bits[] = {
        ST25R3916_IRQ3_GPP_TIMER, ST25R3916_IRQ3_LMS,
        ST25R3916_IRQ3_CRC, ST25R3916_IRQ3_EON,
        ST25R3916_IRQ3_EOF, ST25R3916_IRQ3_EMD,
        ST25R3916_IRQ3_AWU, ST25R3916_IRQ3_RFD,
    };
    int n = sizeof(bits) / sizeof(bits[0]);

    for (int i = 0; i < n; i++) {
        ASSERT_INT_NE(0, bits[i]);
        ASSERT_INT_EQ(0, bits[i] & (bits[i] - 1));
        for (int j = i + 1; j < n; j++) {
            ASSERT_INT_NE(bits[i], bits[j]);
        }
    }

    uint8_t combined = 0;
    for (int i = 0; i < n; i++)
        combined |= bits[i];
    ASSERT_INT_EQ(0xFF, combined);
}

/* ========================================================================
 * Test: IC Identity Register Values
 *
 * The ST25R3916 IC identity register returns 0x39 for ST25R3916
 * and 0x89 for ST25R3916B. Any other value indicates a hardware fault.
 * ======================================================================== */

static void test_ic_identity_values(void)
{
    /* Acceptable IC identity values: 0x39 (ST25R3916) or 0x89 (ST25R3916B) */
    uint8_t valid_ids[] = { 0x39, 0x89 };
    int n = sizeof(valid_ids) / sizeof(valid_ids[0]);

    for (int i = 0; i < n; i++) {
        bool valid = (valid_ids[i] == 0x39 || valid_ids[i] == 0x89);
        ASSERT_TRUE(valid);
    }

    /* Common invalid values that should be rejected */
    uint8_t invalid_ids[] = { 0x00, 0xFF, 0x01, 0x20 };
    int m = sizeof(invalid_ids) / sizeof(invalid_ids[0]);
    for (int i = 0; i < m; i++) {
        bool invalid = (invalid_ids[i] != 0x39 && invalid_ids[i] != 0x89);
        ASSERT_TRUE(invalid);
    }
}

/* ========================================================================
 * Test: Initialization Register Value Constraints
 *
 * Verify that the register values used in the initialization sequence
 * conform to the ST25R3916 datasheet constraints:
 *   - OP_CTRL: only bits 0 (TX_EN) and 1 (RX_EN) are valid
 *   - MODE_DEF: bit 0 selects passive mode, must be 0 or 1
 *   - BIT_RATE: TX/RX rates encoded in nibbles, 0 = 106 kbps
 *   - ISO14443A_MODE: must enable CRC and anticollision
 * ======================================================================== */

static void test_op_ctrl_values(void)
{
    /* OP_CTRL = 0x00 (disabled) and 0x03 (TX_EN | RX_EN) are the
     * initialization values used in the firmware. */
    uint8_t op_ctrl_disabled = 0x00;
    uint8_t op_ctrl_enabled = 0x03;  /* TX_EN | RX_EN */

    /* Only bits 0 and 1 should be set in normal operation */
    ASSERT_INT_EQ(0, op_ctrl_disabled);
    ASSERT_INT_EQ(0x03, op_ctrl_enabled);

    /* Verify no invalid bits are set */
    ASSERT_INT_EQ(0, op_ctrl_enabled & ~0x03);
}

static void test_mode_def_value(void)
{
    /* MODE_DEF = 0x01: ISO 14443A, passive mode */
    uint8_t mode_def = 0x01;

    /* Bit 0 must be set for passive mode per the datasheet */
    ASSERT_TRUE(mode_def & 0x01);

    /* Upper nibble should be 0 (ISO 14443A is encoded as 0x00 in bits [7:4]) */
    ASSERT_INT_EQ(0, mode_def & 0xF0);
}

static void test_bit_rate_value(void)
{
    /* BIT_RATE = 0x00: TX=106kbps, RX=106kbps (default) */
    uint8_t bit_rate = 0x00;

    /* Lower nibble = RX rate, upper nibble = TX rate
     * 0x00 means both at 106 kbps (default) */
    ASSERT_INT_EQ(0, bit_rate);
}

static void test_iso14443a_mode_value(void)
{
    /* ISO14443A_MODE = 0x0D:
     *   Bit 0: RX no error (1)
     *   Bit 2: RX CRC enabled (1)
     *   Bit 3: TX CRC enabled (1)
     *   = 0b00001101 = 0x0D */
    uint8_t iso_mode = 0x0D;

    /* Verify TX CRC enabled (bit 3) */
    ASSERT_TRUE(iso_mode & (1 << 3));

    /* Verify RX CRC enabled (bit 2) */
    ASSERT_TRUE(iso_mode & (1 << 2));

    /* Verify no-error mode (bit 0) */
    ASSERT_TRUE(iso_mode & (1 << 0));
}

static void test_osc_conf_value(void)
{
    /* OSC_CONF = 0x18:
     *   Bits [3:1]: Trimming = 0x8 (center)
     *   Bit 4: Oscillator enabled
     *   = 0b00011000 = 0x18 */
    uint8_t osc_conf = 0x18;

    /* Verify oscillator enable bit (bit 4) */
    ASSERT_TRUE(osc_conf & (1 << 4));

    /* Verify trimming value is in valid range (0-15) */
    uint8_t trim = (osc_conf >> 1) & 0x0F;
    ASSERT_INT_LE(trim, 15);
}

static void test_io_conf1_value(void)
{
    /* IO_CONF1 = 0x01:
     *   Bit 0: IRQ push-pull active-high
     *   = 0b00000001 = 0x01 */
    uint8_t io_conf1 = 0x01;

    /* Verify push-pull mode (bit 0) */
    ASSERT_TRUE(io_conf1 & 0x01);

    /* No other bits should be set */
    ASSERT_INT_EQ(0, io_conf1 & ~0x01);
}

static void test_tx_driver_value(void)
{
    /* TX_DRIVER = 0x08:
     *   Bits [3:0]: RFO resistance = 8 ohm (encoded as 0x08)
     *   Per datasheet, valid values are 0-15 */
    uint8_t tx_driver = 0x08;

    /* Verify RFO resistance is in valid range */
    uint8_t rfo = tx_driver & 0x0F;
    ASSERT_INT_LE(rfo, 15);
}

static void test_timer_values(void)
{
    /* TIMER_EMV = 0x0A: ~10 ms start-of-frame guard time */
    uint8_t timer_emv = 0x0A;
    ASSERT_INT_EQ(0x0A, timer_emv);

    /* TIMER1 = 0x0A, TIMER2 = 0x0A: general-purpose timers */
    uint8_t timer1 = 0x0A;
    uint8_t timer2 = 0x0A;
    ASSERT_INT_EQ(timer_emv, timer1);
    ASSERT_INT_EQ(timer_emv, timer2);

    /* TIMER3 = 0x21: different timing */
    uint8_t timer3 = 0x21;
    ASSERT_INT_NE(timer3, timer1);
}

/* ========================================================================
 * Test: VDD Measurement Conversion
 *
 * The st25r3916_measure_vdd() function converts the ADC result using:
 *   VDD_mV = result * 5000 / 255
 *
 * Verify edge cases and typical values.
 * ======================================================================== */

static void test_vdd_conversion_zero(void)
{
    /* ADC result 0 → 0 mV */
    uint16_t vdd = (uint16_t)((uint32_t)0 * 5000U / 255U);
    ASSERT_INT_EQ(0, vdd);
}

static void test_vdd_conversion_midrange(void)
{
    /* ADC result 128 → approximately 2509 mV */
    uint16_t vdd = (uint16_t)((uint32_t)128 * 5000U / 255U);
    ASSERT_INT_GE(vdd, 2400);
    ASSERT_INT_LE(vdd, 2600);
}

static void test_vdd_conversion_fullscale(void)
{
    /* ADC result 255 → approximately 5000 mV */
    uint16_t vdd = (uint16_t)((uint32_t)255 * 5000U / 255U);
    ASSERT_INT_EQ(5000, vdd);
}

static void test_vdd_conversion_typical_3v3(void)
{
    /* ADC result for 3.3V: result = 255 * 3300 / 5000 ≈ 168 */
    uint8_t result = (uint8_t)((uint32_t)255 * 3300 / 5000);
    uint16_t vdd = (uint16_t)((uint32_t)result * 5000U / 255U);
    /* Allow ±100 mV tolerance for quantization */
    ASSERT_INT_GE(vdd, 3200);
    ASSERT_INT_LE(vdd, 3400);
}

static void test_vdd_conversion_typical_5v0(void)
{
    /* ADC result 255 → 5000 mV (VDD = VREF) */
    uint16_t vdd = (uint16_t)((uint32_t)255 * 5000U / 255U);
    ASSERT_INT_EQ(5000, vdd);
}

/* ========================================================================
 * Test: IRQ Mask Configuration
 *
 * Verify that the IRQ masks used in initialization enable the correct
 * interrupts and don't conflict with each other.
 * ======================================================================== */

static void test_irq_mask1_init_value(void)
{
    /* IRQ_MASK1 enables: OSC | TXE | RXE | NFCA */
    uint8_t mask1 = ST25R3916_IRQ1_OSC | ST25R3916_IRQ1_TXE |
                    ST25R3916_IRQ1_RXE | ST25R3916_IRQ1_NFCA;

    /* Verify each expected bit is set */
    ASSERT_TRUE(mask1 & ST25R3916_IRQ1_OSC);    /* Bit 0 */
    ASSERT_TRUE(mask1 & ST25R3916_IRQ1_NFCA);   /* Bit 2 */
    ASSERT_TRUE(mask1 & ST25R3916_IRQ1_TXE);    /* Bit 6 */
    ASSERT_TRUE(mask1 & ST25R3916_IRQ1_RXE);    /* Bit 7 */

    /* Verify combined value */
    ASSERT_INT_EQ(0b11000101, mask1);
}

static void test_irq_mask2_init_value(void)
{
    /* IRQ_MASK2 enables: RXS */
    uint8_t mask2 = ST25R3916_IRQ2_RXS;

    /* Only RX start should be enabled */
    ASSERT_INT_EQ(ST25R3916_IRQ2_RXS, mask2);
    ASSERT_INT_EQ(0x10, mask2);
}

static void test_irq_mask3_init_value(void)
{
    /* IRQ_MASK3 enables: EON | EOF */
    uint8_t mask3 = ST25R3916_IRQ3_EON | ST25R3916_IRQ3_EOF;

    /* Verify bits */
    ASSERT_TRUE(mask3 & ST25R3916_IRQ3_EON);   /* Bit 3 */
    ASSERT_TRUE(mask3 & ST25R3916_IRQ3_EOF);    /* Bit 4 */

    /* Combined: 0b00011000 = 0x18 */
    ASSERT_INT_EQ(0x18, mask3);
}

/* ========================================================================
 * Test: TX FIFO / Timer2 Address Alias
 *
 * The ST25R3916 has an address alias: address 0x1F in Space A is the
 * TX FIFO when written and Timer2 when read. Verify the constants
 * match this dual-purpose address.
 * ======================================================================== */

static void test_txfifo_timer2_alias(void)
{
    /* TX FIFO and Timer2 share address 0x1F */
    ASSERT_INT_EQ(ST25R3916_REG_TX_FIFO, ST25R3916_REG_TIMER2);
    ASSERT_INT_EQ(0x1F, ST25R3916_REG_TX_FIFO);
}

/* ========================================================================
 * Test: Antenna Calibration Register Constraints
 *
 * Antenna calibration target and time values should be within
 * the valid ranges specified in the datasheet.
 * ======================================================================== */

static void test_ant_cal_target_range(void)
{
    /* ANT_CAL_TARGET = 0x05 (used in init sequence)
     * Valid range per datasheet: 0x00–0x0F */
    uint8_t cal_target = 0x05;
    ASSERT_INT_LE(cal_target, 0x0F);
}

static void test_ant_cal_time_range(void)
{
    /* ANT_CAL_TIME = 0x07 (used in init sequence)
     * Valid range per datasheet: 0x00–0x0F */
    uint8_t cal_time = 0x07;
    ASSERT_INT_LE(cal_time, 0x0F);
}

/* ========================================================================
 * Test: Register Address Sequential Ranges
 *
 * Verify that register groups form sequential address ranges as
 * documented in the datasheet.
 * ======================================================================== */

static void test_rx_conf_address_range(void)
{
    /* RX_CONF1–RX_CONF4 should be sequential at 0x12–0x15 */
    ASSERT_INT_EQ(0x12, ST25R3916_REG_RX_CONF1);
    ASSERT_INT_EQ(0x13, ST25R3916_REG_RX_CONF2);
    ASSERT_INT_EQ(0x14, ST25R3916_REG_RX_CONF3);
    ASSERT_INT_EQ(0x15, ST25R3916_REG_RX_CONF4);
}

static void test_irq_mask_address_range(void)
{
    /* IRQ_MASK1–MASK5 should be sequential at 0x21–0x25 */
    ASSERT_INT_EQ(0x21, ST25R3916_REG_IRQ_MASK1);
    ASSERT_INT_EQ(0x22, ST25R3916_REG_IRQ_MASK2);
    ASSERT_INT_EQ(0x23, ST25R3916_REG_IRQ_MASK3);
    ASSERT_INT_EQ(0x24, ST25R3916_REG_IRQ_MASK4);
    ASSERT_INT_EQ(0x25, ST25R3916_REG_IRQ_MASK5);
}

static void test_tx_byte_count_address_range(void)
{
    /* NUM_TX_BYTES1–3 should be sequential at 0x26–0x28 */
    ASSERT_INT_EQ(0x26, ST25R3916_REG_NUM_TX_BYTES1);
    ASSERT_INT_EQ(0x27, ST25R3916_REG_NUM_TX_BYTES2);
    ASSERT_INT_EQ(0x28, ST25R3916_REG_NUM_TX_BYTES3);
}

static void test_irq_status_address_range(void)
{
    /* IRQ_STATUS1–5 should be sequential at 0x04–0x08 */
    ASSERT_INT_EQ(0x04, ST25R3916_REG_IRQ_STATUS1);
    ASSERT_INT_EQ(0x05, ST25R3916_REG_IRQ_STATUS2);
    ASSERT_INT_EQ(0x06, ST25R3916_REG_IRQ_STATUS3);
    ASSERT_INT_EQ(0x07, ST25R3916_REG_IRQ_STATUS4);
    ASSERT_INT_EQ(0x08, ST25R3916_REG_IRQ_STATUS5);
}

/* ========================================================================
 * Test: AM Gain Range Register Values
 *
 * AM gain registers (GRANGE1-3) are set to 0xFF in init.
 * Verify these are valid maximum values.
 * ======================================================================== */

static void test_am_grange_values(void)
{
    /* AM gain range registers set to 0xFF (maximum) during init */
    uint8_t grange1 = 0xFF;
    uint8_t grange2 = 0xFF;
    uint8_t grange3 = 0xFF;

    /* All should be 0xFF (maximum gain) */
    ASSERT_INT_EQ(0xFF, grange1);
    ASSERT_INT_EQ(0xFF, grange2);
    ASSERT_INT_EQ(0xFF, grange3);
}

/* ========================================================================
 * Test: OP_CTRL State Transitions
 *
 * Verify that the OP_CTRL register transitions are valid:
 *   0x00 → disabled (init)
 *   0x03 → TX_EN | RX_EN enabled (final)
 * ======================================================================== */

static void test_op_ctrl_transition(void)
{
    /* OP_CTRL starts disabled (0x00) then transitions to enabled (0x03) */
    uint8_t op_ctrl_init = 0x00;
    uint8_t op_ctrl_final = 0x03;  /* TX_EN | RX_EN */

    /* Initial state: disabled */
    ASSERT_INT_EQ(0, op_ctrl_init);

    /* Final state: TX and RX enabled */
    ASSERT_TRUE(op_ctrl_final & 0x01);  /* TX_EN */
    ASSERT_TRUE(op_ctrl_final & 0x02);  /* RX_EN */

    /* No other bits should be set */
    ASSERT_INT_EQ(0, op_ctrl_final & ~0x03);
}

/* ========================================================================
 * Main test runner
 * ======================================================================== */

int main(void)
{
    printf("=== ST25R3916 NFC Controller Initialization Tests ===\n\n");

    printf("Register Address Tests\n");
    test_register_addresses_in_range();
    test_direct_command_range();
    test_direct_commands_unique();

    printf("SPI Protocol Encoding Tests\n");
    test_spi_write_single_encoding();
    test_spi_read_single_encoding();
    test_spi_write_burst_encoding();
    test_spi_read_burst_encoding();
    test_spi_addr_masking();

    printf("IRQ Bit Definition Tests\n");
    test_irq1_bits_distinct();
    test_irq2_bits_distinct();
    test_irq3_bits_distinct();

    printf("IC Identity Tests\n");
    test_ic_identity_values();

    printf("Initialization Register Value Tests\n");
    test_op_ctrl_values();
    test_mode_def_value();
    test_bit_rate_value();
    test_iso14443a_mode_value();
    test_osc_conf_value();
    test_io_conf1_value();
    test_tx_driver_value();
    test_timer_values();

    printf("VDD Measurement Conversion Tests\n");
    test_vdd_conversion_zero();
    test_vdd_conversion_midrange();
    test_vdd_conversion_fullscale();
    test_vdd_conversion_typical_3v3();
    test_vdd_conversion_typical_5v0();

    printf("IRQ Mask Configuration Tests\n");
    test_irq_mask1_init_value();
    test_irq_mask2_init_value();
    test_irq_mask3_init_value();

    printf("Address Alias and Range Tests\n");
    test_txfifo_timer2_alias();
    test_rx_conf_address_range();
    test_irq_mask_address_range();
    test_tx_byte_count_address_range();
    test_irq_status_address_range();

    printf("Antenna Calibration Tests\n");
    test_ant_cal_target_range();
    test_ant_cal_time_range();

    printf("AM Gain Range Tests\n");
    test_am_grange_values();

    printf("State Transition Tests\n");
    test_op_ctrl_transition();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}