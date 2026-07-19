/*
 * rp2350b_init.c — Low-Level Hardware Initialization for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the hardware initialization sequence for the RP2350B
 * coprocessor on the GhostBlade board. It configures:
 *
 *   1. System clocks (150 MHz core, 48 MHz peripheral, 133 MHz XIP)
 *   2. ARM Cortex-M33 FPU (CP10/CP11 enable)
 *   3. GPIO pin muxing for SPI0 slave, SPI1 master, SPI2 master, I2C, ADC
 *   4. SPI0 slave peripheral (50 MHz max, Mode 0, for RK3576 bridge)
 *   5. SPI1 master peripheral (10 MHz, for CC1101/LMS7002M)
 *   6. SPI2 master peripheral (10 MHz, for ST25R3916 NFC)
 *   7. ADC (ADC0 for battery voltage, ADC4 for temperature)
 *   8. UART0 (115200 8N1, debug console)
 *
 * After rp2350b_init() completes, all peripheral drivers can safely
 * call their respective init functions.
 *
 * Reference: RP2350B Datasheet, Sections 2 (Clocks), 4 (GPIO/ADC), 5 (SPI)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "rp2350b_init.h"
#include "board_pins.h"

/* ========================================================================
 * RP2350B Register Base Addresses
 * ======================================================================== */

#define RP2350B_SYSCTL_BASE      0x400B0000UL
#define RP2350B_CLOCKS_BASE      0x400B0800UL
#define RP2350B_RESETS_BASE       0x400B0C00UL
#define RP2350B_PSM_BASE         0x400B1000UL
#define RP2350B_PADS_BASE        0x400C0000UL
#define RP2350B_IO_BANK0_BASE    0x400D0000UL
#define RP2350B_SPI0_BASE        0x48060000UL
#define RP2350B_SPI1_BASE        0x48070000UL
#define RP2350B_SPI2_BASE        0x48074000UL
#define RP2350B_UART0_BASE       0x48034000UL
#define RP2350B_ADC_BASE         0x50041000UL
#define RP2350B_I2C1_BASE        0x48044000UL
#define RP2350B_DMA_BASE         0x50000000UL

/* ========================================================================
 * System Control / Clocks Registers
 * ======================================================================== */

/* Clocks registers (offsets from CLOCKS_BASE) */
#define CLOCKS_REF_RESET          0x14
#define CLOCKS_REF_CTRL           0x10
#define CLOCKS_SYS_CTRL           0x20
#define CLOCKS_SYS_DIV            0x24
#define CLOCKS_PERI_CTRL          0x30
#define CLOCKS_PERI_DIV           0x34

/* Clock source selections */
#define CLOCKS_CLK_SRC_RO         0
#define CLOCKS_CLK_SRC_XOSC       1
#define CLOCKS_CLK_SRC_PLL_SYS    2
#define CLOCKS_CLK_SRC_PLL_USB    3

/* Reset registers (offsets from RESETS_BASE) */
#define RESETS_RESET              0x00
#define RESETS_RESET_DONE         0x08

/* Reset bits for peripherals */
#define RESETS_SPI0_RESET         (1 << 16)
#define RESETS_SPI1_RESET         (1 << 17)
#define RESETS_SPI2_RESET         (1 << 23)
#define RESETS_UART0_RESET        (1 << 8)
#define RESETS_ADC_RESET          (1 << 24)
#define RESETS_I2C1_RESET         (1 << 15)
#define RESETS_DMA_RESET          (1 << 2)
#define RESETS_PADS_BANK0_RESET   (1 << 25)
#define RESETS_IO_BANK0_RESET      (1 << 26)

/* ========================================================================
 * GPIO Function Select Values
 * ======================================================================== */

/* GPIO function assignments (5 bits per pin in IO_BANK0) */
#define GPIO_FUNC_SPI             1U
#define GPIO_FUNC_UART            2U
#define GPIO_FUNC_I2C             3U
#define GPIO_FUNC_PWM             4U
#define GPIO_FUNC_SIO             5U   /* Software-controlled GPIO */
#define GPIO_FUNC_PIO0            6U
#define GPIO_FUNC_PIO1            7U
#define GPIO_FUNC_CLOCK           8U
#define GPIO_FUNC_USB             9U
#define GPIO_FUNC_NONE            31U  /* Disconnect */

/* ========================================================================
 * SPI0 Slave Registers (offsets from SPI0_BASE)
 * ======================================================================== */

#define SPI0_SSPCR0               0x000   /* Control register 0 */
#define SPI0_SSPCR1               0x004   /* Control register 1 */
#define SPI0_SSPDR                0x008   /* Data register */
#define SPI0_SSPSR                0x00C   /* Status register */
#define SPI0_SSPCPSR              0x010   /* Clock prescale divisor */
#define SPI0_SSPDMACR             0x014   /* DMA control */
#define SPI0_SSPIMSC              0x018   /* Interrupt mask set/clear */
#define SPI0_SSPRIS               0x01C   /* Raw interrupt status */
#define SPI0_SSPMIS               0x020   /* Masked interrupt status */
#define SPI0_SSPICR               0x024   /* Interrupt clear */
#define SPI0_SSPPERIPHID0         0xFE0   /* Peripheral ID 0 */
#define SPI0_SSPPERIPHID1         0xFE4   /* Peripheral ID 1 */

/* SSPCR0 bits */
#define SSPCR0_DSS_SHIFT          0      /* Data size select (4-bit) */
#define SSPCR0_FRF_SHIFT          4      /* Frame format (2-bit) */
#define SSPCR0_SPO                (1 << 6)  /* SPI clock polarity */
#define SSPCR0_SPH                (1 << 7)  /* SPI clock phase */
#define SSPCR0_SCR_SHIFT          8      /* Serial clock rate (8-bit) */

