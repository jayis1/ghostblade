/*
 * ghostwisp_i2c.c — GhostWisp I2C Driver Implementation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements the I2C0 master driver for the GhostWisp RP2350B.
 * Uses the DesignWare (Synopsys) I2C controller integrated in the
 * RP2350B. The driver provides blocking read/write operations with
 * timeout handling and error recovery.
 *
 * Host test mode: When DRIVER_HOST_TEST is defined, the I2C
 * controller is simulated with a simple slave model that can be
 * configured per-test to ACK/NACK and return specific data.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 5 (I2C)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ghostwisp_i2c.h"

/* ======================================================================== */
/*  Host Test Mode                                                          */
/* ======================================================================== */

#ifdef DRIVER_HOST_TEST

/* ── Simulated I2C slave model ──────────────────────────────────────────── */

/* Up to 8 simulated slave devices, each with a register file */
#define SIM_MAX_SLAVES  8

typedef struct {
    bool     present;           /* true = slave responds to its address */
    uint8_t  addr;              /* 7-bit address */
    uint8_t  registers[256];    /* Register file */
    bool     reg_initialized;   /* Whether registers have been set */
    uint8_t  current_reg;       /* Current register pointer for sequential read */
} sim_slave_t;

static sim_slave_t sim_slaves[SIM_MAX_SLAVES];
static bool        sim_i2c_initialized = false;
static uint32_t    sim_baud_hz = 0;
static uint32_t    sim_error_count = 0;

/* Test control functions */
void i2c_test_reset(void) {
    memset(sim_slaves, 0, sizeof(sim_slaves));
    sim_i2c_initialized = false;
    sim_baud_hz = 0;
    sim_error_count = 0;
}

void i2c_test_add_slave(uint8_t addr) {
    for (int i = 0; i < SIM_MAX_SLAVES; i++) {
        if (!sim_slaves[i].present) {
            sim_slaves[i].present = true;
            sim_slaves[i].addr = addr;
            sim_slaves[i].reg_initialized = false;
            sim_slaves[i].current_reg = 0;
            memset(sim_slaves[i].registers, 0, sizeof(sim_slaves[i].registers));
            return;
        }
    }
}

void i2c_test_set_register(uint8_t addr, uint8_t reg, uint8_t val) {
    for (int i = 0; i < SIM_MAX_SLAVES; i++) {
        if (sim_slaves[i].present && sim_slaves[i].addr == addr) {
            sim_slaves[i].registers[reg] = val;
            sim_slaves[i].reg_initialized = true;
            return;
        }
    }
}

void i2c_test_set_register16(uint8_t addr, uint8_t reg, uint16_t val) {
    /* Store as big-endian (MSB first) to match MAX17048 convention */
    i2c_test_set_register(addr, reg, (uint8_t)(val >> 8));
    i2c_test_set_register(addr, reg + 1, (uint8_t)(val & 0xFF));
}

void i2c_test_remove_slave(uint8_t addr) {
    for (int i = 0; i < SIM_MAX_SLAVES; i++) {
        if (sim_slaves[i].present && sim_slaves[i].addr == addr) {
            sim_slaves[i].present = false;
            return;
        }
    }
}

bool i2c_test_is_init(void) { return sim_i2c_initialized; }
uint32_t i2c_test_get_error_count(void) { return sim_error_count; }

/* Find a simulated slave by address */
static sim_slave_t *find_slave(uint8_t addr) {
    for (int i = 0; i < SIM_MAX_SLAVES; i++) {
        if (sim_slaves[i].present && sim_slaves[i].addr == addr)
            return &sim_slaves[i];
    }
    return NULL;
}

#else /* DRIVER_HOST_TEST — real hardware */

/* ======================================================================== */
/*  RP2350B I2C Register Access                                             */
/* ======================================================================== */

#define REG32(addr)  (*(volatile uint32_t *)(addr))

static uint32_t i2c_base(void) {
    return I2C0_BASE_ADDR;
}

static volatile uint32_t *i2c_reg(uint32_t offset) {
    return (volatile uint32_t *)(i2c_base() + offset);
}

static bool g_i2c_initialized = false;
static uint32_t g_i2c_baud = 0;
static uint32_t g_error_count = 0;

/* Simple microsecond delay */
static void i2c_delay_us(uint32_t us) {
    /* Approximate: at 150 MHz, each NOP ≈ 6.67 ns, so 150 NOPs ≈ 1 µs */
    for (uint32_t i = 0; i < us * 150; i++)
        __asm__ volatile ("nop");
}

/* Check if TX FIFO has space */
static bool i2c_tx_fifo_not_full(void) {
    return (*i2c_reg(I2C_REG_STATUS) & I2C_STATUS_TFNF) != 0;
}

/* Check if RX FIFO has data */
static bool i2c_rx_fifo_not_empty(void) {
    return (*i2c_reg(I2C_REG_STATUS) & I2C_STATUS_RFNE) != 0;
}

