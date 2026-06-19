/*
 * rp2350b_init.c — Low-Level Initialization for RP2350B Coprocessor
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * This file implements the hardware initialization sequence for the
 * RP2350B coprocessor on the GhostBlade board. It runs from the RP2350B's
 * boot ROM → flash bootloader → this code.
 *
 * Sequence:
 *   1. PLL clock configuration (150 MHz system clock from 12 MHz XOSC)
 *   2. GPIO direction configuration for all bridge and RF control pins
 *   3. PIO block initialization for SDR antenna switching and CC1101 bit-bang
 *   4. SPI0 slave configuration (bridge to RK3576)
 *   5. SPI1 master configuration (control bus for LMS7002M and CC1101)
 *   6. SPI2 master configuration (control bus for ST25R3916)
 *   7. I2C0 configuration (ST25R3916 secondary control)
 *   8. ADC configuration (battery voltage, temperature)
 *   9. Interrupt controller (NVIC) configuration
 *  10. Enable SPI0 slave and enter main loop
 */

#include <stdint.h>
#include <stdbool.h>

/* Forward declaration: watchdog_kick is defined in watchdog.c */
extern void watchdog_kick(void);

/* ========================================================================
 * RP2350B Register Base Addresses
 * ======================================================================== */

#define RP2350B_SYSINFO_BASE      0x40000000UL
#define RP2350B_SYSCFG_BASE       0x40004000UL
#define RP2350B_CLOCKS_BASE       0x40008000UL
#define RP2350B_RESETS_BASE       0x4000C000UL
#define RP2350B_PADS_BANK0_BASE   0x4001C000UL
#define RP2350B_IO_BANK0_BASE     0x40018000UL
#define RP2350B_GPIO_BASE         0x400D0000UL  /* SIO */
#define RP2350B_SPI0_BASE         0x48060000UL  /* APB SPI0 (slave to RK3576) */
#define RP2350B_SPI1_BASE         0x48070000UL  /* APB SPI1 (master: SDR, CC1101) */
#define RP2350B_SPI2_BASE         0x48090000UL  /* APB SPI2 (master: NFC) */
#define RP2350B_I2C0_BASE         0x48040000UL
#define RP2350B_PIO0_BASE         0x50200000UL
#define RP2350B_PIO1_BASE         0x50300000UL
#define RP2350B_ADC_BASE          0x50041000UL
#define RP2350B_NVIC_BASE         0xE000E100UL
#define RP2350B_SCB_BASE          0xE000ED00UL

/* ========================================================================
 * Clocks Register Offsets
 * ======================================================================== */

#define CLOCKS_REF_CFG             0x00
#define CLOCKS_REF_DIV             0x04
#define CLOCKS_SYS_CFG             0x08
#define CLOCKS_SYS_DIV             0x0C
#define CLOCKS_PERI_CFG            0x10
#define CLOCKS_CLK_SYS_PLL_SYS     0x00  /* PLL source for SYS */
#define CLOCKS_CLK_SYS_PLL_USB     0x01  /* PLL source for USB */

/* ========================================================================
 * Resets Register Offsets
 * ======================================================================== */

#define RESETS_RESET                0x00
#define RESETS_DONE                 0x04

/* Reset bits */
#define RESETS_RESET_SPI0           BIT(16)
#define RESETS_RESET_SPI1           BIT(17)
#define RESETS_RESET_SPI2           BIT(18)
#define RESETS_RESET_I2C0           BIT(22)
#define RESETS_RESET_UART0          BIT(24)
#define RESETS_RESET_ADC            BIT(28)
#define RESETS_RESET_PIO0          BIT(32)
#define RESETS_RESET_PIO1          BIT(33)
#define RESETS_RESET_PADS_BANK0     BIT(42)

/* ========================================================================
 * GPIO Pin Assignments (matching the schematic netlist)
 * ======================================================================== */

/* SPI0 (slave) — Bridge to RK3576 */
#define PIN_SPI0_RX       16   /* SPI0 MISO (RP2350B transmits) */
#define PIN_SPI0_CSN      17   /* SPI0 chip select (from RK3576) */
#define PIN_SPI0_SCK      18   /* SPI0 clock (from RK3576) */
#define PIN_SPI0_TX       19   /* SPI0 MOSI (RP2350B receives) */

/* Interrupt and control pins */
#define PIN_INT_REQ       20   /* Interrupt request to RK3576 (active-low) */
#define PIN_HOST_RDY       21   /* Host ready from RK3576 (active-low) */
#define PIN_MCU_RUN       24   /* RUN pin (reset control, active-low) */

/* PE42422 antenna switch */
#define PIN_ANT_SEL0      2    /* Antenna select bit 0 */
#define PIN_ANT_SEL1      3    /* Antenna select bit 1 */

