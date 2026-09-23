/*
 * ghostwisp_spi.c — GhostWisp SPI Master Driver Implementation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements the SPI master driver for the GhostWisp RP2350B.
 * Manages three SPI buses:
 *   SPI0 — CC1101 sub-GHz radio (pins 6–9)
 *   SPI1 — ST25R3916 NFC controller (pins 12–15)
 *   SPI2 — microSD card (pins 17–20)
 *
 * Uses the PL022 SSP peripheral. Chip-select is managed in
 * software via SIO GPIO for precise control.
 *
 * Host test mode: When DRIVER_HOST_TEST is defined, the SPI
 * peripherals are simulated with per-bus state and a simple
 * loopback/register model for testing.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 3 (SPI)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ghostwisp_spi.h"
#include "ghostwisp_gpio.h"

/* ======================================================================== */
/*  Per-instance state                                                      */
/* ======================================================================== */

typedef struct {
    bool           initialized;
    uint32_t       base_addr;
    uint8_t        cs_pin;
    uint32_t       baud_hz;
    spi_mode_t     mode;
    spi_bit_order_t bit_order;
    uint32_t       error_count;
} spi_instance_state_t;

#ifndef DRIVER_HOST_TEST
static spi_instance_state_t g_spi[SPI_INSTANCE_COUNT] = {
    { false, SPI0_BASE_ADDR, SPI0_CS_PIN, 0, SPI_MODE0, SPI_MSB_FIRST, 0 },
    { false, SPI1_BASE_ADDR, SPI1_CS_PIN, 0, SPI_MODE0, SPI_MSB_FIRST, 0 },
    { false, SPI2_BASE_ADDR, SPI2_CS_PIN, 0, SPI_MODE0, SPI_MSB_FIRST, 0 },
};
#else
static spi_instance_state_t g_spi[SPI_INSTANCE_COUNT];
#endif

/* ======================================================================== */
/*  Host Test Mode                                                          */
/* ======================================================================== */

#ifdef DRIVER_HOST_TEST

/* Simulated SPI state */
typedef struct {
    bool     cs_asserted;
    uint8_t  tx_buf[SPI_MAX_TRANSFER];
    size_t   tx_len;
    uint8_t  rx_buf[SPI_MAX_TRANSFER];
    size_t   rx_len;
    /* Loopback mode: rx_buf is filled from tx_buf */
    bool     loopback;
    /* Register model: simulate a simple SPI device with registers */
    bool     has_registers;
    uint8_t  registers[256];
    uint8_t  last_reg_addr;
} sim_spi_t;

static sim_spi_t sim_spi_bus[SPI_INSTANCE_COUNT];

void spi_test_reset(void) {
    memset(g_spi, 0, sizeof(g_spi));
    memset(sim_spi_bus, 0, sizeof(sim_spi_bus));
    g_spi[0].base_addr = SPI0_BASE_ADDR;
    g_spi[0].cs_pin = SPI0_CS_PIN;
    g_spi[1].base_addr = SPI1_BASE_ADDR;
    g_spi[1].cs_pin = SPI1_CS_PIN;
    g_spi[2].base_addr = SPI2_BASE_ADDR;
    g_spi[2].cs_pin = SPI2_CS_PIN;
}

void spi_test_set_loopback(uint8_t instance, bool enable) {
    if (instance < SPI_INSTANCE_COUNT)
        sim_spi_bus[instance].loopback = enable;
}

void spi_test_set_register(uint8_t instance, uint8_t reg, uint8_t val) {
    if (instance < SPI_INSTANCE_COUNT) {
        sim_spi_bus[instance].has_registers = true;
        sim_spi_bus[instance].registers[reg] = val;
    }
}

uint8_t spi_test_get_register(uint8_t instance, uint8_t reg) {
    if (instance < SPI_INSTANCE_COUNT)
        return sim_spi_bus[instance].registers[reg];
    return 0;
}

bool spi_test_cs_asserted(uint8_t instance) {
    if (instance < SPI_INSTANCE_COUNT)
        return sim_spi_bus[instance].cs_asserted;
    return false;
}