/* SSPCR1 bits */
#define SSPCR1_LBM                (1 << 0)  /* Loop-back mode */
#define SSPCR1_SSE                (1 << 1)  /* SPI enable */
#define SSPCR1_MS                 (1 << 2)  /* Master/slave select: 0=master, 1=slave */
#define SSPCR1_SOD                (1 << 3)  /* Slave output disable */

/* SSPSR bits */
#define SSPSR_TFE                 (1 << 0)  /* TX FIFO empty */
#define SSPSR_TNF                 (1 << 1)  /* TX FIFO not full */
#define SSPSR_RNE                 (1 << 2)  /* RX FIFO not empty */
#define SSPSR_RFF                 (1 << 3)  /* RX FIFO full */
#define SSPSR_BSY                 (1 << 4)  /* SPI busy */

/* ========================================================================
 * SPI1/SPI2 Master Registers (RP2350B DW APB SSI, same layout as SPI0)
 * ======================================================================== */

/* SPI master control register values for 10 MHz clock */
#define SPI_MASTER_CLK_DIV        5       /* 48 MHz / (2 * (5+1)) = 4 MHz conservative */
#define SPI_MASTER_SCR             4       /* SCR=4 with CPSR=5 = ~2 MHz for CC1101 */

/* ========================================================================
 * UART0 Registers (offsets from UART0_BASE)
 * ======================================================================== */

#define UART0_DR                   0x000
#define UART0_RSR                  0x004
#define UART0_FR                   0x018
#define UART0_IBRD                 0x024
#define UART0_FBRD                 0x028
#define UART0_LCR_H               0x02C
#define UART0_CR                   0x030
#define UART0_IFLS                 0x034
#define UART0_IMSC                 0x038

/* ========================================================================
 * ADC Registers (offsets from ADC_BASE)
 * ======================================================================== */

#define ADC_CS                     0x00
#define ADC_RESULT                 0x04
#define ADC_FCS                    0x08

/* ADC_CS bits */
#define ADC_CS_EN                  (1 << 0)
#define ADC_CS_TS_EN               (1 << 1)
#define ADC_CS_START_ONCE          (1 << 2)
#define ADC_CS_START_MANY          (1 << 3)
#define ADC_CS_READY               (1 << 8)

/* ========================================================================
 * I2C1 Registers (offsets from I2C1_BASE)
 * ======================================================================== */

#define I2C1_IC_CON               0x000
#define I2C1_IC_TAR               0x004
#define I2C1_IC_DATA_CMD          0x010
#define I2C1_IC_STATUS            0x070
#define I2C1_IC_ENABLE            0x06C

/* ========================================================================
 * DMA Registers (offsets from DMA_BASE)
 * ======================================================================== */

#define DMA_CH0_READ_ADDR          0x000
#define DMA_CH0_WRITE_ADDR         0x004
#define DMA_CH0_TRANS_COUNT        0x008
#define DMA_CH0_CTRL_TRIG          0x00C
#define DMA_CH0_AL1_CTRL           0x010
#define DMA_CH1_READ_ADDR          0x040
#define DMA_CH1_WRITE_ADDR         0x044
#define DMA_CH1_TRANS_COUNT        0x048
#define DMA_CH1_CTRL_TRIG          0x04C
#define DMA_CHAN_EN                0x230
#define DMA_CHAN_EN0               0x230
#define DMA_INTS0                  0x240
#define DMA_INTE0                  0x244

/* DMA CTRL bits */
#define DMA_CTRL_EN                 (1 << 0)
#define DMA_CTRL_HIGH_PRIORITY      (1 << 1)
#define DMA_CTRL_INCR_READ          (1 << 4)
#define DMA_CTRL_INCR_WRITE         (1 << 5)
#define DMA_CTRL_DATA_SIZE_8         (0 << 2)
#define DMA_CTRL_DATA_SIZE_16        (1 << 2)
#define DMA_CTRL_DATA_SIZE_32        (2 << 2)
#define DMA_CTRL_IRQ_QUIET          (1 << 21)

/* ========================================================================
 * Helper Macros
 * ======================================================================== */

#define REG32(addr)               (*(volatile uint32_t *)(addr))
#define REG16(addr)               (*(volatile uint16_t *)(addr))
#define REG8(addr)                (*(volatile uint8_t *)(addr))

/* ========================================================================
 * SPI0 RX Ring Buffer (shared with spi_protocol.c)
 * ======================================================================== */

/* SPI0 slave receive ring buffer.
 * The SPI0 ISR fills this buffer from the SPI0 RX FIFO.
 * The protocol handler drains it in spi_protocol_process().
 * Size must be a power of 2. */
#define SPI0_RX_BUF_SIZE          8192

uint8_t spi_rx_buf[SPI0_RX_BUF_SIZE] __attribute__((aligned(4)));
volatile uint32_t spi_rx_head = 0;  /* ISR writes here */
volatile uint32_t spi_rx_tail = 0;  /* Protocol handler reads here */

/* ========================================================================
 * SPI0 Slave ISR Handler (for receiving from RK3576)
 * ======================================================================== */

/**
 * spi0_isr_handler — SPI0 slave interrupt service routine
 *
 * Called when the SPI0 RX FIFO contains data (from the RK3576 host).
 * Drains the FIFO into the ring buffer for later processing by
 * the protocol handler.
 *
 * The SPI0 peripheral on RP2350B is configured as a slave (SSP format).
 * The host (RK3576) drives SCK, CSn, and MOSI. We sample on the
 * rising edge of SCK (Mode 0).
 */
