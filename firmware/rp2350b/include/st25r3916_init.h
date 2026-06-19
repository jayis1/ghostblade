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
 *
 * Reference: ST25R3916 Datasheet (DS12290), Section 5: Register Map
 */

#ifndef ST25R3916_INIT_H
#define ST25R3916_INIT_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * ST25R3916 Register Map (Space A)
 *
 * Register addresses per ST25R3916 datasheet (DS12290 Rev 5).
 * Space A is accessed directly via SPI with address byte encoding:
 *   Write single: bit7=0, bit6=0, addr[5:0]
 *   Read single:  bit7=1, bit6=0, addr[5:0]
 *   Write burst:  bit7=0, bit6=1, addr[5:0]
 *   Read burst:   bit7=1, bit6=1, addr[5:0]
 * ======================================================================== */

/* Identity registers (read-only) */
#define ST25R3916_REG_IC_IDENTITY       0x00    /**< IC identity (read: 0x39 or 0x89) */
#define ST25R3916_REG_IC_VERSION        0x01    /**< IC version */

/* I/O Configuration */
#define ST25R3916_REG_IO_CONF1          0x02    /**< I/O configuration 1 */
#define ST25R3916_REG_IO_CONF2          0x03    /**< I/O configuration 2 */

/* Operation Control */
#define ST25R3916_REG_OP_CTRL           0x04    /**< Operation control */

/* Mode Definition */
#define ST25R3916_REG_MODE_DEF          0x05    /**< Mode definition */

/* Bit Rate Definition */
#define ST25R3916_REG_BIT_RATE          0x06    /**< Bit rate definition */

/* Protocol Mode Registers */
#define ST25R3916_REG_ISO14443A_MODE    0x07    /**< ISO 14443A mode settings */
#define ST25R3916_REG_ISO14443B_MODE    0x08    /**< ISO 14443B mode settings */
#define ST25R3916_REG_FELICA_MODE       0x09    /**< FeliCa mode settings */
#define ST25R3916_REG_ISO15693_MODE     0x0A    /**< ISO 15693 mode settings */

/* Antenna Calibration */
#define ST25R3916_REG_ANT_CAL_TARGET    0x0B    /**< Antenna calibration target */
#define ST25R3916_REG_ANT_CAL_TIME      0x0C    /**< Antenna calibration time */
#define ST25R3916_REG_ANT_MIN           0x0D    /**< Antenna minimum */
#define ST25R3916_REG_ANT_MAX            0x0E    /**< Antenna maximum */
#define ST25R3916_REG_ANT_TUNE           0x0F    /**< Antenna tuning */
#define ST25R3916_REG_ANT_TUNE_MEAS      0x10    /**< Antenna tuning measure */

/* Auxiliary Modulation */
#define ST25R3916_REG_AUX_MOD            0x11    /**< Auxiliary modulation */

/* Receiver Configuration */
#define ST25R3916_REG_RX_CONF1           0x12    /**< Receiver configuration 1 */
#define ST25R3916_REG_RX_CONF2           0x13    /**< Receiver configuration 2 */
#define ST25R3916_REG_RX_CONF3           0x14    /**< Receiver configuration 3 */
#define ST25R3916_REG_RX_CONF4           0x15    /**< Receiver configuration 4 */

/* TX Driver */
#define ST25R3916_REG_TX_DRIVER          0x16    /**< TX driver */
#define ST25R3916_REG_TX_CURRENT          0x17    /**< TX current */
#define ST25R3916_REG_TX_CURRENT_SSC      0x18    /**< TX current SSC */
#define ST25R3916_REG_TX_CURRENT_SSC_HL   0x19    /**< TX current SSC HL */
#define ST25R3916_REG_TX_CURRENT_SSC_LH   0x1A    /**< TX current SSC LH */

/* Correlator Configuration */
#define ST25R3916_REG_CORR_CONF1          0x1B    /**< Correlator configuration 1 */
#define ST25R3916_REG_CORR_CONF2          0x1C    /**< Correlator configuration 2 */

/* Timer Registers */
#define ST25R3916_REG_TIMER_EMV          0x1D    /**< Timer EMV */
#define ST25R3916_REG_TIMER1              0x1E    /**< Timer 1 */
/* Note: 0x1F is the TX FIFO write-only register, NOT Timer 2.
 *       TX FIFO and Timer 2 share address 0x1F in Space A, but
 *       the TX FIFO is write-only while Timer 2 is read-only.
 *       Use ST25R3916_REG_TX_FIFO for writing and TIMER2 offset
 *       when reading the timer value. */