const uint8_t *spi_test_get_tx_buf(uint8_t instance, size_t *len) {
    if (instance < SPI_INSTANCE_COUNT) {
        if (len) *len = sim_spi_bus[instance].tx_len;
        return sim_spi_bus[instance].tx_buf;
    }
    if (len) *len = 0;
    return NULL;
}

void spi_test_set_rx_data(uint8_t instance, const uint8_t *data, size_t len) {
    if (instance < SPI_INSTANCE_COUNT && len <= SPI_MAX_TRANSFER) {
        memcpy(sim_spi_bus[instance].rx_buf, data, len);
        sim_spi_bus[instance].rx_len = len;
    }
}

#endif /* DRIVER_HOST_TEST */

/* ======================================================================== */
/*  Internal helpers                                                        */
/* ======================================================================== */

#ifndef DRIVER_HOST_TEST

#define REG32(addr)  (*(volatile uint32_t *)(addr))

static volatile uint32_t *spi_reg(uint8_t instance, uint32_t offset) {
    return (volatile uint32_t *)(g_spi[instance].base_addr + offset);
}

static void spi_delay_us(uint32_t us) {
    for (uint32_t i = 0; i < us * 150; i++)
        __asm__ volatile ("nop");
}

/* Wait for TX FIFO to have space */
static bool spi_tx_not_full(uint8_t instance) {
    return (*spi_reg(instance, SPI_REG_SR) & SPI_SR_TNF) != 0;
}

/* Wait for RX FIFO to have data */
static bool spi_rx_not_empty(uint8_t instance) {
    return (*spi_reg(instance, SPI_REG_SR) & SPI_SR_RNE) != 0;
}

/* Wait for SPI to be idle (not busy, TX FIFO empty) */
static bool spi_idle(uint8_t instance) {
    uint32_t sr = *spi_reg(instance, SPI_REG_SR);
    return (sr & (SPI_SR_BSY | SPI_SR_TFE)) == SPI_SR_TFE;
}

/* Compute prescaler and postscaler for desired baud rate
 * RP2350B SPI: CPSR (2–254, even) and SCR (0–255)
 * baud = peri_clk / (CPSR * (1 + SCR))
 */
static void spi_compute_baud(uint32_t baud_hz, uint8_t *cpsr, uint8_t *scr) {
    /* Peripheral clock = 48 MHz */
    uint32_t peri_clk = 48000000UL;
    uint32_t div = peri_clk / baud_hz;
    if (div < 2) div = 2;

    /* Find CPSR (even, 2–254) and SCR (0–255) such that CPSR * (1+SCR) ≈ div */
    *cpsr = 2;
    *scr = 0;
    uint32_t best_err = div;

    for (uint32_t c = 2; c <= 254; c += 2) {
        uint32_t s = (div / c) - 1;
        if (s > 255) continue;
        if (s == 0 && div < c) continue;
        uint32_t actual = c * (s + 1);
        uint32_t err = (actual > div) ? (actual - div) : (div - actual);
        if (err < best_err) {
            best_err = err;
            *cpsr = (uint8_t)c;
            *scr = (uint8_t)s;
        }
    }
}

#endif /* DRIVER_HOST_TEST */

/* ======================================================================== */
/*  Implementation                                                          */
/* ======================================================================== */

uint32_t spi_get_instance_base(uint8_t instance) {
    switch (instance) {
        case 0: return SPI0_BASE_ADDR;
        case 1: return SPI1_BASE_ADDR;
        case 2: return SPI2_BASE_ADDR;
        default: return 0;
    }
}

uint8_t spi_get_instance_cs_pin(uint8_t instance) {
    switch (instance) {
        case 0: return SPI0_CS_PIN;
        case 1: return SPI1_CS_PIN;
        case 2: return SPI2_CS_PIN;
        default: return 0xFF;
    }
}

spi_result_t spi_init(uint8_t instance, spi_mode_t mode, uint32_t baud_hz,
                      spi_bit_order_t bit_order) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;
    if (baud_hz == 0)
        return SPI_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    g_spi[instance].initialized = true;
    g_spi[instance].mode = mode;
    g_spi[instance].baud_hz = baud_hz;
    g_spi[instance].bit_order = bit_order;
    g_spi[instance].error_count = 0;
    sim_spi_bus[instance].cs_asserted = false;
    return SPI_OK;