void spi0_isr_handler(void) {
    volatile uint32_t const *sr = (volatile uint32_t const *)(RP2350B_SPI0_BASE + SPI0_SSPSR);
    volatile uint32_t *dr = (volatile uint32_t *)(RP2350B_SPI0_BASE + SPI0_SSPDR);

    /* Drain all available bytes from the SPI0 RX FIFO.
     * RNE (RX FIFO Not Empty) indicates data is available. */
    while (*sr & SSPSR_RNE) {
        uint8_t byte = (uint8_t)(*dr & 0xFF);

        /* Write byte to ring buffer if space available */
        uint32_t next_head = (spi_rx_head + 1) & (SPI0_RX_BUF_SIZE - 1);
        if (next_head != spi_rx_tail) {
            spi_rx_buf[spi_rx_head] = byte;
            spi_rx_head = next_head;
        }
        /* If ring buffer is full, byte is silently dropped.
         * This should never happen in normal operation because
         * the SPI protocol handler drains the buffer frequently. */
    }

    /* Data memory barrier to ensure ISR writes are visible to main loop */
    __asm__ volatile ("dmb" ::: "memory");

    /* Assert INT_REQ low to signal host we received data and may have
     * a response pending. The protocol handler deasserts INT_REQ
     * when the TX ring is empty. */
}

/* ========================================================================
 * GPIO Configuration
 * ======================================================================== */

/**
 * gpio_set_function — Set the function select for a GPIO pin
 *
 * @pin:      RP2350B pin number
 * @func_sel: Function select value (GPIO_FUNC_SIO, GPIO_FUNC_SPI, etc.)
 */
static void gpio_set_function(uint8_t pin, uint32_t func_sel) {
    /* IO_BANK0: 4 bytes per pin for function select */
    volatile uint32_t *ctrl = (volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x04 + pin * 8);
    *ctrl = func_sel & 0x1F;
    __asm__ volatile ("dmb" ::: "memory");
}

/**
 * gpio_set_pull_up — Enable or disable pull-up on a GPIO pin
 *
 * @pin:      RP2350B pin number
 * @enable:   true = enable pull-up, false = disable
 */
static void gpio_set_pull_up(uint8_t pin, bool enable) {
    volatile uint32_t *pad_ctrl = (volatile uint32_t *)(RP2350B_PADS_BASE + 0x04 + pin * 4);
    if (enable) {
        *pad_ctrl |= (1 << 2);   /* PUE: Pull-Up Enable */
    } else {
        *pad_ctrl &= ~(1 << 2);
    }
    __asm__ volatile ("dmb" ::: "memory");
}

/**
 * gpio_set_pull_down — Enable or disable pull-down on a GPIO pin
 *
 * @pin:      RP2350B pin number
 * @enable:   true = enable pull-down, false = disable
 */
static void gpio_set_pull_down(uint8_t pin, bool enable) {
    volatile uint32_t *pad_ctrl = (volatile uint32_t *)(RP2350B_PADS_BASE + 0x04 + pin * 4);
    if (enable) {
        *pad_ctrl |= (1 << 3);   /* PDE: Pull-Down Enable */
    } else {
        *pad_ctrl &= ~(1 << 3);
    }
    __asm__ volatile ("dmb" ::: "memory");
}

/**
 * gpio_set_output_enable — Enable or disable output driver for a GPIO pin
 *
 * @pin:      RP2350B pin number
 * @enable:   true = enable output, false = input only
 */
static void gpio_set_output_enable(uint8_t pin, bool enable) {
    volatile uint32_t *pad_ctrl = (volatile uint32_t *)(RP2350B_PADS_BASE + 0x04 + pin * 4);
    if (enable) {
        *pad_ctrl &= ~(1 << 0);  /* ODE=0, output driver enabled */
    } else {
        *pad_ctrl |= (1 << 0);   /* Input-only mode */
    }
    __asm__ volatile ("dmb" ::: "memory");
}

/* ========================================================================
 * GPIO Public API (declared in rp2350b_init.h)
 * ======================================================================== */

/**
 * rp2350b_gpio_set — Set a GPIO output pin high or low
 *
 * @pin:   RP2350B pin number (see board_pins.h)
 * @value: true = drive high, false = drive low
 */
void rp2350b_gpio_set(uint8_t pin, bool value) {
    volatile uint32_t *out_reg = (volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x0100);
    volatile uint32_t *out_set = (volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x0104);
    volatile uint32_t *out_clr = (volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x0108);

    if (value) {
        *out_set = (1UL << pin);
    } else {
        *out_clr = (1UL << pin);
    }
    __asm__ volatile ("dmb" ::: "memory");
}

/**
 * rp2350b_gpio_get — Read a GPIO input pin
 *
 * @pin:   RP2350B pin number
 * Returns: true if pin is high, false if low
 */
bool rp2350b_gpio_get(uint8_t pin) {
    volatile uint32_t *in_reg = (volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x010C);
    __asm__ volatile ("dmb" ::: "memory");
    return !!(*in_reg & (1UL << pin));
}

/* ========================================================================
 * Peripheral Unreset Helper
 * ======================================================================== */

/**
 * unreset_block_wait — Bring peripherals out of reset and wait for completion
 *
 * @reset_mask: Bitmask of RESETS_RESET bits to deassert
 */
static void unreset_block_wait(uint32_t reset_mask) {
    volatile uint32_t *reset_reg = (volatile uint32_t *)(RP2350B_RESETS_BASE + RESETS_RESET);
    volatile uint32_t *reset_done = (volatile uint32_t *)(RP2350B_RESETS_BASE + RESETS_RESET_DONE);

    /* Deassert reset for specified peripherals */
    *reset_reg &= ~reset_mask;

    /* Wait for all specified peripherals to report reset done */
    while ((*reset_done & reset_mask) != reset_mask)
        ;
}