/* SDR control */
#define PIN_SDR_SPI_SCK   27   /* SPI1 SCK to LMS7002M */
#define PIN_SDR_SPI_TX    28   /* SPI1 MOSI to LMS7002M */
#define PIN_SDR_SPI_RX    29   /* SPI1 MISO from LMS7002M */
#define PIN_SDR_SPI_CSN   30   /* SPI1 CSn to LMS7002M */
#define PIN_SDR_RESET      31   /* LMS7002M reset (active-low) */
#define PIN_SDR_GPIO0      32   /* LMS7002M GPIO0 (TX enable) */
#define PIN_SDR_GPIO1      33   /* LMS7002M GPIO1 (RX enable) */
#define PIN_SDR_LNA_EN    34   /* LMS7002M LNA enable */

/* CC1101 sub-GHz */
#define PIN_CC_SPI_SCK    8    /* SPI1 SCK (shared with SDR via PIO) */
#define PIN_CC_SPI_TX     9    /* SPI1 MOSI to CC1101 */
#define PIN_CC_SPI_RX    12    /* SPI1 MISO from CC1101 */
#define PIN_CC_SPI_CSN   10    /* CC1101 CSn (GPIO, not SPI peripheral) */
#define PIN_CC_GDO0      13    /* CC1101 GDO0 interrupt */
#define PIN_CC_GDO2      14    /* CC1101 GDO2 interrupt */

/* ST25R3916 NFC */
#define PIN_NFC_SPI_SCK  40   /* SPI2 SCK to ST25R3916 */
#define PIN_NFC_SPI_TX   41   /* SPI2 MOSI to ST25R3916 */
#define PIN_NFC_SPI_RX   42   /* SPI2 MISO from ST25R3916 */
#define PIN_NFC_SPI_CSN  43   /* SPI2 CSn to ST25R3916 */
#define PIN_NFC_IRQ      44   /* ST25R3916 IRQ (active-low) */

/* I2C (secondary NFC control) */
#define PIN_I2C_SDA      46   /* I2C0 SDA */
#define PIN_I2C_SCL      47   /* I2C0 SCL */

/* ADC inputs */
#define PIN_ADC_VBAT      0   /* ADC0: Battery voltage (via voltage divider) */
#define PIN_ADC_TEMP       4   /* ADC4: Internal die temperature */

/* UART0 debug */
#define PIN_UART_TX       0   /* UART0 TX (debug console) */
#define PIN_UART_RX       1   /* UART0 RX (debug console) */

/* ========================================================================
 * Helper macros
 * ======================================================================== */

#define BIT(n)               (1UL << (n))
#define REG32(addr)           (*(volatile uint32_t *)(addr))
#define REG16(addr)           (*(volatile uint16_t *)(addr))
#define REG8(addr)            (*(volatile uint8_t *)(addr))

#define MIN(a, b)             ((a) < (b) ? (a) : (b))
#define MAX(a, b)             ((a) > (b) ? (a) : (b))

/* ========================================================================
 * CRC-64 and CRC-32 computation (for SPI frame integrity)
 * ======================================================================== */

/* CRC-64 using polynomial 0x42F0E1EBA9EA3693 (ECMA-182)
 * Runtime-initialized to avoid wasting SRAM on a 2 KB table that
 * would be all zeros if left as a const array. */
static uint64_t crc64_table[256];
static int crc64_initialized = 0;

static void crc64_init(void) {
    const uint64_t poly = 0x42F0E1EBA9EA3693ULL;
    for (int i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc64_table[i] = crc;
    }
    crc64_initialized = 1;
    /* Ensure table is visible to all contexts (ISR may use CRC) */
    __asm__ volatile ("dmb" ::: "memory");
}