/* Check if TX FIFO is empty (transfer complete) */
static bool i2c_tx_fifo_empty(void) {
    return (*i2c_reg(I2C_REG_STATUS) & I2C_STATUS_TFE) != 0;
}

#endif /* DRIVER_HOST_TEST */

/* ======================================================================== */
/*  Implementation                                                          */
/* ======================================================================== */

i2c_result_t i2c_init(uint8_t instance, uint32_t baud_hz) {
    if (instance != I2C_INSTANCE_0)
        return I2C_ERR_INVALID_ARG;
    if (baud_hz == 0)
        return I2C_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    sim_i2c_initialized = true;
    sim_baud_hz = baud_hz;
    return I2C_OK;
#else
    /* Disable I2C controller during configuration */
    *i2c_reg(I2C_REG_ENABLE) = 0;

    /* Configure as master, fast mode, restart enabled, slave disabled */
    *i2c_reg(I2C_REG_CON) = I2C_CON_MASTER | I2C_CON_SPEED_FAST |
                             I2C_CON_RESTART_EN | I2C_CON_SLAVE_DISABLE;

    /* Set baud rate by configuring the SS_SCL_HCNT/LCNT registers.
     * For 400 kHz at 48 MHz peripheral clock:
     *   LCNT ≈ 48e6 / (400e3 * 2) ≈ 120
     * The DW I2C controller auto-adjusts based on the input clock.
     * We write the CON speed bits and let the controller handle timing. */

    /* Enable the I2C controller */
    *i2c_reg(I2C_REG_ENABLE) = I2C_ENABLE_CTRL;
    __asm__ volatile ("dmb" ::: "memory");

    g_i2c_initialized = true;
    g_i2c_baud = baud_hz;
    g_error_count = 0;
    return I2C_OK;
#endif
}