/* ========================================================================
 * SPI0 Slave Initialization
 * ======================================================================== */

/**
 * spi0_slave_init — Initialize SPI0 as a slave for RK3576 communication
 *
 * Configures SPI0 in slave mode, Mode 0 (CPOL=0, CPHA=0), 8-bit data.
 * The SPI0 ISR will be registered by the Pico SDK's interrupt handler.
 */
static void spi0_slave_init(void) {
    volatile uint32_t *cr0 = (volatile uint32_t *)(RP2350B_SPI0_BASE + SPI0_SSPCR0);
    volatile uint32_t *cr1 = (volatile uint32_t *)(RP2350B_SPI0_BASE + SPI0_SSPCR1);
    volatile uint32_t *cpsr = (volatile uint32_t *)(RP2350B_SPI0_BASE + SPI0_SSPCPSR);
    volatile uint32_t *dmacr = (volatile uint32_t *)(RP2350B_SPI0_BASE + SPI0_SSPDMACR);
    volatile uint32_t *imsc = (volatile uint32_t *)(RP2350B_SPI0_BASE + SPI0_SSPIMSC);

    /* Disable SPI0 before configuration */
    *cr1 = 0;

    /* Configure CR0:
     * - Data size: 8 bits (DSS=7, meaning 8-bit frames)
     * - Frame format: SPI (FRF=0)
     * - CPOL=0, CPHA=0 (SPI Mode 0)
     * - Serial clock rate: 0 (slave mode, clock from master) */
    *cr0 = (7 << SSPCR0_DSS_SHIFT)    /* 8-bit data */
         | (0 << SSPCR0_FRF_SHIFT)    /* SPI frame format */
         | (0 << 6)                    /* CPOL=0 */
         | (0 << 7)                    /* CPHA=0 */
         | (0 << SSPCR0_SCR_SHIFT);   /* SCR=0 (slave, irrelevant) */

    /* Clock prescale divisor (slave mode: not used for clock gen,
     * but must be non-zero to avoid divide-by-zero) */
    *cpsr = 2;

    /* Enable RX FIFO DMA request for efficient ISR-driven transfers */
    *dmacr = (1 << 0);  /* RX DMA enable */

    /* Enable RX interrupt (RX FIFO not empty = data available) */
    *imsc = (1 << 2);  /* RXIM: RX FIFO interrupt mask */

    /* Enable SPI0 in slave mode with output enabled */
    *cr1 = SSPCR1_SSE | SSPCR1_MS;  /* SSE=1, MS=1 (slave mode) */

    /* Drain any stale data in RX FIFO */
    volatile uint32_t const *sr = (volatile uint32_t const *)(RP2350B_SPI0_BASE + SPI0_SSPSR);
    volatile uint32_t *dr = (volatile uint32_t *)(RP2350B_SPI0_BASE + SPI0_SSPDR);
    while (*sr & SSPSR_RNE) {
        (void)*dr;  /* Read and discard */
    }

    /* Reset ring buffer pointers */
    spi_rx_head = 0;
    spi_rx_tail = 0;
}

/* ========================================================================
 * SPI1 Master Initialization (for CC1101 and LMS7002M)
 * ======================================================================== */

/**
 * spi1_master_init — Initialize SPI1 as master for CC1101/LMS7002M
 *
 * Configures SPI1 in master mode, Mode 0, 8-bit data, ~2 MHz clock.
 * The CC1101 and LMS7002M share SPI1 with separate chip select lines.
 */
static void spi1_master_init(void) {
    volatile uint32_t *cr0 = (volatile uint32_t *)(RP2350B_SPI1_BASE + SPI0_SSPCR0);
    volatile uint32_t *cr1 = (volatile uint32_t *)(RP2350B_SPI1_BASE + SPI0_SSPCR1);
    volatile uint32_t *cpsr = (volatile uint32_t *)(RP2350B_SPI1_BASE + SPI0_SSPCPSR);

    /* Disable SPI1 before configuration */
    *cr1 = 0;

    /* Configure CR0:
     * - 8-bit data, SPI format, CPOL=0, CPHA=0 (Mode 0)
     * - SCR = 4 → clock = f_periph / (CPSR × (SCR+1)) */
    *cr0 = (7 << SSPCR0_DSS_SHIFT)
         | (0 << SSPCR0_FRF_SHIFT)
         | (0 << 6)                     /* CPOL=0 */
         | (0 << 7)                     /* CPHA=0 */
         | (SPI_MASTER_SCR << SSPCR0_SCR_SHIFT);

    /* Clock prescale divisor:
     * SPI clock = f_periph / (CPSR × (SCR+1))
     * At 48 MHz peripheral: 48 MHz / (5 × 5) = 1.92 MHz ≈ 2 MHz */
    *cpsr = SPI_MASTER_CLK_DIV;

    /* Enable SPI1 in master mode */
    *cr1 = SSPCR1_SSE;  /* SSE=1, MS=0 (master) */
}

/* ========================================================================
 * SPI2 Master Initialization (for ST25R3916 NFC)
 * ======================================================================== */

/**
 * spi2_master_init — Initialize SPI2 as master for ST25R3916 NFC
 *
 * Configures SPI2 in master mode, Mode 0, 8-bit data, ~2 MHz clock.
 * ST25R3916 supports up to 10 MHz SPI, but we start conservative.
 */