#define ST25R3916_REG_TIMER2              0x1F    /**< Timer 2 (read-only; TX FIFO at same addr is write-only) */
#define ST25R3916_REG_TIMER3              0x20    /**< Timer 3 */

/* Interrupt Masks */
#define ST25R3916_REG_IRQ_MASK1           0x21    /**< Interrupt mask 1 */
#define ST25R3916_REG_IRQ_MASK2           0x22    /**< Interrupt mask 2 */
#define ST25R3916_REG_IRQ_MASK3           0x23    /**< Interrupt mask 3 */
#define ST25R3916_REG_IRQ_MASK4           0x24    /**< Interrupt mask 4 */
#define ST25R3916_REG_IRQ_MASK5           0x25    /**< Interrupt mask 5 */

/* TX/RX Byte Count Registers */
#define ST25R3916_REG_NUM_TX_BYTES1       0x26    /**< Number of TX bytes 1 */
#define ST25R3916_REG_NUM_TX_BYTES2       0x27    /**< Number of TX bytes 2 */
#define ST25R3916_REG_NUM_TX_BYTES3       0x28    /**< Number of TX bytes 3 */
#define ST25R3916_REG_TX_FIFO_STATUS       0x29    /**< TX FIFO status */
#define ST25R3916_REG_NUM_RX_BYTES1       0x2A    /**< Number of RX bytes 1 */
#define ST25R3916_REG_NUM_RX_BYTES2       0x2B    /**< Number of RX bytes 2 */
#define ST25R3916_REG_NUM_RX_BYTES3       0x2C    /**< Number of RX bytes 3 */
#define ST25R3916_REG_RX_FIFO_STATUS       0x2D    /**< RX FIFO status */

/* Collision and INT Clear */
#define ST25R3916_REG_COLL_INT_CLEAR      0x2E    /**< Collision and INT clear */

/* Wake-up / Sleep Timers */
#define ST25R3916_REG_WUP_TIMER            0x2F    /**< Wake-up timer */
#define ST25R3916_REG_WUP_TIMER_GRAN       0x30    /**< Wake-up timer granularity */
#define ST25R3916_REG_SLP_TIMER            0x31    /**< Sleep timer */
#define ST25R3916_REG_SLP_TIMER_GRAN       0x32    /**< Sleep timer granularity */

/* Measurement and Validation */
#define ST25R3916_REG_MVT                  0x33    /**< Minimum validation time */
#define ST25R3916_REG_AGC_CONFIG           0x34    /**< AGC configuration */
#define ST25R3916_REG_AM_CONFIG            0x35    /**< AM configuration */
#define ST25R3916_REG_AM_GRANGE1           0x36    /**< AM gain range 1 */
#define ST25R3916_REG_AM_GRANGE2           0x37    /**< AM gain range 2 */
#define ST25R3916_REG_AM_GRANGE3           0x38    /**< AM gain range 3 */
#define ST25R3916_REG_WUP_COLL             0x39    /**< Wake-up collision */
#define ST25R3916_REG_RSSI                 0x3A    /**< RSSI level */

/* Observer Configuration */
#define ST25R3916_REG_OBSV_CONF1           0x3B    /**< Observer configuration 1 */
#define ST25R3916_REG_OBSV_CONF2           0x3C    /**< Observer configuration 2 */
#define ST25R3916_REG_OBSV_CONF3           0x3D    /**< Observer configuration 3 */

/* Oscillator and Regulator */
#define ST25R3916_REG_OSC_CONF             0x3E    /**< Oscillator configuration */
#define ST25R3916_REG_VREG_CONF             0x3F    /**< Voltage regulator configuration */

/* Test / Discovery Configuration */
#define ST25R3916_REG_TEST_DISC_CONF1       0x3E    /**< Test discovery configuration 1 (same as OSC_CONF on some revs) */
#define ST25R3916_REG_TEST_DISC_CONF2       0x3F    /**< Test discovery configuration 2 (same as VREG_CONF on some revs) */

/* ========================================================================
 * ST25R3916 IRQ Status Registers (Space A, read to clear)
 * These are at fixed addresses and are read to clear pending IRQ flags.
 * ======================================================================== */