static uint64_t crc64_compute(const uint8_t *data, uint32_t len) {
    if (!crc64_initialized)
        crc64_init();
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc64_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

/* CRC-32 using polynomial 0xEDB88320 (ISO 3309) */
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void crc32_init(void) {
    const uint32_t poly = 0xEDB88320UL;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
    __asm__ volatile ("dmb" ::: "memory");
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    if (!crc32_initialized)
        crc32_init();
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * System Clock Configuration
 * ======================================================================== */

/*
 * RP2350B Clock Tree:
 *   12 MHz XOSC → PLL_SYS (multiply to 1500 MHz) → DIV (÷10) → 150 MHz SYS_CLK
 *   12 MHz XOSC → PLL_USB (multiply to 480 MHz)  → DIV (÷1)  → 48 MHz USB_CLK
 *
 * The system clock runs at 150 MHz for both ARM Cortex-M33 cores.
 * Peripheral clock (CLK_PERI) is derived from SYS_CLK at 150 MHz.
 */

#define XOSC_FREQ_HZ            12000000UL   /* 12 MHz crystal */
#define SYS_CLK_TARGET_HZ       150000000UL  /* 150 MHz system clock */
#define PLL_SYS_VCO_FREQ_HZ     1500000000UL /* 1500 MHz VCO */
#define PLL_SYS_REFDIV          1
#define PLL_SYS_FBDIV           125          /* VCO = 12 MHz * 125 = 1500 MHz */
#define PLL_SYS_POSTDIV1        5            /* 1500 / 5 = 300 MHz */
#define PLL_SYS_POSTDIV2        2            /* 300 / 2 = 150 MHz */

static void clocks_init(void) {
    /* Step 1: Start XOSC (12 MHz) */
    REG32(RP2350B_CLOCKS_BASE + 0x00) = 0;  /* Disable ring oscillator */
    /* Wait for XOSC stable — approximately 1 ms */
    for (volatile int i = 0; i < 1500; i++)
        __asm__("nop");

    /* Step 2: Configure PLL_SYS */
    /* Reset PLL_SYS */
    REG32(RP2350B_RESETS_BASE + RESETS_RESET) |= BIT(14);  /* PLL_SYS reset */
    REG32(RP2350B_RESETS_BASE + RESETS_RESET) &= ~BIT(14);

    /* Wait for PLL_SYS reset to deassert */
    while (!(REG32(RP2350B_RESETS_BASE + RESETS_DONE) & BIT(14)))
        ;

    /* Configure PLL_SYS: REFDIV=1, FBDIV=125, POSTDIV1=5, POSTDIV2=2 */
    /* VCO = 12 MHz * 125 / 1 = 1500 MHz */
    /* Output = 1500 / 5 / 2 = 150 MHz */
    REG32(RP2350B_CLOCKS_BASE + 0x40) = PLL_SYS_REFDIV;         /* REFDIV */
    REG32(RP2350B_CLOCKS_BASE + 0x44) = PLL_SYS_FBDIV << 0;       /* FBDIV */
    REG32(RP2350B_CLOCKS_BASE + 0x48) = (PLL_SYS_POSTDIV1 << 0) | /* POSTDIV1 */
                                          (PLL_SYS_POSTDIV2 << 4);    /* POSTDIV2 */

    /* Power up PLL_SYS VCO */
    REG32(RP2350B_CLOCKS_BASE + 0x44) &= ~(1U << 31);  /* Clear PD (power down) */

    /* Wait for PLL_SYS lock (VCO stable) */
    while (!(REG32(RP2350B_CLOCKS_BASE + 0x50) & (1U << 31)))
        ;

    /* Step 3: Switch SYS_CLK to PLL_SYS output */
    /* CLK_SYS_CTRL: SRC = 0 (CLK_REF), then switch to PLL_SYS */
    REG32(RP2350B_CLOCKS_BASE + 0x08) = (0 << 0);  /* Source: CLK_REF initially */
    REG32(RP2350B_CLOCKS_BASE + 0x08) = (1 << 0);  /* Source: PLL_SYS */

    /* CLK_SYS_DIV: divide by 1 (150 MHz) */
    REG32(RP2350B_CLOCKS_BASE + 0x0C) = (1 << 0);   /* Integer divide = 1 */

    /* Step 4: Configure CLK_PERI to run at SYS_CLK (150 MHz) */
    REG32(RP2350B_CLOCKS_BASE + 0x10) = (0 << 0);   /* Source: SYS_CLK */

    /* Step 5: Release peripheral resets */
    uint32_t peripheral_resets = RESETS_RESET_SPI0  |
                                  RESETS_RESET_SPI1  |
                                  RESETS_RESET_SPI2  |
                                  RESETS_RESET_I2C0  |
                                  RESETS_RESET_ADC   |
                                  RESETS_RESET_PIO0  |
                                  RESETS_RESET_PIO1  |
                                  RESETS_RESET_PADS_BANK0;

    /* Deassert resets */
    REG32(RP2350B_RESETS_BASE + RESETS_RESET) &= ~peripheral_resets;

    /* Wait for all resets to complete */
    while ((REG32(RP2350B_RESETS_BASE + RESETS_DONE) & peripheral_resets) !=
           peripheral_resets)
        ;
}

/* ========================================================================
 * GPIO Configuration
 * ======================================================================== */

static void gpio_init(void) {
    /* Configure GPIO pin function (0 = SPI, 1 = SIO, 2 = PIO, 3 = GPCK, etc.)
     * Each pin has a 4-bit function select in IO_BANK0.
     */

    /* SPI0 slave pins (function 1 = SPI) */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SPI0_RX + 0x00) = (1 << 0);   /* SPI0 RX */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SPI0_CSN + 0x00) = (1 << 0);  /* SPI0 CSn */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SPI0_SCK + 0x00) = (1 << 0);  /* SPI0 SCK */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SPI0_TX + 0x00) = (1 << 0);   /* SPI0 TX */

    /* NFC SPI2 pins (function 1 = SPI) */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_NFC_SPI_SCK + 0x00) = (1 << 0);  /* SPI2 SCK */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_NFC_SPI_TX  + 0x00) = (1 << 0);  /* SPI2 TX  */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_NFC_SPI_RX  + 0x00) = (1 << 0);  /* SPI2 RX  */

    /* Output pins (function 1 = SIO, controlled by SIO_GPIO registers) */
    /* Antenna switch */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_ANT_SEL0 + 0x00) = (5 << 0);   /* SIO */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_ANT_SEL1 + 0x00) = (5 << 0);   /* SIO */

    /* Interrupt and control */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_INT_REQ + 0x00) = (5 << 0);    /* SIO output */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_HOST_RDY + 0x00) = (5 << 0);   /* SIO input */

    /* SDR control */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SDR_RESET + 0x00) = (5 << 0);  /* SIO output */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SDR_GPIO0 + 0x00) = (5 << 0);  /* SIO output */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SDR_GPIO1 + 0x00) = (5 << 0);  /* SIO output */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_SDR_LNA_EN + 0x00) = (5 << 0); /* SIO output */

    /* CC1101 */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_CC_SPI_CSN + 0x00) = (5 << 0); /* SIO output (GPIO CS) */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_CC_GDO0 + 0x00) = (5 << 0);   /* SIO input */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_CC_GDO2 + 0x00) = (5 << 0);   /* SIO input */

    /* NFC */
    REG32(RP2350B_IO_BANK0_BASE + 0x04 * PIN_NFC_IRQ + 0x00) = (5 << 0);   /* SIO input */

    /* Set GPIO direction: 1 = output, 0 = input */
    volatile uint32_t *gpio_oe = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x10);
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);

    /* Set all output pins */
    *gpio_oe = (1UL << PIN_ANT_SEL0)  |
               (1UL << PIN_ANT_SEL1)  |
               (1UL << PIN_INT_REQ)   |
               (1UL << PIN_SDR_RESET) |
               (1UL << PIN_SDR_GPIO0) |
               (1UL << PIN_SDR_GPIO1) |
               (1UL << PIN_SDR_LNA_EN) |
               (1UL << PIN_CC_SPI_CSN) |
               (1UL << PIN_NFC_SPI_CSN);

    /* Set initial output states */
    uint32_t init_val = (1UL << PIN_INT_REQ)    |  /* IRQ deasserted (HIGH) */
                        (1UL << PIN_SDR_RESET)   |  /* SDR out of reset (HIGH) */
                        (1UL << PIN_CC_SPI_CSN)  |  /* CC1101 CSn deasserted (HIGH) */
                        (1UL << PIN_NFC_SPI_CSN) |  /* NFC CSn deasserted (HIGH) */
                        (0UL << PIN_ANT_SEL0)    |  /* Antenna select: MIMO TX */
                        (0UL << PIN_ANT_SEL1)    |  /* Antenna select: MIMO TX */
                        (0UL << PIN_SDR_GPIO0)  |  /* SDR TX disabled */
                        (0UL << PIN_SDR_GPIO1)  |  /* SDR RX disabled */
                        (0UL << PIN_SDR_LNA_EN);    /* SDR LNA disabled */
    *gpio_out = init_val;
}