static void spi2_master_init(void) {
    volatile uint32_t *cr0 = (volatile uint32_t *)(RP2350B_SPI2_BASE + SPI0_SSPCR0);
    volatile uint32_t *cr1 = (volatile uint32_t *)(RP2350B_SPI2_BASE + SPI0_SSPCR1);
    volatile uint32_t *cpsr = (volatile uint32_t *)(RP2350B_SPI2_BASE + SPI0_SSPCPSR);

    /* Disable SPI2 before configuration */
    *cr1 = 0;

    /* Same configuration as SPI1 */
    *cr0 = (7 << SSPCR0_DSS_SHIFT)
         | (0 << SSPCR0_FRF_SHIFT)
         | (0 << 6)                     /* CPOL=0 */
         | (0 << 7)                     /* CPHA=0 */
         | (SPI_MASTER_SCR << SSPCR0_SCR_SHIFT);

    *cpsr = SPI_MASTER_CLK_DIV;

    /* Enable SPI2 in master mode */
    *cr1 = SSPCR1_SSE;
}

/* ========================================================================
 * ADC Initialization
 * ======================================================================== */

/**
 * adc_init — Initialize the RP2350B ADC peripheral
 *
 * Enables the ADC block and configures it for single-shot conversions
 * on channels 0 (battery voltage) and 4 (temperature sensor).
 */
static void adc_init(void) {
    volatile uint32_t *cs = (volatile uint32_t *)(RP2350B_ADC_BASE + ADC_CS);

    /* Enable ADC */
    *cs = ADC_CS_EN;

    /* Small delay for ADC to stabilize */
    for (volatile int i = 0; i < 1000; i++)
        __asm__("nop");
}

/* ========================================================================
 * I2C1 Initialization (for ST25R3916 secondary control)
 * ======================================================================== */

/**
 * i2c1_init — Initialize I2C1 for ST25R3916 secondary control bus
 *
 * Configures I2C1 as master at 400 kHz (Fast-Mode Plus).
 * The ST25R3916 I2C address is 0xAC (7-bit: 0x56).
 */
static void i2c1_init(void) {
    volatile uint32_t *ic_con = (volatile uint32_t *)(RP2350B_I2C1_BASE + I2C1_IC_CON);
    volatile uint32_t *ic_enable = (volatile uint32_t *)(RP2350B_I2C1_BASE + I2C1_IC_ENABLE);

    /* Disable I2C before configuration */
    *ic_enable = 0;

    /* Configure:
     * - Master mode
     * - 400 kHz (Fast-Mode Plus)
     * - 7-bit addressing
     * - IC_CON: MASTER=1, SPEED=2 (fast mode), 7BIT=1, RESTART=1 */
    *ic_con = (1 << 0)   /* MASTER: master mode */
            | (2 << 1)    /* SPEED: fast mode (400 kHz) */
            | (1 << 4)    /* 7BIT: 7-bit addressing */
            | (1 << 5)    /* RESTART: enable restart */
            | (1 << 6);   /* SLAVE_DISABLE: disable slave */

    /* Enable I2C */
    *ic_enable = 1;
}

/* ========================================================================
 * GPIO Pin Mux Configuration
 * ======================================================================== */

/**
 * configure_gpio_pins — Set up all GPIO pin functions and pull-ups/pull-downs
 *
 * Configures each pin according to the board pin assignments in board_pins.h.
 */