#else
    /* Disable SPI during config */
    *spi_reg(instance, SPI_REG_CR1) = 0;

    /* Configure CR0: 8-bit data, SPI frame format, CPOL/CPHA from mode */
    uint32_t cr0 = SPI_CR0_DSS_8BIT | SPI_CR0_FRF_SPI;
    if (mode == SPI_MODE2 || mode == SPI_MODE3)
        cr0 |= SPI_CR0_SPO;  /* CPOL=1 */
    if (mode == SPI_MODE1 || mode == SPI_MODE3)
        cr0 |= SPI_CR0_SPH;  /* CPHA=1 */

    /* Bit order: PL022 doesn't have native LSB-first; use SCR bit 9? No.
     * On RP2350B, bit order is controlled via the peripheral.
     * For MSB-first (default), no extra bits needed.
     * For LSB-first, we'd need to reverse in software or use a PIO.
     * We store the setting and reverse in software if needed. */
    *spi_reg(instance, SPI_REG_CR0) = cr0;

    /* Set baud rate via CPSR and SCR */
    uint8_t cpsr, scr;
    spi_compute_baud(baud_hz, &cpsr, &scr);
    *spi_reg(instance, SPI_REG_CPSR) = cpsr;
    /* SCR is in CR0 bits 15:8 */
    *spi_reg(instance, SPI_REG_CR0) = cr0 | ((uint32_t)scr << 8);

    /* Enable SPI as master (no loopback) */
    *spi_reg(instance, SPI_REG_CR1) = SPI_CR1_SSE;

    /* Deselect CS (drive high) */
    gpio_put(g_spi[instance].cs_pin, true);

    g_spi[instance].initialized = true;
    g_spi[instance].mode = mode;
    g_spi[instance].baud_hz = baud_hz;
    g_spi[instance].bit_order = bit_order;
    g_spi[instance].error_count = 0;
    __asm__ volatile ("dmb" ::: "memory");
    return SPI_OK;
#endif
}

spi_result_t spi_cs_select(uint8_t instance) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;

#ifdef DRIVER_HOST_TEST
    sim_spi_bus[instance].cs_asserted = true;
    return SPI_OK;
#else
    gpio_put(g_spi[instance].cs_pin, false);  /* Active-low */
    __asm__ volatile ("dmb" ::: "memory");
    return SPI_OK;
#endif
}

spi_result_t spi_cs_deselect(uint8_t instance) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;

#ifdef DRIVER_HOST_TEST
    sim_spi_bus[instance].cs_asserted = false;
    return SPI_OK;
#else
    gpio_put(g_spi[instance].cs_pin, true);  /* Active-high deselect */
    __asm__ volatile ("dmb" ::: "memory");
    return SPI_OK;
#endif
}

spi_result_t spi_transfer_blocking(uint8_t instance,
                                    const uint8_t *tx_data,
                                    uint8_t *rx_data, size_t len) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;
    if (!g_spi[instance].initialized)
        return SPI_ERR_NOT_INIT;
    if (len > SPI_MAX_TRANSFER)
        return SPI_ERR_TOO_LONG;
    if (len > 0 && tx_data == NULL)
        return SPI_ERR_NULL_BUF;
    if (len > 0 && rx_data == NULL)
        return SPI_ERR_NULL_BUF;

#ifdef DRIVER_HOST_TEST
    /* Record TX data */
    if (len > 0) {
        memcpy(sim_spi_bus[instance].tx_buf, tx_data, len);
        sim_spi_bus[instance].tx_len = len;
    }

    /* If registers are enabled, process register writes/reads */
    if (sim_spi_bus[instance].has_registers && len >= 2) {
        uint8_t addr_byte = tx_data[0];
        bool is_read = (addr_byte & 0x80) != 0;  /* Bit 7 = read */
        uint8_t reg = addr_byte & 0x7F;

        sim_spi_bus[instance].last_reg_addr = reg;

        if (!is_read && len >= 2) {
            /* Register write: tx_data[1] is the value */
            sim_spi_bus[instance].registers[reg] = tx_data[1];
        }
        if (is_read) {
            /* Register read: return register value in rx_data[1] */
            rx_data[0] = 0xFF;  /* First byte is discard */
            rx_data[1] = sim_spi_bus[instance].registers[reg];
        } else {
            rx_data[0] = 0xFF;
            if (len >= 2) rx_data[1] = 0xFF;
        }
    } else if (sim_spi_bus[instance].loopback) {
        /* Loopback: rx = tx */
        memcpy(rx_data, tx_data, len);
    } else {
        /* Return pre-set rx_buf data */
        size_t copy_len = len;
        if (copy_len > sim_spi_bus[instance].rx_len)
            copy_len = sim_spi_bus[instance].rx_len;
        memcpy(rx_data, sim_spi_bus[instance].rx_buf, copy_len);
        /* Fill remaining with 0xFF */
        for (size_t i = copy_len; i < len; i++)
            rx_data[i] = 0xFF;
    }
    return SPI_OK;
