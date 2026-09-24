/*
 * GhostWisp Rev A RP2350B board definition
 *
 * Author: jayis1
 * SPDX-License-Identifier: MIT
 *
 * This header is also consumed by the assembler; keep it preprocessor-only.
 */

#ifndef _BOARDS_GHOSTWISP_H
#define _BOARDS_GHOSTWISP_H

// pico_cmake_set PICO_PLATFORM=rp2350

// For board detection
#define GHOSTWISP
#define GHOSTWISP_REV_A 1

/* RP2350B exposes GPIO0 through GPIO47. */
#define PICO_RP2350A 0

/* Expansion-header debug console. */
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 1
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 36
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 37
#endif
#ifndef PICO_DEFAULT_UART_BAUD_RATE
#define PICO_DEFAULT_UART_BAUD_RATE 115200
#endif

/* Rev A target flash: 16 MiB W25Q128-compatible QSPI. */
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (16 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