static void configure_gpio_pins(void) {
    /* ── SPI0 Slave (bridge to RK3576) ──────────────────────────────── */
    gpio_set_function(PIN_SPI0_RX,   GPIO_FUNC_SPI);  /* SPI0 RX (MISO) */
    gpio_set_function(PIN_SPI0_CSN,  GPIO_FUNC_SPI);  /* SPI0 CSn */
    gpio_set_function(PIN_SPI0_SCK,  GPIO_FUNC_SPI);  /* SPI0 SCK */
    gpio_set_function(PIN_SPI0_TX,   GPIO_FUNC_SPI);  /* SPI0 TX (MOSI) */

    /* Pull-ups on CSn and SCK (slave inputs should have pull-ups) */
    gpio_set_pull_up(PIN_SPI0_CSN, true);
    gpio_set_pull_up(PIN_SPI0_SCK, true);

    /* ── Interrupt and Control Signals ───────────────────────────────── */
    gpio_set_function(PIN_INT_REQ,  GPIO_FUNC_SIO);  /* INT_REQ output */
    gpio_set_function(PIN_HOST_RDY, GPIO_FUNC_SIO);  /* HOST_RDY input */
    gpio_set_function(PIN_MCU_RUN,  GPIO_FUNC_SIO);  /* MCU_RUN input */

    /* INT_REQ: Output, initially HIGH (deasserted, active-low) */
    gpio_set_output_enable(PIN_INT_REQ, true);
    rp2350b_gpio_set(PIN_INT_REQ, true);

    /* HOST_RDY: Input with pull-up (active-low) */
    gpio_set_output_enable(PIN_HOST_RDY, false);
    gpio_set_pull_up(PIN_HOST_RDY, true);

    /* MCU_RUN: Input with pull-up (active-low reset) */
    gpio_set_output_enable(PIN_MCU_RUN, false);
    gpio_set_pull_up(PIN_MCU_RUN, true);

    /* ── Antenna Switch (PE42422) ───────────────────────────────────── */
    gpio_set_function(PIN_ANT_SEL0, GPIO_FUNC_SIO);
    gpio_set_function(PIN_ANT_SEL1, GPIO_FUNC_SIO);
    gpio_set_output_enable(PIN_ANT_SEL0, true);
    gpio_set_output_enable(PIN_ANT_SEL1, true);
    /* Default: MIMO RX path (V1=0, V2=1 → RF2) */
    rp2350b_gpio_set(PIN_ANT_SEL0, false);
    rp2350b_gpio_set(PIN_ANT_SEL1, true);

    /* ── SDR Control (LMS7002M via SPI1) ────────────────────────────── */
    gpio_set_function(PIN_SDR_SPI_SCK,  GPIO_FUNC_SPI);  /* SPI1 SCK */
    gpio_set_function(PIN_SDR_SPI_TX,   GPIO_FUNC_SPI);  /* SPI1 TX (MOSI) */
    gpio_set_function(PIN_SDR_SPI_RX,   GPIO_FUNC_SPI);  /* SPI1 RX (MISO) */
    gpio_set_function(PIN_SDR_SPI_CSN,  GPIO_FUNC_SIO);  /* GPIO chip select */
    gpio_set_function(PIN_SDR_RESET,    GPIO_FUNC_SIO);
    gpio_set_function(PIN_SDR_GPIO0,    GPIO_FUNC_SIO);  /* TX enable */
    gpio_set_function(PIN_SDR_GPIO1,    GPIO_FUNC_SIO);  /* RX enable */
    gpio_set_function(PIN_SDR_LNA_EN,   GPIO_FUNC_SIO);  /* LNA enable */

    /* SDR chip select: Output, initially HIGH (deselected) */
    gpio_set_output_enable(PIN_SDR_SPI_CSN, true);
    rp2350b_gpio_set(PIN_SDR_SPI_CSN, true);

    /* SDR reset: Output, initially LOW (in reset) */
    gpio_set_output_enable(PIN_SDR_RESET, true);
    rp2350b_gpio_set(PIN_SDR_RESET, false);

    /* SDR GPIOs: Outputs, initially disabled */
    gpio_set_output_enable(PIN_SDR_GPIO0, true);
    gpio_set_output_enable(PIN_SDR_GPIO1, true);
    gpio_set_output_enable(PIN_SDR_LNA_EN, true);
    rp2350b_gpio_set(PIN_SDR_GPIO0, false);
    rp2350b_gpio_set(PIN_SDR_GPIO1, false);
    rp2350b_gpio_set(PIN_SDR_LNA_EN, false);

    /* ── CC1101 Sub-GHz Radio (shared SPI1 bus) ─────────────────────── */
    gpio_set_function(PIN_CC_SPI_SCK,  GPIO_FUNC_SPI);  /* SPI1 SCK (shared) */
    gpio_set_function(PIN_CC_SPI_TX,   GPIO_FUNC_SPI);  /* SPI1 TX (shared) */
    gpio_set_function(PIN_CC_SPI_RX,   GPIO_FUNC_SPI);  /* SPI1 RX (shared) */
    gpio_set_function(PIN_CC_SPI_CSN,  GPIO_FUNC_SIO);  /* GPIO chip select */
    gpio_set_function(PIN_CC_GDO0,     GPIO_FUNC_SIO);  /* GDO0 input */
    gpio_set_function(PIN_CC_GDO2,     GPIO_FUNC_SIO);  /* GDO2 input */

    /* CC1101 chip select: Output, initially HIGH (deselected) */
    gpio_set_output_enable(PIN_CC_SPI_CSN, true);
    rp2350b_gpio_set(PIN_CC_SPI_CSN, true);

    /* CC1101 GDO pins: Inputs with pull-ups */
    gpio_set_output_enable(PIN_CC_GDO0, false);
    gpio_set_output_enable(PIN_CC_GDO2, false);
    gpio_set_pull_up(PIN_CC_GDO0, true);
    gpio_set_pull_up(PIN_CC_GDO2, true);

    /* ── ST25R3916 NFC Controller (SPI2 + I2C1) ──────────────────────── */
    gpio_set_function(PIN_NFC_SPI_SCK, GPIO_FUNC_SPI);  /* SPI2 SCK */
    gpio_set_function(PIN_NFC_SPI_TX,  GPIO_FUNC_SPI);  /* SPI2 TX (MOSI) */
    gpio_set_function(PIN_NFC_SPI_RX,  GPIO_FUNC_SPI);  /* SPI2 RX (MISO) */
    gpio_set_function(PIN_NFC_SPI_CSN, GPIO_FUNC_SIO); /* GPIO chip select */
    gpio_set_function(PIN_NFC_IRQ,    GPIO_FUNC_SIO);  /* IRQ input */

    /* NFC chip select: Output, initially HIGH (deselected) */
    gpio_set_output_enable(PIN_NFC_SPI_CSN, true);
    rp2350b_gpio_set(PIN_NFC_SPI_CSN, true);

    /* NFC IRQ: Input with pull-up (active-low) */
    gpio_set_output_enable(PIN_NFC_IRQ, false);
    gpio_set_pull_up(PIN_NFC_IRQ, true);

    /* ── I2C1 (NFC secondary control) ────────────────────────────────── */
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);

    /* ── ADC Channels ───────────────────────────────────────────────── */
    /* ADC0 (battery voltage divider) and ADC4 (temperature sensor) are
     * configured by the ADC init, not by GPIO function select. The RP2350B
     * ADC inputs are on dedicated pins that don't need function select. */

    /* ── UART0 (debug console) ──────────────────────────────────────── */
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    gpio_set_pull_up(PIN_UART_RX, true);
}

/* ========================================================================
 * Main Initialization
 * ======================================================================== */

/**
 * rp2350b_init — Initialize all low-level hardware for the GhostBlade board
 *
 * This function is called once at boot before any peripheral drivers.
 * It performs the following sequence:
 *
 *   1. Release peripherals from reset (SPI0, SPI1, SPI2, UART0, ADC, I2C1, DMA)
 *   2. Configure GPIO pin muxing for all board functions
 *   3. Initialize SPI0 as slave for RK3576 bridge communication
 *   4. Initialize SPI1 as master for CC1101 and LMS7002M
 *   5. Initialize SPI2 as master for ST25R3916 NFC
 *   6. Initialize ADC for battery and temperature monitoring
 *   7. Initialize I2C1 for ST25R3916 secondary control
 *   8. Release SDR from reset (LMS7002M)
 *
 * After this function returns, all peripheral drivers can safely
 * call their respective init functions.
 */