/* ========================================================================
 * SPI0 Slave Configuration (Bridge to RK3576)
 * ======================================================================== */

/* SPI0 peripheral registers (offsets from SPI0_BASE) */
#define SPI_SSPCR0               0x000  /* Control register 0 */
#define SPI_SSPCR1               0x004  /* Control register 1 */
#define SPI_SSPDR                0x008  /* Data register */
#define SPI_SSPSR                0x00C  /* Status register */
#define SPI_SSPCPSR              0x010  /* Clock prescale */
#define SPI_SSPIMSC              0x014  /* Interrupt mask */
#define SPI_SSPRIS               0x018  /* Raw interrupt status */
#define SPI_SSPMIS               0x01C  /* Masked interrupt status */
#define SPI_SSPICR               0x020  /* Interrupt clear */
#define SPI_SSPDMACR             0x024  /* DMA control */

/* SSPCR0 bits */
#define SPI_SSPCR0_DSS_8BIT      (0x7 << 0)   /* 8-bit data size */
#define SPI_SSPCR0_FRF_SPI       (0x0 << 4)   /* SPI frame format */
#define SPI_SSPCR0_CPOL_0        (0x0 << 6)   /* CPOL=0 */
#define SPI_SSPCR0_CPHA_0        (0x0 << 7)   /* CPHA=0 */
#define SPI_SSPCR0_SCR_SHIFT     8            /* Serial clock rate divisor */
#define SPI_SSPCR0_SCR_MASK      0xFF00

/* SSPCR1 bits */
#define SPI_SSPCR1_LBM           BIT(0)  /* Loopback */
#define SPI_SSPCR1_SSE           BIT(1)  /* SPI enable */
#define SPI_SSPCR1_MS            BIT(2)  /* Master/slave (0=master, 1=slave) */
#define SPI_SSPCR1_SOD           BIT(3)  /* Slave output disable */

/* SSPSR bits */
#define SPI_SSPSR_TFE            BIT(0)  /* TX FIFO empty */
#define SPI_SSPSR_TNF            BIT(1)  /* TX FIFO not full */
#define SPI_SSPSR_RNE            BIT(2)  /* RX FIFO not empty */
#define SPI_SSPSR_RFF            BIT(3)  /* RX FIFO full */
#define SPI_SSPSR_BSY            BIT(4)  /* SPI busy */