#define ST25R3916_REG_IRQ_STATUS1          0x04    /**< IRQ status 1 (read to clear) */
#define ST25R3916_REG_IRQ_STATUS2          0x05    /**< IRQ status 2 (read to clear) */
#define ST25R3916_REG_IRQ_STATUS3          0x06    /**< IRQ status 3 (read to clear) */
#define ST25R3916_REG_IRQ_STATUS4          0x07    /**< IRQ status 4 (read to clear) */
#define ST25R3916_REG_IRQ_STATUS5          0x08    /**< IRQ status 5 (read to clear) */

/* TX FIFO (Space A, write-only) */
#define ST25R3916_REG_TX_FIFO              0x1F    /**< TX FIFO write (write-only) */

/* ========================================================================
 * ST25R3916 Space B Registers (accessed via Space B gateway at 0x40)
 * ======================================================================== */

#define ST25R3916_REG_SPACE_B_CTRL         0x40    /**< Space B control gateway */

/* Space B registers (accessed through Space B gateway mechanism) */
#define ST25R3916_REG_EMD_SUP_CONF         0x01    /**< EMD suppression config (Space B) */
#define ST25R3916_REG_SUBCORR_THRESH       0x02    /**< Sub-correlator threshold (Space B) */
#define ST25R3916_REG_RATS_RESP1            0x03    /**< RATS response 1 (Space B) */
#define ST25R3916_REG_RATS_RESP2            0x04    /**< RATS response 2 (Space B) */
#define ST25R3916_REG_PV_ADAPT_LOAD         0x05    /**< PV adapt load (Space B) */
#define ST25R3916_REG_DPO_ISO15693          0x06    /**< DPO ISO 15693 (Space B) */
#define ST25R3916_REG_DPO_ISO14443A         0x07    /**< DPO ISO 14443A (Space B) */
#define ST25R3916_REG_DPO_3                 0x08    /**< DPO 3 (Space B) */
#define ST25R3916_REG_DPO_I_CODE            0x09    /**< DPO I-CODE (Space B) */

/* ========================================================================
 * ST25R3916 Direct Commands
 *
 * Direct commands are sent as SPI write transactions with bit7=1, bit6=1.
 * Command addresses range from 0xC1 to 0xD3.
 * ======================================================================== */

#define ST25R3916_CMD_SET_DEFAULT             0xC1   /**< Set Default */
#define ST25R3916_CMD_INITIALIZE              0xC2   /**< Initialize */
#define ST25R3916_CMD_INITIALIZE_DPO           0xC3   /**< Initialize DPO */
#define ST25R3916_CMD_CLEAR_IRQS              0xC4   /**< Clear Interrupts */
#define ST25R3916_CMD_MEASURE_VDD             0xC5   /**< Measure VDD */
#define ST25R3916_CMD_TX_ON                   0xC6   /**< TX On */
#define ST25R3916_CMD_TX_OFF                  0xC7   /**< TX Off */
#define ST25R3916_CMD_CALIBRATE_ANTENNA       0xC8   /**< Calibrate Antenna */
#define ST25R3916_CMD_MEASURE_AMPLITUDE       0xC9   /**< Measure Amplitude */
#define ST25R3916_CMD_MEASURE_PHASE           0xCA   /**< Measure Phase */
#define ST25R3916_CMD_GOTO_SENSE              0xD0   /**< Go to Sense */
#define ST25R3916_CMD_GOTO_SLEEP              0xD1   /**< Go to Sleep */
#define ST25R3916_CMD_START_WUP_TIMER         0xD2   /**< Start Wake-up Timer */
#define ST25R3916_CMD_START_GP_TIMER          0xD3   /**< Start General Purpose Timer */
#define ST25R3916_CMD_SPACE_B_ACCESS          0xFF   /**< Access Space B */

/* ========================================================================
 * ST25R3916 IRQ Status Register Bits
 * ======================================================================== */

/* IRQ1 bits */
#define ST25R3916_IRQ1_OSC                    (1 << 0)   /* Oscillator on */
#define ST25R3916_IRQ1_FELICA                 (1 << 1)   /* FeliCa interrupt */
#define ST25R3916_IRQ1_NFCA                   (1 << 2)   /* NFC-A interrupt */
#define ST25R3916_IRQ1_NFCB                   (1 << 3)   /* NFC-B interrupt */
#define ST25R3916_IRQ1_NFCF                   (1 << 4)   /* NFC-F interrupt */
#define ST25R3916_IRQ1_NFCV                   (1 << 5)   /* NFC-V interrupt */
#define ST25R3916_IRQ1_TXE                    (1 << 6)   /* TX ended */
#define ST25R3916_IRQ1_RXE                    (1 << 7)   /* RX ended */

