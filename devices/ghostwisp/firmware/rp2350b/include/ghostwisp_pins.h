/*
 * ghostwisp_pins.h — GhostWisp RP2350B Board Pin Definitions
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Pin assignments for the RP2350B primary MCU on the GhostWisp
 * board (Project Little Spectre, Rev A). GhostWisp is a standalone
 * device — there is no RK3576 host bridge. USB-C is the primary
 * companion link to GhostBlade.
 *
 * All pin numbers refer to RP2350B physical pin numbering.
 * The RP2350B has 48 GPIO pins (GPIO0–GPIO47).
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           devices/ghostwisp/docs/roadmap.md (Phase 1 — pin allocation)
 */

#ifndef GHOSTWISP_PINS_H
#define GHOSTWISP_PINS_H

/* ========================================================================
 * SPI0 — CC1101 Sub-GHz Radio
 * SPI Mode 0, up to 10 MHz
 * ======================================================================== */

#define PIN_CC_SPI_SCK     6    /* CC1101 SPI clock (SPI0 bus) */
#define PIN_CC_SPI_TX      7    /* CC1101 SPI data in (MOSI) */
#define PIN_CC_SPI_RX      8    /* CC1101 SPI data out (MISO) */
#define PIN_CC_SPI_CSN     9    /* CC1101 SPI chip select (active-low) */
#define PIN_CC_GDO0       10    /* CC1101 GDO0: FIFO threshold / sync detect */
#define PIN_CC_GDO2       11    /* CC1101 GDO2: packet received / TX done */

/* ========================================================================
 * SPI1 — ST25R3916 NFC Controller
 * SPI Mode 0, up to 10 MHz
 * ======================================================================== */

#define PIN_NFC_SPI_SCK   12    /* ST25R3916 SPI clock */
#define PIN_NFC_SPI_TX    13    /* ST25R3916 SPI MOSI */
#define PIN_NFC_SPI_RX    14    /* ST25R3916 SPI MISO */
#define PIN_NFC_SPI_CSN   15    /* ST25R3916 SPI chip select (active-low) */
#define PIN_NFC_IRQ       16    /* ST25R3916 interrupt (active-low) */

/* ========================================================================
 * SPI2 — microSD Card
 * SPI Mode 0, up to 20 MHz (default 10 MHz for init)
 * ======================================================================== */

#define PIN_SD_SPI_SCK    17    /* microSD SPI clock */
#define PIN_SD_SPI_TX     18    /* microSD SPI MOSI */
#define PIN_SD_SPI_RX     19    /* microSD SPI MISO */
#define PIN_SD_SPI_CSN    20    /* microSD SPI chip select (active-low) */
#define PIN_SD_CD         21    /* microSD card detect (active-low) */

/* ========================================================================
 * QSPI — External 16 MiB Flash
 * Dedicated QSPI interface (pins 22–27), not GPIO-muxed
 * ======================================================================== */

/* QSPI pins are fixed on RP2350B: SCLK, CSn, DQ0–DQ3 */
/* No GPIO pin defines needed — handled by QMI/DOCSIS hardware */

/* ========================================================================
 * Display — 2.4" 320x240 IPS (SPI3 / PIO-driven)
 * SPI Mode 0, up to 40 MHz
 * ======================================================================== */

#define PIN_DISP_SPI_SCK  22    /* Display SPI clock */
#define PIN_DISP_SPI_TX   23    /* Display SPI MOSI (data) */
#define PIN_DISP_SPI_CSN  24    /* Display chip select (active-low) */
#define PIN_DISP_DC       25    /* Display D/C signal (0=cmd, 1=data) */
#define PIN_DISP_RST      26    /* Display reset (active-low) */
#define PIN_DISP_BL       27    /* Display backlight enable (active-high) */

/* ========================================================================
 * User Input — Five-way navigation + two action buttons
 * All active-low with internal pull-ups
 * ======================================================================== */

#define PIN_BTN_UP        28    /* Five-way: up */
#define PIN_BTN_DOWN      29    /* Five-way: down */
#define PIN_BTN_LEFT      30    /* Five-way: left */
#define PIN_BTN_RIGHT     31    /* Five-way: right */
#define PIN_BTN_CENTER    32    /* Five-way: center (enter) */
#define PIN_BTN_A         33    /* Action button A */
#define PIN_BTN_B         34    /* Action button B */

/* ========================================================================
 * IR Transmitter / Receiver
 * ======================================================================== */

#define PIN_IR_TX          2    /* IR LED output (PWM via PIO or GPIO) */
#define PIN_IR_RX          3    /* IR receiver input (demodulated) */

/* ========================================================================
 * USB-C — Companion Link to GhostBlade
 * RP2350B USB peripheral (fixed pins, not GPIO-muxed)
 * ======================================================================== */

/* USB D+/D- are on fixed RP2350B pins. The following control signals
 * are GPIO-driven for explicit host/device power control. */
#define PIN_USB_VBUS_SENSE 4    /* VBUS detect (high when USB host attached) */
#define PIN_USB_HOST_EN    5    /* USB host mode enable (active-high) */

/* ========================================================================
 * I2C0 — Fuel Gauge / Battery Management
 * 400 kHz Fast Mode
 * ======================================================================== */

#define PIN_BATT_I2C_SDA   0    /* I2C0 SDA to fuel gauge (e.g., MAX17048) */
#define PIN_BATT_I2C_SCL   1    /* I2C0 SCL to fuel gauge */

/* ========================================================================
 * Expansion Header — 3.3V UART/SPI/I2C/GPIO
 * ======================================================================== */

#define PIN_EXP_UART_TX   36    /* Expansion header UART1 TX */
#define PIN_EXP_UART_RX   37    /* Expansion header UART1 RX */
#define PIN_EXP_GPIO0     35    /* Expansion header GPIO 0 */
#define PIN_EXP_GPIO1     38    /* Expansion header GPIO 1 */
#define PIN_EXP_GPIO2     39    /* Expansion header GPIO 2 */

/* ========================================================================
 * Feedback — RGB LED, Buzzer, Vibration Motor
 * ======================================================================== */

#define PIN_RGB_LED       40    /* WS2812-style addressable RGB (PIO-driven) */
#define PIN_BUZZER        41    /* Piezo buzzer (PWM) */
#define PIN_VIBRATION     42    /* Vibration motor enable (active-high) */

/* ========================================================================
 * Power Management
 * ======================================================================== */

#define PIN_POWER_SWITCH  43    /* Hard power switch sense (high = on) */
#define PIN_CHARGE_STAT   44    /* Charger status (active-low when charging) */
#define PIN_RADIO_DISABLE 45    /* Global radio hardware disable (active-high) */

/* ========================================================================
 * Debug — UART1 Console
 * 115200 8N1
 * ======================================================================== */

/* UART1 is routed to a dedicated expansion-header pair so the fuel-gauge
 * I2C0 bus remains available. Debug UART may be disabled in production. */
#define PIN_DEBUG_UART_TX PIN_EXP_UART_TX
#define PIN_DEBUG_UART_RX PIN_EXP_UART_RX

/* ========================================================================
 * Boot Configuration Straps
 * Sampled at reset to select boot mode
 * ======================================================================== */

#define PIN_BOOT_STRAP0   46    /* Boot strap 0: 0=normal, 1=forced recovery */
#define PIN_BOOT_STRAP1   47    /* Boot strap 1: A/B partition select */

#endif /* GHOSTWISP_PINS_H */