static void spi0_slave_init(void) {
    volatile uint32_t *spi = (volatile uint32_t *)RP2350B_SPI0_BASE;

    /* Disable SPI0 first */
    spi[SPI_SSPCR1 / 4] = 0;

    /* Clear any pending data in FIFOs */
    while (spi[SPI_SSPSR / 4] & SPI_SSPSR_RNE) {
        (void)spi[SPI_SSPDR / 4];  /* Read and discard */
    }

    /* Configure SPI0 as slave, Mode 0 (CPOL=0, CPHA=0), 8-bit data */
    spi[SPI_SSPCR0 / 4] = SPI_SSPCR0_DSS_8BIT |
                           SPI_SSPCR0_FRF_SPI   |
                           SPI_SSPCR0_CPOL_0    |
                           SPI_SSPCR0_CPHA_0;

    /* Clock prescale divisor (slave mode — not used for clock gen, but must be set) */
    spi[SPI_SSPCPSR / 4] = 2;  /* Minimum divisor */

    /* Enable SPI0 in slave mode */
    spi[SPI_SSPCR1 / 4] = SPI_SSPCR1_MS |     /* Slave mode */
                           SPI_SSPCR1_SSE;      /* Enable */

    /* Enable receive interrupt */
    spi[SPI_SSPIMSC / 4] = BIT(2);  /* RX half-full or higher priority */
}

/* ========================================================================
 * SPI1 Master Configuration (SDR + CC1101 control bus)
 * ======================================================================== */

static void spi1_master_init(void) {
    volatile uint32_t *spi = (volatile uint32_t *)RP2350B_SPI1_BASE;

    /* Disable SPI1 first */
    spi[SPI_SSPCR1 / 4] = 0;

    /* Clear FIFOs */
    while (spi[SPI_SSPSR / 4] & SPI_SSPSR_RNE) {
        (void)spi[SPI_SSPDR / 4];
    }

    /* Configure SPI1 as master, Mode 0, 8-bit data
     * Clock = SYS_CLK / (CPSDVR × (SCR+1))
     * For 10 MHz: 150 MHz / (2 × 8) = 9.375 MHz ≈ 10 MHz */
    spi[SPI_SSPCR0 / 4] = SPI_SSPCR0_DSS_8BIT |
                           SPI_SSPCR0_FRF_SPI   |
                           SPI_SSPCR0_CPOL_0    |
                           SPI_SSPCR0_CPHA_0    |
                           (7 << SPI_SSPCR0_SCR_SHIFT);  /* SCR = 7 */

    spi[SPI_SSPCPSR / 4] = 2;  /* CPSDVR = 2 */

    /* Enable SPI1 in master mode */
    spi[SPI_SSPCR1 / 4] = SPI_SSPCR1_SSE;  /* Master mode (MS bit = 0), enable */
}

/* ========================================================================
 * SPI2 Master Configuration (NFC ST25R3916)
 * ======================================================================== */

static void spi2_master_init(void) {
    volatile uint32_t *spi = (volatile uint32_t *)RP2350B_SPI2_BASE;

    /* Disable SPI2 first */
    spi[SPI_SSPCR1 / 4] = 0;

    /* Clear FIFOs */
    while (spi[SPI_SSPSR / 4] & SPI_SSPSR_RNE) {
        (void)spi[SPI_SSPDR / 4];
    }

    /* Configure SPI2 as master, Mode 0, 8-bit data
     * For 10 MHz */
    spi[SPI_SSPCR0 / 4] = SPI_SSPCR0_DSS_8BIT |
                           SPI_SSPCR0_FRF_SPI   |
                           SPI_SSPCR0_CPOL_0    |
                           SPI_SSPCR0_CPHA_0    |
                           (7 << SPI_SSPCR0_SCR_SHIFT);

    spi[SPI_SSPCPSR / 4] = 2;

    /* Enable SPI2 in master mode */
    spi[SPI_SSPCR1 / 4] = SPI_SSPCR1_SSE;
}

/* ========================================================================
 * PIO Block Initialization
 * ======================================================================== */

/*
 * PIO0: SDR Antenna Switching (PE42422 control)
 *   - State Machine 0: Fast antenna switching with <1 μs response time
 *   - Accepts antenna ID via TX FIFO, drives PE42422 V1/V2 pins
 *
 * PIO1: CC1101 Bit-Bang (for proprietary OOK/FSK modes not supported by SPI)
 *   - State Machine 0: Transmit bit-bang on GDO0
 *   - State Machine 1: Receive bit-bang from GDO2
 *
 * The PIO programs are loaded from the flash boot stage; we configure the
 * state machines here and point them at the loaded instruction memory.
 */

/* PIO0 State Machine 0: Antenna Switch Program
 *
 * This program reads an antenna ID (0-3) from the TX FIFO and drives
 * PE42422 V1/V2 pins (GPIO 2 and 3) accordingly.
 *
 * .origin 0
 * .wrap_target
 *   out pins, 2        ; Drive 2 bits to V1/V2 (pins 2,3)
 *   nop                 ; 1 cycle hold time for switch settling
 * .wrap
 *
 * Total: 2 instructions, 2 cycles per switch
 */

static const uint16_t pio0_ant_switch_prog[] = {
    0x6002,  /* out pins, 2 */
    0xA042,  /* nop (with side-set for timing) */
};