#else
    /* Assert CS */
    spi_cs_select(instance);

    for (size_t i = 0; i < len; i++) {
        uint8_t tx_byte = tx_data[i];

        /* Software LSB-first reversal if needed */
        if (g_spi[instance].bit_order == SPI_LSB_FIRST) {
            uint8_t rev = 0;
            for (int b = 0; b < 8; b++)
                if (tx_byte & (1 << b))
                    rev |= (1 << (7 - b));
            tx_byte = rev;
        }

        /* Wait for TX FIFO not full */
        uint32_t timeout = SPI_TIMEOUT_US;
        while (!spi_tx_not_full(instance)) {
            if (--timeout == 0) {
                spi_cs_deselect(instance);
                g_spi[instance].error_count++;
                return SPI_ERR_TIMEOUT;
            }
            spi_delay_us(1);
        }
        *spi_reg(instance, SPI_REG_DR) = tx_byte;

        /* Wait for RX FIFO not empty (received byte) */
        timeout = SPI_TIMEOUT_US;
        while (!spi_rx_not_empty(instance)) {
            if (--timeout == 0) {
                spi_cs_deselect(instance);
                g_spi[instance].error_count++;
                return SPI_ERR_TIMEOUT;
            }
            spi_delay_us(1);
        }
        uint8_t rx_byte = (uint8_t)(*spi_reg(instance, SPI_REG_DR) & 0xFF);

        /* Reverse LSB-first */
        if (g_spi[instance].bit_order == SPI_LSB_FIRST) {
            uint8_t rev = 0;
            for (int b = 0; b < 8; b++)
                if (rx_byte & (1 << b))
                    rev |= (1 << (7 - b));
            rx_byte = rev;
        }

        rx_data[i] = rx_byte;
    }

    /* Wait for SPI to be idle before deasserting CS */
    uint32_t timeout = SPI_TIMEOUT_US;
    while (!spi_idle(instance)) {
        if (--timeout == 0) {
            spi_cs_deselect(instance);
            g_spi[instance].error_count++;
            return SPI_ERR_TIMEOUT;
        }
        spi_delay_us(1);
    }

    /* Deselect CS */
    spi_cs_deselect(instance);
    return SPI_OK;
#endif
}

spi_result_t spi_write_blocking(uint8_t instance, const uint8_t *data, size_t len) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;
    if (!g_spi[instance].initialized)
        return SPI_ERR_NOT_INIT;
    if (len > SPI_MAX_TRANSFER)
        return SPI_ERR_TOO_LONG;
    if (len > 0 && data == NULL)
        return SPI_ERR_NULL_BUF;

#ifdef DRIVER_HOST_TEST
    /* Record TX data, discard RX */
    if (len > 0) {
        memcpy(sim_spi_bus[instance].tx_buf, data, len);
        sim_spi_bus[instance].tx_len = len;
    }

    /* If registers enabled, handle register write */
    if (sim_spi_bus[instance].has_registers && len >= 2) {
        uint8_t addr_byte = data[0];
        bool is_read = (addr_byte & 0x80) != 0;
        uint8_t reg = addr_byte & 0x7F;
        if (!is_read) {
            sim_spi_bus[instance].registers[reg] = data[1];
        }
    }
    return SPI_OK;
#else
    /* Use transfer with a dummy RX buffer (reuse stack) */
    uint8_t dummy[SPI_MAX_TRANSFER];
    return spi_transfer_blocking(instance, data, dummy, len);
#endif
}