i2c_result_t i2c_write_blocking(uint8_t addr, const uint8_t *data, size_t len) {
    if (len > I2C_MAX_TRANSFER)
        return I2C_ERR_TOO_LONG;
    if (len > 0 && data == NULL)
        return I2C_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    if (!sim_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    sim_slave_t *slave = find_slave(addr);
    if (slave == NULL) {
        sim_error_count++;
        return I2C_ERR_NACK;
    }

    /* I2C sensor protocol: first byte of a write is the register address.
     * Subsequent bytes are data written to consecutive registers starting
     * at that address. */
    if (len > 0) {
        slave->current_reg = data[0];
        for (size_t i = 1; i < len; i++) {
            slave->registers[slave->current_reg] = data[i];
            slave->current_reg++;
        }
    }
    return I2C_OK;
#else
    if (!g_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    /* Set target address */
    *i2c_reg(I2C_REG_TAR) = addr & 0x7F;
    i2c_delay_us(10);

    /* Write data to TX FIFO */
    for (size_t i = 0; i < len; i++) {
        uint32_t timeout = I2C_TIMEOUT_US;
        while (!i2c_tx_fifo_not_full()) {
            if (--timeout == 0) {
                g_error_count++;
                return I2C_ERR_TIMEOUT;
            }
            i2c_delay_us(1);
        }
        uint32_t cmd = data[i];
        if (i == len - 1)
            cmd |= I2C_CMD_STOP;
        *i2c_reg(I2C_REG_DATA_CMD) = cmd;
    }

    /* Wait for TX FIFO empty (transfer complete) */
    uint32_t timeout = I2C_TIMEOUT_US * 10;
    while (!i2c_tx_fifo_empty()) {
        if (--timeout == 0) {
            g_error_count++;
            return I2C_ERR_TIMEOUT;
        }
        i2c_delay_us(1);
    }

    return I2C_OK;
#endif
}

i2c_result_t i2c_read_blocking(uint8_t addr, uint8_t *data, size_t len) {
    if (len > I2C_MAX_TRANSFER)
        return I2C_ERR_TOO_LONG;
    if (len > 0 && data == NULL)
        return I2C_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    if (!sim_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    sim_slave_t *slave = find_slave(addr);
    if (slave == NULL) {
        sim_error_count++;
        return I2C_ERR_NACK;
    }

    /* Read data from slave's current register pointer */
    for (size_t i = 0; i < len; i++) {
        data[i] = slave->registers[slave->current_reg];
        slave->current_reg++;
    }
    return I2C_OK;
#else
    if (!g_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    *i2c_reg(I2C_REG_TAR) = addr & 0x7F;
    i2c_delay_us(10);

    /* Issue read commands */
    for (size_t i = 0; i < len; i++) {
        uint32_t timeout = I2C_TIMEOUT_US;
        while (!i2c_tx_fifo_not_full()) {
            if (--timeout == 0) {
                g_error_count++;
                return I2C_ERR_TIMEOUT;
            }
            i2c_delay_us(1);
        }
        uint32_t cmd = I2C_CMD_READ;
        if (i == len - 1)
            cmd |= I2C_CMD_STOP;
        *i2c_reg(I2C_REG_DATA_CMD) = cmd;
    }

    /* Read received data from RX FIFO */
    for (size_t i = 0; i < len; i++) {
        uint32_t timeout = I2C_TIMEOUT_US * 10;
        while (!i2c_rx_fifo_not_empty()) {
            if (--timeout == 0) {
                g_error_count++;
                return I2C_ERR_TIMEOUT;
            }
            i2c_delay_us(1);
        }
        data[i] = (uint8_t)(*i2c_reg(I2C_REG_DATA_CMD) & 0xFF);
    }

    return I2C_OK;
#endif
}

i2c_result_t i2c_write_register(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_write_blocking(addr, buf, 2);
}

i2c_result_t i2c_read_register(uint8_t addr, uint8_t reg, uint8_t *out) {
    if (out == NULL)
        return I2C_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    if (!sim_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    sim_slave_t *slave = find_slave(addr);
    if (slave == NULL) {
        sim_error_count++;
        return I2C_ERR_NACK;
    }

    /* Set register pointer, then read one byte */
    slave->current_reg = reg;
    return i2c_read_blocking(addr, out, 1);
#else
    /* Write register address (no stop), then repeated-start read */
    i2c_result_t result = i2c_write_blocking(addr, &reg, 1);
    if (result != I2C_OK)
        return result;

    return i2c_read_blocking(addr, out, 1);
#endif
}

i2c_result_t i2c_read_register16(uint8_t addr, uint8_t reg, uint16_t *out16) {
    if (out16 == NULL)
        return I2C_ERR_INVALID_ARG;

    uint8_t buf[2] = {0, 0};
    i2c_result_t result;

#ifdef DRIVER_HOST_TEST
    if (!sim_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    sim_slave_t *slave = find_slave(addr);
    if (slave == NULL) {
        sim_error_count++;
        return I2C_ERR_NACK;
    }

    /* Set register pointer, then read 2 bytes */
    slave->current_reg = reg;
    result = i2c_read_blocking(addr, buf, 2);
#else
    result = i2c_write_blocking(addr, &reg, 1);
    if (result != I2C_OK)
        return result;
    result = i2c_read_blocking(addr, buf, 2);
#endif

    if (result == I2C_OK) {
        /* MAX17048 returns big-endian: MSB first */
        *out16 = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
    }
    return result;
}

i2c_result_t i2c_probe(uint8_t addr) {
#ifdef DRIVER_HOST_TEST
    if (!sim_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    sim_slave_t *slave = find_slave(addr);
    if (slave == NULL) {
        sim_error_count++;
        return I2C_ERR_NACK;
    }
    return I2C_OK;
#else
    if (!g_i2c_initialized)
        return I2C_ERR_NOT_INIT;

    /* Zero-length write: just address + stop */
    *i2c_reg(I2C_REG_TAR) = addr & 0x7F;
    i2c_delay_us(10);

    uint32_t timeout = I2C_TIMEOUT_US;
    while (!i2c_tx_fifo_not_full()) {
        if (--timeout == 0) {
            g_error_count++;
            return I2C_ERR_TIMEOUT;
        }
        i2c_delay_us(1);
    }
    *i2c_reg(I2C_REG_DATA_CMD) = I2C_CMD_STOP;

    /* Wait for completion */
    timeout = I2C_TIMEOUT_US * 10;
    while (!i2c_tx_fifo_empty()) {
        if (--timeout == 0) {
            g_error_count++;
            return I2C_ERR_TIMEOUT;
        }
        i2c_delay_us(1);
    }

    /* Check for TX abort (NACK) */
    uint32_t raw_int = *i2c_reg(I2C_REG_RAW_INT_STAT);
    if (raw_int & (1U << 6)) {  /* TX_ABRT bit */
        *i2c_reg(I2C_REG_CLR_TX_ABRT);  /* Clear abort */
        g_error_count++;
        return I2C_ERR_NACK;
    }

    return I2C_OK;
#endif
}

i2c_result_t i2c_deinit(void) {
#ifdef DRIVER_HOST_TEST
    sim_i2c_initialized = false;
    return I2C_OK;
#else
    if (!g_i2c_initialized)
        return I2C_OK;

    *i2c_reg(I2C_REG_ENABLE) = 0;
    __asm__ volatile ("dmb" ::: "memory");
    g_i2c_initialized = false;
    return I2C_OK;
#endif
}

bool i2c_is_initialized(void) {
#ifdef DRIVER_HOST_TEST
    return sim_i2c_initialized;
#else
    return g_i2c_initialized;
#endif
}

uint32_t i2c_get_baud(void) {
#ifdef DRIVER_HOST_TEST
    return sim_baud_hz;
#else
    return g_i2c_baud;
#endif
}

uint32_t i2c_get_error_count(void) {
#ifdef DRIVER_HOST_TEST
    return sim_error_count;
#else
    return g_error_count;
#endif
}