static void pio0_ant_switch_init(void) {
    volatile uint32_t *pio = (volatile uint32_t *)RP2350B_PIO0_BASE;

    /* Load program into PIO0 instruction memory */
    for (int i = 0; i < 2; i++) {
        pio[0x20 / 4 + i] = pio0_ant_switch_prog[i];  /* PIO_INSTR_MEM */
    }

    /* Configure State Machine 0 */
    /* CLKDIV: 150 MHz / 1 = 150 MHz (fast switching) */
    pio[0xC8 / 4] = (1 << 16);  /* SM0 CLKDIV: fraction=0, integer=1 */

    /* EXECCTRL: wrap at instruction 1, wrap target at instruction 0 */
    pio[0xCC / 4] = (1U << 28);    /* wrap_top = 1, wrap_bottom = 0 */

    /* SHIFTCTRL: auto-fill from TX FIFO, shift right */
    pio[0xD0 / 4] = (1 << 23) |   /* AUTO_PUSH disabled */
                     (1 << 25) |   /* AUTO_PULL enabled */
                     (2 << 29);    /* PULL_THRESH = 2 bits */

    /* PINCTRL: OUT pins 2-3 (2 pins), SET pins 2-3 (2 pins) */
    pio[0xD4 / 4] = (2 << 0)  |   /* OUT_COUNT = 2 */
                     (2 << 5)  |   /* SET_COUNT = 2 */
                     (2 << 20) |   /* OUT_BASE = 2 (pins 2,3) */
                     (2 << 26);    /* SET_BASE = 2 (pins 2,3) */

    /* Enable SM0 */
    pio[0x10 / 4] |= (1 << 0);   /* CTRL: SM0_ENABLE */
}

/* PIO1 State Machine 0: CC1101 OOK/FSK Bit-Bang Transmit
 *
 * This program sends raw bit patterns to GDO0 at configurable baud rate.
 * It reads 32-bit words from the TX FIFO, shifts them out one bit at a time.
 *
 * .origin 0
 * .wrap_target
 *   out x, 1            ; Get 1 bit from TX shift register into X
 *   mov pins, x          ; Drive bit to GDO0 output
 * .wrap
 */

static const uint16_t pio1_cc_tx_prog[] = {
    0x6021,  /* out x, 1 */
    0xA026,  /* mov pins, x */
};

static void pio1_cc_tx_init(void) {
    volatile uint32_t *pio = (volatile uint32_t *)RP2350B_PIO1_BASE;

    /* Load program */
    for (int i = 0; i < 2; i++) {
        pio[0x20 / 4 + i] = pio1_cc_tx_prog[i];
    }

    /* Configure SM0: 1 MHz clock for sub-GHz OOK */
    /* CLKDIV: 150 MHz / 150 = 1 MHz */
    pio[0xC8 / 4] = 150;  /* fraction=0, integer=150 */

    pio[0xCC / 4] = (1U << 28);  /* wrap_top=1, wrap_bottom=0 */
    pio[0xD0 / 4] = (1 << 25) | (1 << 29);  /* AUTO_PULL, PULL_THRESH=1 */
    pio[0xD4 / 4] = (1 << 0) | (1 << 5) | (13 << 20) | (13 << 26);
    /* OUT_BASE=13 (PIN_CC_GDO0=13), SET_BASE=13 */

    /* SM0 stays disabled until CC1101 TX is requested */
}

/* ========================================================================
 * ADC Configuration (Battery Voltage, Temperature)
 * ======================================================================== */

static void adc_init(void) {
    volatile uint32_t *adc = (volatile uint32_t *)RP2350B_ADC_BASE;

    /* Power on ADC */
    adc[0x00 / 4] &= ~(1U << 0);  /* Clear CS_EN (enable) */

    /* Configure FIFO: threshold = 1, no DMA, no shift (right-aligned 12-bit) */
    adc[0x14 / 4] = (1 << 24);  /* FCS_THRESH = 1 */

    /* Enable temperature sensor (channel 4) and select ADC channel 0 for
     * battery voltage. Both are configured in the same register (CS), so
     * we preserve the TEMP_EN bit when setting INSEL. */
    adc[0x04 >> 2] |= (1U << 4);  /* CS_TEMP_EN */
    adc[0x04 >> 2] = (adc[0x04 >> 2] & ~0x7U);  /* INSEL = ADC0 (VBAT) */
}

/* ========================================================================
 * NVIC (Nested Vectored Interrupt Controller) Configuration
 * ======================================================================== */