/* IRQ2 bits */
#define ST25R3916_IRQ2_CAC                    (1 << 0)   /* Collision during anticollision */
#define ST25R3916_IRQ2_WU_F                  (1 << 1)   /* Wake-up fall */
#define ST25R3916_IRQ2_WU_A                  (1 << 2)   /* Wake-up active */
#define ST25R3916_IRQ2_WU_S                  (1 << 3)   /* Wake-up sense */
#define ST25R3916_IRQ2_RXS                   (1 << 4)   /* RX start */
#define ST25R3916_IRQ2_RX_F                  (1 << 5)   /* RX FIFO full */
#define ST25R3916_IRQ2_TX_F                  (1 << 6)   /* TX FIFO full */
#define ST25R3916_IRQ2_DCT                   (1 << 7)   /* Discovery cycle done */

/* IRQ3 bits */
#define ST25R3916_IRQ3_GPP_TIMER              (1 << 0)   /* General Purpose Timer */
#define ST25R3916_IRQ3_LMS                   (1 << 1)   /* Length mis-match */
#define ST25R3916_IRQ3_CRC                   (1 << 2)   /* CRC error */
#define ST25R3916_IRQ3_EON                    (1 << 3)   /* External field on */
#define ST25R3916_IRQ3_EOF                   (1 << 4)   /* External field off */
#define ST25R3916_IRQ3_EMD                   (1 << 5)   /* EMD detected */
#define ST25R3916_IRQ3_AWU                   (1 << 6)   /* Auto wake-up */
#define ST25R3916_IRQ3_RFD                   (1 << 7)   /* RF field detected */

/* ========================================================================
 * ST25R3916 SPI Access Macros
 *
 * The ST25R3916 SPI protocol encodes the operation in the address byte:
 *   Bit 7: 1 = read, 0 = write
 *   Bit 6: 1 = burst, 0 = single
 *   Bits 5:0: register address or Space B command
 * ======================================================================== */

#define ST25R3916_WRITE_SINGLE(addr)    (((addr) & 0x3F))        /* Write single register */
#define ST25R3916_WRITE_BURST(addr)     (((addr) & 0x3F) | 0x40) /* Write burst registers */
#define ST25R3916_READ_SINGLE(addr)     (((addr) & 0x3F) | 0x80) /* Read single register */
#define ST25R3916_READ_BURST(addr)      (((addr) & 0x3F) | 0xC0) /* Read burst registers */
#define ST25R3916_DIRECT_CMD(cmd)        ((cmd) & 0xFF)           /* Direct command strobe */

/* ========================================================================
 * Forward Declarations (platform-specific, provided by board code)
 * ======================================================================== */

extern void apex_nfc_cs_assert(void);
extern void apex_nfc_cs_release(void);
extern uint8_t apex_nfc_spi_xfer(uint8_t tx_byte);
extern void apex_nfc_write_register(uint8_t addr, uint8_t val);
extern uint8_t apex_nfc_read_register(uint8_t addr);

/* GPIO for IRQ */
#define RP2350B_GPIO_BASE     0x400D0000UL
#define PIN_NFC_IRQ           44

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * st25r3916_init — Initialize the ST25R3916 NFC controller
 *
 * Performs a full reset, configures SPI2 and I2C1 interfaces,
 * sets up the 13.56 MHz oscillator, configures TX/RX parameters
 * for ISO 14443A at 106 kbps, and enables interrupt handling.
 *
 * Returns: 0 on success, negative on error
 *   -1: IC identity check failed (wrong chip or not responding)
 *   -2: Oscillator not ready after timeout
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
 * st25r3916_send_command — Send a direct command to the ST25R3916
 *
 * @cmd: Command byte (0xC1-0xD3)
 *
 * Sends a command strobe via SPI. This is the primary command interface
 * for direct commands such as SET_DEFAULT, CLEAR_IRQS, TX_ON, etc.
 * Use st25r3916_transact() for full NFC command transactions with
 * TX/RX data exchange (REQA, anticollision, etc.).
 */