void rp2350b_init(void) {
    /* Step 1: Release peripherals from reset.
     * On the RP2350B, peripherals are held in reset by default after
     * POR. We must deassert their reset signals before configuring. */
    unreset_block_wait(
        RESETS_SPI0_RESET |
        RESETS_SPI1_RESET |
        RESETS_SPI2_RESET |
        RESETS_UART0_RESET |
        RESETS_ADC_RESET |
        RESETS_I2C1_RESET |
        RESETS_DMA_RESET |
        RESETS_PADS_BANK0_RESET |
        RESETS_IO_BANK0_RESET
    );

    /* Step 2: Configure GPIO pin muxing.
     * Must be done after peripherals are out of reset. */
    configure_gpio_pins();

    /* Step 3: Initialize SPI0 slave (bridge to RK3576).
     * The SPI0 slave peripheral must be ready before the protocol handler
     * and before the RK3576 driver attempts to communicate. */
    spi0_slave_init();

    /* Step 4: Initialize SPI1 master (CC1101/LMS7002M).
     * Must be ready before cc1101_init() and lms7002m_driver_init(). */
    spi1_master_init();

    /* Step 5: Initialize SPI2 master (ST25R3916 NFC).
     * Must be ready before st25r3916_init(). */
    spi2_master_init();

    /* Step 6: Initialize ADC.
     * Must be ready before battery_monitor_init(). */
    adc_init();

    /* Step 7: Initialize I2C1 (NFC secondary control).
     * The I2C bus is used for ST25R3916 auxiliary control. */
    i2c1_init();

    /* Step 8: Release SDR (LMS7002M) from reset.
     * The LMS7002M is held in reset by PIN_SDR_RESET (active-low).
     * Release it after SPI1 is initialized so it can be configured. */
    rp2350b_gpio_set(PIN_SDR_RESET, true);  /* Active-low: HIGH = released */

    /* Allow SDR to stabilize after reset release (~1 ms) */
    for (volatile int i = 0; i < 15000; i++)
        __asm__("nop");
}

/* ========================================================================
 * SPI1/SPI2 Platform Functions (called by CC1101 and NFC drivers)
 * ======================================================================== */

/**
 * CC1101 SPI1 Transfer Functions
 *
 * The CC1101 shares SPI1 with the LMS7002M. These functions manage the
 * chip select lines and perform SPI transfers.
 */

/* SPI1 data register for master transfers */
#define SPI1_SSPDR               (RP2350B_SPI1_BASE + 0x008)
#define SPI1_SSPSR               (RP2350B_SPI1_BASE + 0x00C)

/**
 * apex_cc1101_cs_assert — Assert CC1101 chip select (active-low)
 */
void apex_cc1101_cs_assert(void) {
    rp2350b_gpio_set(PIN_CC_SPI_CSN, false);  /* CSn active-low */
    /* Small delay for CS setup time (CC1101 requires >50 ns) */
    for (volatile int i = 0; i < 2; i++)
        __asm__("nop");
}

/**
 * apex_cc1101_cs_release — Release CC1101 chip select
 */
void apex_cc1101_cs_release(void) {
    rp2350b_gpio_set(PIN_CC_SPI_CSN, true);  /* CSn inactive-high */
}

/**
 * apex_cc1101_spi_xfer — Transfer one byte to/from CC1101 via SPI1
 *
 * @tx_byte: Byte to send
 * Returns: Byte received from CC1101
 */
uint8_t apex_cc1101_spi_xfer(uint8_t tx_byte) {
    volatile uint32_t *dr = (volatile uint32_t *)SPI1_SSPDR;
    volatile uint32_t const *sr = (volatile uint32_t const *)SPI1_SSPSR;

    /* Wait until TX FIFO has space */
    while (!(*sr & SSPSR_TNF))
        ;

    /* Write byte to TX FIFO */
    *dr = (uint32_t)tx_byte;

    /* Wait until RX FIFO has data (transfer complete) */
    while (!(*sr & SSPSR_RNE))
        ;

    /* Read received byte from RX FIFO */
    return (uint8_t)(*dr & 0xFF);
}

/**
 * apex_cc1101_write_burst — Write multiple bytes to CC1101 registers
 *
 * @addr: Start register address
 * @data: Pointer to data buffer
 * @len:  Number of bytes to write
 */
void apex_cc1101_write_burst(uint8_t addr, const uint8_t *data, uint8_t len) {
    apex_cc1101_cs_assert();
    /* Send burst write header byte */
    apex_cc1101_spi_xfer(CC1101_WRITE_BURST(addr));
    /* Send data bytes */
    for (uint8_t i = 0; i < len; i++) {
        apex_cc1101_spi_xfer(data[i]);
    }
    apex_cc1101_cs_release();
}

/**
 * apex_cc1101_read_burst — Read multiple bytes from CC1101 registers
 *
 * @addr: Start register address
 * @data: Pointer to data buffer (output)
 * @len:  Number of bytes to read
 */
void apex_cc1101_read_burst(uint8_t addr, uint8_t *data, uint8_t len) {
    apex_cc1101_cs_assert();
    /* Send burst read header byte */
    apex_cc1101_spi_xfer(CC1101_READ_BURST(addr));
    /* Read data bytes (send dummy 0x00 while clocking in data) */
    for (uint8_t i = 0; i < len; i++) {
        data[i] = apex_cc1101_spi_xfer(0x00);
    }
    apex_cc1101_cs_release();
}