static void nvic_init(void) {
    volatile uint32_t *nvic = (volatile uint32_t *)RP2350B_NVIC_BASE;

    /* Enable SPI0 slave interrupt (IRQ 18 on RP2350B) */
    nvic[0x60 / 4 + (18 / 32)] |= (1UL << (18 % 32));  /* ISER: enable IRQ 18 */

    /* Enable GPIO interrupts for:
     * - PIN_HOST_RDY (GPIO 21): host ready signal
     * - PIN_CC_GDO0 (GPIO 13): CC1101 FIFO interrupt
     * - PIN_CC_GDO2 (GPIO 14): CC1101 packet done
     * - PIN_NFC_IRQ (GPIO 44): ST25R3916 interrupt
     */

    /* Configure GPIO interrupt for HOST_RDY (falling edge, active-low) */
    /* IO_BANK0: PROC0_INTE0 for GPIO 21 */
    volatile uint32_t *io_inte = (volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0xF0);
    io_inte[21 / 8] |= (1UL << ((21 % 8) * 4));  /* Falling edge detect */

    /* Enable GPIO IRQ in NVIC (shared GPIO IRQ, typically IRQ 13) */
    nvic[0x60 / 4 + (13 / 32)] |= (1UL << (13 % 32));

    /* Set interrupt priorities */
    volatile uint32_t *nvic_ipr = (volatile uint32_t *)(RP2350B_NVIC_BASE + 0x300);
    nvic_ipr[18 / 4] = (0 << 4);   /* SPI0: priority 0 (highest) */
    nvic_ipr[13 / 4] = (1 << 4);   /* GPIO: priority 1 (high) */
}

/* ========================================================================
 * Main Initialization Sequence
 * ======================================================================== */

void rp2350b_init(void) {
    /* Step 1: Configure system clocks (150 MHz SYS_CLK) */
    clocks_init();

    /* Watchdog may already be enabled from bootloader or early init.
     * Feed it periodically during this lengthy initialization to
     * prevent a reset before the main loop starts kicking it. */
    watchdog_kick();

    /* Step 2: Configure GPIO directions and initial states */
    gpio_init();

    /* Step 3: Initialize CRC tables */
    crc64_init();
    crc32_init();
    watchdog_kick();  /* CRC init is computationally heavy */

    /* Step 4: Initialize ADC (battery voltage, temperature) */
    adc_init();

    /* Step 5: Initialize SPI1 master (SDR + CC1101 control) */
    spi1_master_init();
    watchdog_kick();

    /* Step 6: Initialize SPI2 master (NFC ST25R3916) */
    spi2_master_init();

    /* Step 7: Initialize PIO blocks */
    pio0_ant_switch_init();   /* Antenna switching state machine */
    pio1_cc_tx_init();        /* CC1101 bit-bang TX (disabled until needed) */
    watchdog_kick();

    /* Step 8: Initialize SPI0 slave LAST (after all other peripherals are ready) */
    spi0_slave_init();
    /* Step 9: Configure NVIC interrupts */
    nvic_init();

    /* Step 10: MCU signals readiness by keeping INT_REQ deasserted (HIGH).
     * This was already done in gpio_init(). No additional action needed. */

    /* Final watchdog kick before entering main loop */
    watchdog_kick();

    /* System is now ready for SPI communication with RK3576 */
}

/* ========================================================================
 * SPI0 Slave Interrupt Handler (Bridge RX from RK3576)
 * ======================================================================== */

/* RX ring buffer for incoming SPI frames from RK3576 */
#define SPI_RX_BUF_SIZE  8192
static uint8_t spi_rx_buf[SPI_RX_BUF_SIZE];
static volatile uint32_t spi_rx_head = 0;
static volatile uint32_t spi_rx_tail = 0;
static volatile uint32_t spi_rx_overrun = 0;  /* Overflow counter for diagnostics */

/* TX response buffer */
#define SPI_TX_BUF_SIZE  4096
static uint8_t spi_tx_buf[SPI_TX_BUF_SIZE];
static volatile uint32_t spi_tx_len = 0;

void spi0_handler(void) {
    volatile uint32_t *spi = (volatile uint32_t *)RP2350B_SPI0_BASE;

    /* Read all available data from SPI0 RX FIFO */
    while (spi[SPI_SSPSR / 4] & SPI_SSPSR_RNE) {
        uint8_t byte = (uint8_t)(spi[SPI_SSPDR / 4] & 0xFF);

        /* Store in ring buffer */
        uint32_t next_head = (spi_rx_head + 1) % SPI_RX_BUF_SIZE;
        if (next_head != spi_rx_tail) {
            spi_rx_buf[spi_rx_head] = byte;
            /*
             * Ensure data write completes before updating head index,
             * so the consumer (main loop) sees the data before
             * seeing the advanced head. Use DMB for correctness on
             * ARM Cortex-M33 with data cache.
             */
            __asm__ volatile ("dmb" ::: "memory");
            spi_rx_head = next_head;
        } else {
            /* Buffer overflow — data is lost. Increment diagnostic
             * counter. The host should not send data faster than
             * the MCU can process it. INT_REQ flow control should
             * prevent this under normal operation. */
            spi_rx_overrun++;
        }
    }

    /* Clear interrupt */
    spi[SPI_SSPICR / 4] = spi[SPI_SSPRIS / 4];  /* Clear all pending interrupts */
}

/* ========================================================================
 * Antenna Switching (via PIO0)
 * ======================================================================== */

void apex_antenna_select(uint8_t ant_id) {
    /* ant_id: 0=MIMO_TX, 1=MIMO_RX, 2=SUBGHZ, 3=TERMINATED */
    volatile uint32_t *pio = (volatile uint32_t *)RP2350B_PIO0_BASE;

    /* Write antenna ID to PIO0 SM0 TX FIFO
     * Only 2 bits are needed (V1, V0 of PE42422) */
    pio[0x10 / 4] = ant_id & 0x03;  /* PIO0_TXF0 */
}