spi_result_t spi_read_blocking(uint8_t instance, uint8_t *data, size_t len) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;
    if (!g_spi[instance].initialized)
        return SPI_ERR_NOT_INIT;
    if (len > SPI_MAX_TRANSFER)
        return SPI_ERR_TOO_LONG;
    if (len > 0 && data == NULL)
        return SPI_ERR_NULL_BUF;

#ifdef DRIVER_HOST_TEST
    /* Send dummy 0xFF, receive from rx_buf or registers */
    static uint8_t dummy_tx[SPI_MAX_TRANSFER];
    memset(dummy_tx, 0xFF, len);

    if (sim_spi_bus[instance].loopback) {
        memcpy(data, dummy_tx, len);
    } else {
        size_t copy_len = len;
        if (copy_len > sim_spi_bus[instance].rx_len)
            copy_len = sim_spi_bus[instance].rx_len;
        memcpy(data, sim_spi_bus[instance].rx_buf, copy_len);
        for (size_t i = copy_len; i < len; i++)
            data[i] = 0xFF;
    }
    return SPI_OK;
#else
    /* Send dummy 0xFF bytes while reading */
    static uint8_t dummy_tx[SPI_MAX_TRANSFER];
    memset(dummy_tx, 0xFF, len > SPI_MAX_TRANSFER ? SPI_MAX_TRANSFER : len);
    return spi_transfer_blocking(instance, dummy_tx, data, len);
#endif
}

spi_result_t spi_write_register(uint8_t instance, uint8_t reg_addr, uint8_t value) {
    uint8_t buf[2] = { reg_addr, value };
    return spi_write_blocking(instance, buf, 2);
}

spi_result_t spi_read_register(uint8_t instance, uint8_t reg_addr, uint8_t *value) {
    if (value == NULL)
        return SPI_ERR_NULL_BUF;

    uint8_t tx_buf[2] = { (uint8_t)(reg_addr | 0x80), 0xFF };
    uint8_t rx_buf[2] = { 0, 0 };
    spi_result_t result = spi_transfer_blocking(instance, tx_buf, rx_buf, 2);
    if (result != SPI_OK)
        return result;
    *value = rx_buf[1];
    return SPI_OK;
}

spi_result_t spi_set_baud(uint8_t instance, uint32_t baud_hz) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;
    if (baud_hz == 0)
        return SPI_ERR_INVALID_ARG;
    if (!g_spi[instance].initialized)
        return SPI_ERR_NOT_INIT;

#ifdef DRIVER_HOST_TEST
    g_spi[instance].baud_hz = baud_hz;
    return SPI_OK;
#else
    /* Disable, reconfigure baud, re-enable */
    *spi_reg(instance, SPI_REG_CR1) = 0;
    uint8_t cpsr, scr;
    spi_compute_baud(baud_hz, &cpsr, &scr);
    *spi_reg(instance, SPI_REG_CPSR) = cpsr;
    uint32_t cr0 = *spi_reg(instance, SPI_REG_CR0);
    cr0 &= 0x00FF;  /* Clear SCR bits */
    cr0 |= ((uint32_t)scr << 8);
    *spi_reg(instance, SPI_REG_CR0) = cr0;
    *spi_reg(instance, SPI_REG_CR1) = SPI_CR1_SSE;
    g_spi[instance].baud_hz = baud_hz;
    __asm__ volatile ("dmb" ::: "memory");
    return SPI_OK;
#endif
}

uint32_t spi_get_baud(uint8_t instance) {
    if (instance >= SPI_INSTANCE_COUNT)
        return 0;
    return g_spi[instance].baud_hz;
}

bool spi_is_initialized(uint8_t instance) {
    if (instance >= SPI_INSTANCE_COUNT)
        return false;
    return g_spi[instance].initialized;
}

spi_result_t spi_deinit(uint8_t instance) {
    if (instance >= SPI_INSTANCE_COUNT)
        return SPI_ERR_INVALID_INST;

#ifdef DRIVER_HOST_TEST
    g_spi[instance].initialized = false;
    return SPI_OK;
#else
    if (!g_spi[instance].initialized)
        return SPI_OK;
    *spi_reg(instance, SPI_REG_CR1) = 0;
    __asm__ volatile ("dmb" ::: "memory");
    g_spi[instance].initialized = false;
    return SPI_OK;
#endif
}