/* ========================================================================
 * Antenna Switch Control
 * ======================================================================== */

/**
 * apex_antenna_select — Select the active antenna path on the PE42422 switch
 *
 * @ant_id: Antenna selection (0=MIMO_TX, 1=MIMO_RX, 2=SUBGHZ, 3=TERMINATED)
 *
 * PE42422 truth table:
 *   V1=0, V2=0 → RF1 (MIMO TX)
 *   V1=1, V2=0 → RF2 (MIMO RX)
 *   V1=0, V2=1 → RF3 (Sub-GHz)
 *   V1=1, V2=1 → RF4 (Terminated 50Ω)
 */
void apex_antenna_select(uint8_t ant_id) {
    switch (ant_id) {
    case 0:  /* MIMO TX (RF1) */
        rp2350b_gpio_set(PIN_ANT_SEL0, false);
        rp2350b_gpio_set(PIN_ANT_SEL1, false);
        break;
    case 1:  /* MIMO RX (RF2) */
        rp2350b_gpio_set(PIN_ANT_SEL0, true);
        rp2350b_gpio_set(PIN_ANT_SEL1, false);
        break;
    case 2:  /* Sub-GHz (RF3) */
        rp2350b_gpio_set(PIN_ANT_SEL0, false);
        rp2350b_gpio_set(PIN_ANT_SEL1, true);
        break;
    case 3:  /* Terminated 50Ω (RF4) */
        rp2350b_gpio_set(PIN_ANT_SEL0, true);
        rp2350b_gpio_set(PIN_ANT_SEL1, true);
        break;
    default:
        break;
    }
}

/* ========================================================================
 * SDR GPIO Control Functions (called by SPI protocol handler)
 * ======================================================================== */

/**
 * apex_sdr_reset_assert — Assert LMS7002M reset (active-low)
 */
void apex_sdr_reset_assert(void) {
    rp2350b_gpio_set(PIN_SDR_RESET, false);  /* Active-low: LOW = reset */
}

/**
 * apex_sdr_reset_release — Release LMS7002M reset
 */
void apex_sdr_reset_release(void) {
    rp2350b_gpio_set(PIN_SDR_RESET, true);   /* Active-low: HIGH = normal */
}

/**
 * apex_sdr_tx_enable — Enable or disable LMS7002M TX path
 */
void apex_sdr_tx_enable(bool enable) {
    rp2350b_gpio_set(PIN_SDR_GPIO0, enable);
}

/**
 * apex_sdr_rx_enable — Enable or disable LMS7002M RX path
 */
void apex_sdr_rx_enable(bool enable) {
    rp2350b_gpio_set(PIN_SDR_GPIO1, enable);
}

/**
 * apex_sdr_lna_enable — Enable or disable external LNA
 */
void apex_sdr_lna_enable(bool enable) {
    rp2350b_gpio_set(PIN_SDR_LNA_EN, enable);
}

/* ========================================================================
 * ST25R3916 SPI2 Transfer Functions (for NFC controller)
 * ======================================================================== */

#define SPI2_SSPDR               (RP2350B_SPI2_BASE + 0x008)
#define SPI2_SSPSR               (RP2350B_SPI2_BASE + 0x00C)

/* ST25R3916 SPI protocol address byte encoding:
 *   Bit 7: 1 = Read, 0 = Write
 *   Bit 6: 1 = Burst (auto-increment), 0 = Single
 *   Bits 5:0: Register address or command */

/**
 * apex_nfc_cs_assert — Assert ST25R3916 chip select (active-low)
 */
void apex_nfc_cs_assert(void) {
    rp2350b_gpio_set(PIN_NFC_SPI_CSN, false);
    for (volatile int i = 0; i < 2; i++)
        __asm__("nop");
}

/**
 * apex_nfc_cs_release — Release ST25R3916 chip select
 */
void apex_nfc_cs_release(void) {
    rp2350b_gpio_set(PIN_NFC_SPI_CSN, true);
}

/**
 * apex_nfc_spi_xfer — Transfer one byte to/from ST25R3916 via SPI2
 *
 * @tx_byte: Byte to send
 * Returns: Byte received from ST25R3916
 */
uint8_t apex_nfc_spi_xfer(uint8_t tx_byte) {
    volatile uint32_t *dr = (volatile uint32_t *)SPI2_SSPDR;
    volatile uint32_t *sr = (volatile uint32_t *)SPI2_SSPSR;

    while (!(*sr & SSPSR_TNF))
        ;
    *dr = (uint32_t)tx_byte;
    while (!(*sr & SSPSR_RNE))
        ;
    return (uint8_t)(*dr & 0xFF);
}

/**
 * apex_nfc_write_register — Write a single byte to ST25R3916 via SPI2
 *
 * This is the platform-specific implementation used by st25r3916_init.c.
 * The ST25R3916 SPI protocol sends the address byte first, then the data byte.
 */
void apex_nfc_write_register(uint8_t addr, uint8_t val) {
    apex_nfc_cs_assert();
    /* Write single: bit7=0, bit6=0, addr[5:0] */
    apex_nfc_spi_xfer((uint8_t)(addr & 0x3F));
    apex_nfc_spi_xfer(val);
    apex_nfc_cs_release();
}

/**
 * apex_nfc_read_register — Read a single byte from ST25R3916 via SPI2
 */
uint8_t apex_nfc_read_register(uint8_t addr) {
    uint8_t val;
    apex_nfc_cs_assert();
    /* Read single: bit7=1, bit6=0, addr[5:0] */
    apex_nfc_spi_xfer((uint8_t)((addr & 0x3F) | 0x80));
    val = apex_nfc_spi_xfer(0x00);
    apex_nfc_cs_release();
    return val;
}