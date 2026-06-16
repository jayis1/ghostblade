/*
 * st25r3916_init.h — ST25R3916 NFC Controller Initialization and Control API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Provides initialization, polling, and field control for the ST25R3916
 * NFC reader/writer on the GhostBlade board. The ST25R3916 is connected
 * via SPI2 and I2C1, supporting ISO 14443A/B and ISO 15693 protocols.
 *
 * Default configuration: 13.56 MHz carrier, ISO 14443A polling at
 * 106 kbps with anti-collision support.
 */

#ifndef ST25R3916_INIT_H
#define ST25R3916_INIT_H

#include <stdint.h>
#include <stdbool.h>

/* ── ST25R3916 register addresses ───────────────────────────────────────── */

#define ST25R3916_REG_IC_IDENTITY       0x00    /**< IC identity register */
#define ST25R3916_REG_IC_VERSION        0x01    /**< IC version register */
#define ST25R3916_REG_IO_CONF1         0x02    /**< I/O configuration 1 */
#define ST25R3916_REG_IO_CONF2         0x03    /**< I/O configuration 2 */
#define ST25R3916_REG_OP_CONTROL       0x04    /**< Operation control */
#define ST25R3916_REG_MODE_DISPLAY      0x05    /**< Mode display */
#define ST25R3916_REG_MODE_AND_BT       0x06    /**< Mode and bit rate */
#define ST25R3916_REG_BITRATE           0x07    /**< Bit rate configuration */
#define ST25R3916_REG_ISO14443A_NFC     0x08    /**< ISO14443A/NFC settings */
#define ST25R3916_REG_TX_DRIVER         0x09    /**< TX driver control */
#define ST25R3916_REG_TX_MOD_AM        0x0A    /**< TX modulation / AM */
#define ST25R3916_REG_RX_CONF1         0x0B    /**< RX configuration 1 */
#define ST25R3916_REG_RX_CONF2         0x0C    /**< RX configuration 2 */
#define ST25R3916_REG_RX_CONF3         0x0D    /**< RX configuration 3 */
#define ST25R3916_REG_RX_CONF4         0x0E    /**< RX configuration 4 */
#define ST25R3916_REG_CORR_CONF1       0x0F    /**< Correlator configuration 1 */
#define ST25R3916_REG_CORR_CONF2       0x10    /**< Correlator configuration 2 */
#define ST25R3916_REG_TIMER_EMV        0x11    /**< Timer / EMV configuration */
#define ST25R3916_REG_TIMEOUT           0x12    /**< Timeout configuration */

/* ── NFC command opcodes (for SPI_CMD_NFC_TRANSACT) ─────────────────────── */

#define NFC_CMD_REQA            0x26    /**< REQA (ISO 14443A) */
#define NFC_CMD_WUPA            0x52    /**< WUPA (ISO 14443A) */
#define NFC_CMD_ANTICOL         0x93    /**< Anticollision Level 1 */
#define NFC_CMD_SELECT          0x93    /**< SELECT Level 1 */
#define NFC_CMD_HALT            0x50    /**< HLTA (ISO 14443A) */
#define NFC_CMD_RATS            0xE0    /**< RATS (ISO 14443A) */
#define NFC_CMD_REQB            0x05    /**< REQB (ISO 14443B) */
#define NFC_CMD_ATTRIB          0x1D    /**< ATTRIB (ISO 14443B) */

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * st25r3916_init — Initialize the ST25R3916 NFC controller
 *
 * Performs a full reset, configures SPI2 and I2C1 interfaces,
 * sets up the 13.56 MHz oscillator, configures TX/RX parameters
 * for ISO 14443A at 106 kbps, and enables interrupt handling.
 *
 * Returns: 0 on success, negative on error
 */
int st25r3916_init(void);

/**
 * st25r3916_start_polling — Start NFC tag detection polling
 *
 * Activates the 13.56 MHz carrier field and begins polling for
 * ISO 14443A tags. Detected tags generate an interrupt on PIN_NFC_IRQ.
 */
void st25r3916_start_polling(void);

/**
 * st25r3916_stop_polling — Stop NFC tag detection polling
 *
 * Deactivates the carrier field and stops polling. Call before
 * entering low-power mode or switching to a different NFC mode.
 */
void st25r3916_stop_polling(void);

/**
 * st25r3916_get_field_strength_mv — Read NFC field strength in mV
 *
 * Reads the ST25R3916 measured field strength register and converts
 * it to millivolts. Useful for antenna tuning and debug.
 *
 * Returns: field strength in mV (0–5000)
 */
uint16_t st25r3916_get_field_strength_mv(void);

/**
 * st25r3916_send_command — Send a raw NFC command
 *
 * @cmd:       NFC command opcode (NFC_CMD_REQA, etc.)
 * @tx_data:   TX data buffer (may be NULL for no-TX commands)
 * @tx_len:    TX data length in bytes
 * @rx_data:   RX data buffer (may be NULL for no-RX commands)
 * @rx_len:    Pointer to RX buffer size on input, received bytes on output
 * @timeout_ms: Response timeout in milliseconds (0 = default 100 ms)
 *
 * Returns: 0 on success, negative on error
 */
int st25r3916_send_command(uint8_t cmd,
                            const uint8_t *tx_data, uint16_t tx_len,
                            uint8_t *rx_data, uint16_t *rx_len,
                            uint32_t timeout_ms);

#endif /* ST25R3916_INIT_H */