/* ========================================================================
 * SDR Control Functions
 * ======================================================================== */

void apex_sdr_reset_assert(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out &= ~(1UL << PIN_SDR_RESET);  /* Drive LOW (assert reset) */
}

void apex_sdr_reset_release(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out |= (1UL << PIN_SDR_RESET);   /* Drive HIGH (release reset) */
}

void apex_sdr_tx_enable(bool enable) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    if (enable)
        *gpio_out |= (1UL << PIN_SDR_GPIO0);   /* TX enable HIGH */
    else
        *gpio_out &= ~(1UL << PIN_SDR_GPIO0);  /* TX enable LOW */
}

void apex_sdr_rx_enable(bool enable) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    if (enable)
        *gpio_out |= (1UL << PIN_SDR_GPIO1);   /* RX enable HIGH */
    else
        *gpio_out &= ~(1UL << PIN_SDR_GPIO1);  /* RX enable LOW */
}

void apex_sdr_lna_enable(bool enable) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    if (enable)
        *gpio_out |= (1UL << PIN_SDR_LNA_EN);   /* LNA enable HIGH */
    else
        *gpio_out &= ~(1UL << PIN_SDR_LNA_EN);  /* LNA enable LOW */
}

/* ========================================================================
 * CC1101 SPI Transaction
 * ======================================================================== */

void apex_cc1101_cs_assert(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out &= ~(1UL << PIN_CC_SPI_CSN);  /* Drive LOW (assert CS) */
}

void apex_cc1101_cs_release(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out |= (1UL << PIN_CC_SPI_CSN);   /* Drive HIGH (release CS) */
}

uint8_t apex_cc1101_spi_xfer(uint8_t tx_byte) {
    volatile uint32_t *spi = (volatile uint32_t *)RP2350B_SPI1_BASE;

    /* Wait until TX FIFO has space */
    while (!(spi[SPI_SSPSR / 4] & SPI_SSPSR_TNF))
        ;

    /* Write byte to TX FIFO */
    spi[SPI_SSPDR / 4] = tx_byte;

    /* Wait until RX FIFO has data */
    while (!(spi[SPI_SSPSR / 4] & SPI_SSPSR_RNE))
        ;

    /* Read byte from RX FIFO */
    return (uint8_t)(spi[SPI_SSPDR / 4] & 0xFF);
}

void apex_cc1101_write_burst(uint8_t addr, const uint8_t *data, uint8_t len) {
    apex_cc1101_cs_assert();

    /* Send header: address with write bit (bit 7 = 0 for write, bit 6 = 1 for burst) */
    apex_cc1101_spi_xfer(addr | 0x40);

    /* Send data bytes */
    for (uint8_t i = 0; i < len; i++) {
        apex_cc1101_spi_xfer(data[i]);
    }

    apex_cc1101_cs_release();
}

void apex_cc1101_read_burst(uint8_t addr, uint8_t *data, uint8_t len) {
    apex_cc1101_cs_assert();

    /* Send header: address with read bit (bit 7 = 1 for read, bit 6 = 1 for burst) */
    apex_cc1101_spi_xfer(addr | 0xC0);

    /* Read data bytes (send dummy 0x00 while reading) */
    for (uint8_t i = 0; i < len; i++) {
        data[i] = apex_cc1101_spi_xfer(0x00);
    }

    apex_cc1101_cs_release();
}

/* ========================================================================
 * NFC ST25R3916 SPI Transaction
 * ======================================================================== */

void apex_nfc_cs_assert(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out &= ~(1UL << PIN_NFC_SPI_CSN);
}

void apex_nfc_cs_release(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out |= (1UL << PIN_NFC_SPI_CSN);
}

uint8_t apex_nfc_spi_xfer(uint8_t tx_byte) {
    volatile uint32_t *spi = (volatile uint32_t *)RP2350B_SPI2_BASE;

    while (!(spi[SPI_SSPSR / 4] & SPI_SSPSR_TNF))
        ;
    spi[SPI_SSPDR / 4] = tx_byte;

    while (!(spi[SPI_SSPSR / 4] & SPI_SSPSR_RNE))
        ;
    return (uint8_t)(spi[SPI_SSPDR / 4] & 0xFF);
}

void apex_nfc_write_register(uint8_t addr, uint8_t val) {
    apex_nfc_cs_assert();
    /* ST25R3916 SPI: bit7=0 (write), bit6:0=address */
    apex_nfc_spi_xfer(addr & 0x7F);
    apex_nfc_spi_xfer(val);
    apex_nfc_cs_release();
}

uint8_t apex_nfc_read_register(uint8_t addr) {
    apex_nfc_cs_assert();
    /* ST25R3916 SPI: bit7=1 (read), bit6:0=address */
    apex_nfc_spi_xfer(addr | 0x80);
    uint8_t val = apex_nfc_spi_xfer(0x00);
    apex_nfc_cs_release();
    return val;
}