void st25r3916_send_command(uint8_t cmd);

/**
 * st25r3916_transact — Perform a full NFC command transaction
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
int st25r3916_transact(uint8_t cmd,
                       const uint8_t *tx_data, uint16_t tx_len,
                       uint8_t *rx_data, uint16_t *rx_len,
                       uint32_t timeout_ms);

/* ========================================================================
 * Register Access Functions (used internally and for advanced configuration)
 * ======================================================================== */

/**
 * st25r3916_write_reg — Write a single ST25R3916 register
 * @addr: Register address (Space A: 0x00-0x3F, Space B via gateway)
 * @val:  Value to write
 */
void st25r3916_write_reg(uint8_t addr, uint8_t val);

/**
 * st25r3916_read_reg — Read a single ST25R3916 register
 * @addr: Register address
 * Returns: Register value
 */
uint8_t st25r3916_read_reg(uint8_t addr);

/**
 * st25r3916_write_multiple_regs — Write multiple consecutive registers
 * @start_addr: Starting register address
 * @values:      Array of values to write
 * @len:         Number of registers to write
 */
void st25r3916_write_multiple_regs(uint8_t start_addr,
                                    const uint8_t *values, uint8_t len);

/**
 * st25r3916_strobe — Alias for st25r3916_send_command
 *
 * @cmd: Command byte (0xC1-0xD3)
 * Returns: 0 (for API compatibility with RFAL-style drivers)
 */
static inline uint8_t st25r3916_strobe(uint8_t cmd) {
    st25r3916_send_command(cmd);
    return 0;
}

/**
 * st25r3916_clear_interrupts — Clear all pending interrupt flags
 *
 * Reads all 5 IRQ status registers to clear pending flags.
 * Any NULL pointer parameter will skip that register read.
 */
void st25r3916_clear_interrupts(uint8_t *irq1, uint8_t *irq2,
                                uint8_t *irq3, uint8_t *irq4,
                                uint8_t *irq5);

/* ========================================================================
 * NFC Command Opcodes (for st25r3916_send_command and SPI protocol)
 * ======================================================================== */

#define NFC_CMD_REQA            0x26    /**< REQA (ISO 14443A) */
#define NFC_CMD_WUPA            0x52    /**< WUPA (ISO 14443A) */
#define NFC_CMD_ANTICOL         0x93    /**< Anticollision Level 1 */
#define NFC_CMD_SELECT          0x93    /**< SELECT Level 1 */
#define NFC_CMD_HALT            0x50    /**< HLTA (ISO 14443A) */
#define NFC_CMD_RATS            0xE0    /**< RATS (ISO 14443A) */
#define NFC_CMD_REQB            0x05    /**< REQB (ISO 14443B) */
#define NFC_CMD_ATTRIB          0x1D    /**< ATTRIB (ISO 14443B) */

/**
 * st25r3916_field_on — Turn on the NFC RF field
 *
 * Enables the 13.56 MHz carrier for tag detection.
 */
void st25r3916_field_on(void);

/**
 * st25r3916_field_off — Turn off the NFC RF field
 *
 * Disables the 13.56 MHz carrier.
 */
void st25r3916_field_off(void);

/**
 * st25r3916_measure_vdd — Measure the NFC VDD supply voltage
 *
 * Sends the MEASURE_VDD direct command and reads the result.
 *
 * Returns: VDD voltage in mV (approximate)
 */
uint16_t st25r3916_measure_vdd(void);

/**
 * st25r3916_get_rssi — Get the NFC field RSSI level
 *
 * Returns: RSSI value (raw register value, 0-255)
 */
uint8_t st25r3916_get_rssi(void);

/**
 * st25r3916_sleep — Put ST25R3916 into low-power mode
 *
 * Disables the RF field and enters sleep state.
 * Wake-up requires SPI command or WUP timer expiry.
 */
void st25r3916_sleep(void);

/**
 * st25r3916_iso14443a_reqa — Send REQA (Request Type A) command
 *
 * Sends a 7-bit REQA (0x26) or WUPA (0x52) command to poll for
 * ISO 14443A tags in the field.
 *
 * Returns: 0 on success (tag detected), -1 on no response
 */
int st25r3916_iso14443a_reqa(void);

#endif /* ST25R3916_INIT_H */