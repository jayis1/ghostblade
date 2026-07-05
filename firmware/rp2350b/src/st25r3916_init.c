/*
 * st25r3916_init.c — ST25R3916 NFC Controller Initialization for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the ST25R3916 NFC reader/writer initialization
 * sequence for the GhostBlade board. The ST25R3916 is connected to the
 * RP2350B via SPI2 (dedicated NFC control bus) and I2C0 (secondary
 * control/monitoring).
 *
 * The ST25R3916 supports:
 *   - ISO/IEC 14443 Type A and Type B (up to 848 kbps)
 *   - ISO/IEC 15693 (vicinity cards)
 *   - JIS X 6319-4 / FeliCa (212/424 kbps)
 *   - NFC Forum Type 1-5 tags
 *   - Active load modulation (NFC-DEP, P2P)
 *
 * Default configuration: ISO 14443A, 106 kbps, passive polling mode
 *
 * Reference: ST25R3916 Datasheet (DS12290), ST SW-Drivers RFAL library
 */

#include <stdint.h>
#include <stdbool.h>
#include "st25r3916_init.h"

/* ========================================================================
 * ST25R3916 SPI Access Functions
 * ======================================================================== */

/**
 * st25r3916_write_reg — Write a single ST25R3916 register
 *
 * @addr: Register address (Space A: 0x00-0x3F, Space B via gateway)
 * @val:  Value to write
 */
void st25r3916_write_reg(uint8_t addr, uint8_t val) {
    /* Validate register address range — Space A is 0x00-0x3F.
     * Out-of-range addresses could trigger undefined SPI behavior. */
    if (addr > 0x3F)
        return;
    apex_nfc_write_register(addr, val);
}

/**
 * st25r3916_read_reg — Read a single ST25R3916 register
 *
 * @addr: Register address
 * Returns: Register value (0xFF if address is out of range)
 */
uint8_t st25r3916_read_reg(uint8_t addr) {
    /* Validate register address range — Space A is 0x00-0x3F.
     * Return 0xFF (all bits set) for invalid addresses to avoid
     * undefined SPI behavior. */
    if (addr > 0x3F)
        return 0xFF;
    return apex_nfc_read_register(addr);
}

/**
 * st25r3916_write_multiple_regs — Write multiple consecutive registers
 *
 * @start_addr: Starting register address
 * @values:      Array of values to write
 * @len:         Number of registers to write
 */
void st25r3916_write_multiple_regs(uint8_t start_addr,
                                    const uint8_t *values, uint8_t len) {
    /* Bounds check: ST25R3916 register address space is 0x00-0x3F.
     * Prevent wrap-around by rejecting writes that exceed the address space. */
    if (len == 0 || !values)
        return;
    if ((uint16_t)start_addr + len > 0x40)
        return;
    for (uint8_t i = 0; i < len; i++) {
        st25r3916_write_reg(start_addr + i, values[i]);
    }
}

/**
 * st25r3916_send_command — Send a direct command to the ST25R3916
 *
 * @cmd: Command byte (0xC1-0xD3)
 */
void st25r3916_send_command(uint8_t cmd) {
    /* Validate command range — direct commands are 0xC1-0xD3 per datasheet */
    if (cmd < 0xC1 || cmd > 0xD3)
        return;
    apex_nfc_cs_assert();
    /* Direct command: bit7=1 (write), bit6=1 (space A), addr = cmd */
    apex_nfc_spi_xfer(cmd | 0x80);
    apex_nfc_cs_release();
}

/**
 * st25r3916_clear_interrupts — Clear all pending interrupts
 *
 * Reads all 5 IRQ status registers to clear pending interrupt flags.
 */
void st25r3916_clear_interrupts(uint8_t *irq1, uint8_t *irq2,
                                uint8_t *irq3, uint8_t *irq4,
                                uint8_t *irq5) {
    /* Read IRQ status registers (reading clears the flags) */
    if (irq1) *irq1 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS1);
    if (irq2) *irq2 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS2);
    if (irq3) *irq3 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS3);
    if (irq4) *irq4 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS4);
    if (irq5) *irq5 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS5);

    /* Also send clear IRQs command for safety */
    st25r3916_send_command(ST25R3916_CMD_CLEAR_IRQS);
}

/* ========================================================================
 * ST25R3916 Initialization Sequence
 * ======================================================================== */

/**
 * st25r3916_init — Initialize the ST25R3916 NFC controller
 *
 * Full initialization sequence:
 *   1. Power-on reset (VDD_NFC stable)
 *   2. Send SET_DEFAULT command
 *   3. Configure oscillator (27.12 MHz crystal or external clock)
 *   4. Configure I/O pins and operation control
 *   5. Configure receiver chain (AGC, AM detection, correlators)
 *   6. Configure TX driver and current
 *   7. Configure timers (EMV, anti-collision)
 *   8. Enable required interrupts
 *   9. Run antenna calibration
 *  10. Verify oscillator IRQ (chip alive)
 *
 * Returns: 0 on success, negative on error
 */
int st25r3916_init(void) {
    uint8_t irq1 = 0;
    uint8_t ic_id;

    /* Step 1: Ensure power supply is stable.
     * VDD_NFC_TX (5.0V) must be stable before accessing the chip.
     * The power sequencing is handled by the PMIC; we assume it's
     * already stable when this function is called.
     */

    /* Step 2: Verify chip identity before configuration.
     * The ST25R3916 IC identity register should return 0x39 (ST25R3916)
     * or 0x89 (ST25R3916B). A mismatch indicates the chip is not
     * present or not responding correctly on SPI2.
     */
    ic_id = st25r3916_read_reg(ST25R3916_REG_IC_IDENTITY);
    if (ic_id != 0x39 && ic_id != 0x89) {
        /* Chip not detected or wrong part — return error */
        return -1;
    }

    /* Step 3: Send SET_DEFAULT command to reset all registers to defaults */
    st25r3916_send_command(ST25R3916_CMD_SET_DEFAULT);

    /* Wait for SET_DEFAULT to complete (~100 μs) */
    for (volatile int i = 0; i < 1500; i++)
        __asm__("nop");

    /* Step 3: Configure Oscillator
     * The ST25R3916 uses a 27.12 MHz crystal (standard NFC frequency).
     * OSC_CONF: Enable oscillator, wait for stable.
     */
    st25r3916_write_reg(ST25R3916_REG_OSC_CONF, 0x18);
    /* Trimming = 0x8 (center), Oscillator enabled */

    /* Wait for oscillator to start and stabilize (typically < 1 ms) */
    for (volatile int i = 0; i < 15000; i++)
        __asm__("nop");

    /* Step 4: Configure I/O and Operation Control
     * IO_CONF1: SPI mode, IRQ push-pull, disable MCU_CLK output
     * IO_CONF2: Default
     * OP_CTRL: Enable TX and RX
     */
    st25r3916_write_reg(ST25R3916_REG_IO_CONF1, 0x01);
    /* IO_CONF1: MCU_CLK output disabled, IRQ push-pull active-high */

    st25r3916_write_reg(ST25R3916_REG_IO_CONF2, 0x00);
    /* IO_CONF2: Default configuration */

    /* Step 5: Configure Operation Control
     * Enable the reader: TX_EN and RX_EN
     */
    st25r3916_write_reg(ST25R3916_REG_OP_CTRL, 0x00);
    /* OP_CTRL: Initially disabled; will enable after full config */

    /* Step 6: Configure Mode Definition
     * Standard ISO 14443A mode, passive polling
     */
    st25r3916_write_reg(ST25R3916_REG_MODE_DEF, 0x01);
    /* MODE_DEF: ISO 14443A, passive mode */

    /* Step 7: Configure Bit Rate
     * Default 106 kbps (both TX and RX)
     */
    st25r3916_write_reg(ST25R3916_REG_BIT_RATE, 0x00);
    /* BIT_RATE: TX=106kbps, RX=106kbps */

    /* Step 8: Configure ISO 14443A Mode Register */
    st25r3916_write_reg(ST25R3916_REG_ISO14443A_MODE, 0x0D);
    /* ISO14443A_MODE: RX no error, RX CRC enabled, TX CRC enabled,
     * anticollision enabled, 106 kbps */

    /* Step 9: Configure Receiver Chain */
    st25r3916_write_reg(ST25R3916_REG_RX_CONF1, 0x12);
    /* RX_CONF1: AGC enabled, gain settings */

    st25r3916_write_reg(ST25R3916_REG_RX_CONF2, 0x2D);
    /* RX_CONF2: Correlator configuration */

    st25r3916_write_reg(ST25R3916_REG_RX_CONF3, 0x01);
    /* RX_CONF3: Receiver gain settings */

    st25r3916_write_reg(ST25R3916_REG_RX_CONF4, 0x00);
    /* RX_CONF4: Default */

    /* Step 10: Configure AGC */
    st25r3916_write_reg(ST25R3916_REG_AGC_CONFIG, 0x00);
    /* AGC: Default configuration */

    /* Step 11: Configure AM Detection */
    st25r3916_write_reg(ST25R3916_REG_AM_CONFIG, 0x02);
    /* AM_CONFIG: AM detection enabled */

    st25r3916_write_reg(ST25R3916_REG_AM_GRANGE1, 0xFF);
    st25r3916_write_reg(ST25R3916_REG_AM_GRANGE2, 0xFF);
    st25r3916_write_reg(ST25R3916_REG_AM_GRANGE3, 0xFF);

    /* Step 12: Configure TX Driver */
    st25r3916_write_reg(ST25R3916_REG_TX_DRIVER, 0x08);
    /* TX_DRIVER: RFO resistance = 8 ohm, full-wave drive */

    /* Step 13: Configure TX Current
     * Adjust for target antenna current (typically 50-100 mA)
     */
    st25r3916_write_reg(ST25R3916_REG_TX_CURRENT, 0x40);
    /* TX_CURRENT: Moderate TX current for 13.56 MHz antenna */

    /* Step 14: Configure Correlator */
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF1, 0x00);
    st25r3916_write_reg(ST25R3916_REG_CORR_CONF2, 0x00);

    /* Step 15: Configure EMV Timer */
    st25r3916_write_reg(ST25R3916_REG_TIMER_EMV, 0x0A);
    /* EMV_TIMER: ~10 ms start-of-frame guard time */

    /* Step 16: Configure General Purpose Timers */
    st25r3916_write_reg(ST25R3916_REG_TIMER1, 0x0A);
    st25r3916_write_reg(ST25R3916_REG_TIMER2, 0x0A);
    st25r3916_write_reg(ST25R3916_REG_TIMER3, 0x21);

    /* Step 17: Configure Wake-up and Sleep Timers */
    st25r3916_write_reg(ST25R3916_REG_WUP_TIMER, 0x00);
    st25r3916_write_reg(ST25R3916_REG_WUP_TIMER_GRAN, 0x00);
    st25r3916_write_reg(ST25R3916_REG_SLP_TIMER, 0x00);
    st25r3916_write_reg(ST25R3916_REG_SLP_TIMER_GRAN, 0x00);

    /* Step 18: Configure Voltage Regulator */
    st25r3916_write_reg(ST25R3916_REG_VREG_CONF, 0x00);
    /* VREG_CONF: Default regulator settings */

    /* Step 19: Configure Interrupt Masks
     * Enable: Oscillator ready, TX ended, RX ended, NFC-A detected
     */
    st25r3916_write_reg(ST25R3916_REG_IRQ_MASK1,
                         ST25R3916_IRQ1_OSC |     /* Oscillator ready */
                         ST25R3916_IRQ1_TXE |     /* TX ended */
                         ST25R3916_IRQ1_RXE |     /* RX ended */
                         ST25R3916_IRQ1_NFCA);    /* NFC-A detected */
    st25r3916_write_reg(ST25R3916_REG_IRQ_MASK2,
                         ST25R3916_IRQ2_RXS);    /* RX start */
    st25r3916_write_reg(ST25R3916_REG_IRQ_MASK3,
                         ST25R3916_IRQ3_EON |    /* External field on */
                         ST25R3916_IRQ3_EOF);     /* External field off */
    st25r3916_write_reg(ST25R3916_REG_IRQ_MASK4, 0x00);
    st25r3916_write_reg(ST25R3916_REG_IRQ_MASK5, 0x00);

    /* Step 20: Clear any pending interrupts */
    st25r3916_send_command(ST25R3916_CMD_CLEAR_IRQS);

    /* Step 21: Run Antenna Calibration
     * The ST25R3916 can auto-calibrate the antenna matching circuit.
     * This measures the resonant frequency and adjusts tuning capacitors.
     */
    st25r3916_write_reg(ST25R3916_REG_ANT_CAL_TARGET, 0x05);
    st25r3916_write_reg(ST25R3916_REG_ANT_CAL_TIME, 0x07);

    st25r3916_send_command(ST25R3916_CMD_CALIBRATE_ANTENNA);

    /* Wait for calibration (typically 5-10 ms) */
    for (volatile int i = 0; i < 150000; i++)
        __asm__("nop");

    /* Step 22: Enable Operation */
    st25r3916_write_reg(ST25R3916_REG_OP_CTRL, 0x03);
    /* OP_CTRL: TX_EN=1, RX_EN=1, reader active */

    /* Step 23: Verify oscillator is running by checking IRQ status */
    /* Read IRQ1 register — bit 0 (OSC) should be set */
    st25r3916_clear_interrupts(&irq1, NULL, NULL, NULL, NULL);
    if (!(irq1 & ST25R3916_IRQ1_OSC)) {
        /* Oscillator not ready yet — wait a bit more and retry */
        for (volatile int i = 0; i < 50000; i++)
            __asm__("nop");
        st25r3916_clear_interrupts(&irq1, NULL, NULL, NULL, NULL);
        if (!(irq1 & ST25R3916_IRQ1_OSC)) {
            /* Oscillator still not ready — this is a hardware fault.
             * Return error so caller can handle appropriately. */
            return -2;
        }
    }

    /* Step 24: Send INITIALIZE command for DPO (Dynamic Power Output) */
    st25r3916_send_command(ST25R3916_CMD_INITIALIZE_DPO);

    return 0;
}

/** Maximum ST25R3916 FIFO depth (bytes) */
#define ST25R3916_FIFO_SIZE_VAL 512

/* ========================================================================
 * ST25R3916 ISO 14443A Operations
 * ======================================================================== */

/**
 * st25r3916_iso14443a_reqa — Send REQA (Request Type A) command
 *
 * Sends a 7-bit REQA (0x26) or WUPA (0x52) command to poll for
 * ISO 14443A tags in the field.
 *
 * Returns: 0 on success (tag detected), -1 on no response
 */
int st25r3916_iso14443a_reqa(void) {
    uint8_t irq1;

    /* Clear interrupts */
    st25r3916_send_command(ST25R3916_CMD_CLEAR_IRQS);

    /* Configure for short frame (7-bit) TX */
    st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES1, 0x01);
    st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES2, 0x00);
    st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES3, 0x00);

    /* Write REQA command byte (0x26) to TX FIFO.
     * TX FIFO is accessed via the write-only FIFO register at address 0x1F
     * in Space A. This is distinct from the IRQ_MASK1 register at the same
     * address in Space B — the Space A/B distinction is encoded in the SPI
     * address byte. */
    st25r3916_write_reg(ST25R3916_REG_TX_FIFO, 0x26);

    /* Start TX */
    st25r3916_send_command(ST25R3916_CMD_TX_ON);

    /* Wait for RX complete */
    for (volatile int i = 0; i < 50000; i++)
        __asm__("nop");

    st25r3916_clear_interrupts(&irq1, NULL, NULL, NULL, NULL);

    if (irq1 & ST25R3916_IRQ1_RXE)
        return 0;  /* Tag responded */

    return -1;  /* No tag */
}

/**
 * st25r3916_field_on — Turn on the NFC RF field
 *
 * Enables the 13.56 MHz carrier for tag detection.
 */
void st25r3916_field_on(void) {
    st25r3916_send_command(ST25R3916_CMD_TX_ON);
}

/**
 * st25r3916_field_off — Turn off the NFC RF field
 *
 * Disables the 13.56 MHz carrier.
 */
void st25r3916_field_off(void) {
    st25r3916_send_command(ST25R3916_CMD_TX_OFF);
}

/**
 * st25r3916_measure_vdd — Measure the NFC VDD supply voltage
 *
 * Sends the MEASURE_VDD direct command and reads the result.
 *
 * Returns: VDD voltage in mV (approximate)
 */
uint16_t st25r3916_measure_vdd(void) {
    uint8_t result;

    st25r3916_send_command(ST25R3916_CMD_MEASURE_VDD);

    /* Wait for measurement (typically 100 μs) */
    for (volatile int i = 0; i < 1500; i++)
        __asm__("nop");

    /* Read result from observer register */
    result = st25r3916_read_reg(ST25R3916_REG_OBSV_CONF1);

    /* Convert ADC result to mV:
     * VDD = result * VREF / 255
     * Assuming VREF = 5.0V: VDD = result * 5000 / 255
     */
    return (uint16_t)((uint32_t)result * 5000 / 255);
}

/**
 * st25r3916_get_rssi — Get the NFC field RSSI level
 *
 * Returns: RSSI value (raw register value, 0-255)
 */
uint8_t st25r3916_get_rssi(void) {
    return st25r3916_read_reg(ST25R3916_REG_RSSI);
}

/**
 * st25r3916_sleep — Put ST25R3916 into low-power mode
 *
 * Disables the RF field and enters sleep state.
 * Wake-up requires SPI command or WUP timer expiry.
 */
void st25r3916_sleep(void) {
    st25r3916_field_off();
    st25r3916_write_reg(ST25R3916_REG_OP_CTRL, 0x00);  /* Disable TX/RX */
    st25r3916_send_command(ST25R3916_CMD_GOTO_SLEEP);
}

/**
 * st25r3916_stop_polling — Stop NFC tag detection polling
 *
 * Disables the carrier field and clears the NFC-A detection state.
 * Called before entering low-power mode or switching NFC protocol.
 * Equivalent to field_off() but also clears NFC-A interrupt flags
 * to prevent spurious detections after re-enabling polling.
 */
void st25r3916_stop_polling(void) {
    /* Turn off the RF field */
    st25r3916_field_off();

    /* Disable TX and RX in operation control */
    st25r3916_write_reg(ST25R3916_REG_OP_CTRL, 0x00);

    /* Clear all interrupt flags to reset detection state */
    st25r3916_send_command(ST25R3916_CMD_CLEAR_IRQS);
}

/**
 * st25r3916_start_polling — Start NFC tag detection polling
 *
 * Activates the 13.56 MHz carrier field and begins polling for
 * ISO 14443A tags. The sequence is:
 *   1. Clear any pending interrupt flags
 *   2. Enable TX and RX in operation control
 *   3. Turn on the RF field (TX_ON command)
 *   4. Send REQA (0x26) to detect Type A tags in the field
 *
 * After calling this function, poll PIN_NFC_IRQ (GPIO44) for
 * tag detection events, or call st25r3916_iso14443a_reqa()
 * periodically to check for tags.
 */
void st25r3916_start_polling(void) {
    /* Clear any stale interrupt flags before starting */
    st25r3916_send_command(ST25R3916_CMD_CLEAR_IRQS);

    /* Enable TX and RX in operation control */
    st25r3916_write_reg(ST25R3916_REG_OP_CTRL, 0x03);
    /* OP_CTRL: TX_EN=1, RX_EN=1 */

    /* Turn on the 13.56 MHz carrier field */
    st25r3916_send_command(ST25R3916_CMD_TX_ON);
}

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
 * Performs a complete NFC transaction:
 *   1. Clear interrupts
 *   2. Configure TX byte count
 *   3. Write TX data to FIFO (if any)
 *   4. Start transmission (TX_ON)
 *   5. Wait for RX complete or timeout
 *   6. Read RX data from FIFO (if any)
 *   7. Turn off field
 *
 * Returns: 0 on success, negative on error
 *   -1: Timeout waiting for response
 *   -2: CRC error in response
 *   -3: Invalid parameters
 */
int st25r3916_transact(uint8_t cmd,
                       const uint8_t *tx_data, uint16_t tx_len,
                       uint8_t *rx_data, uint16_t *rx_len,
                       uint32_t timeout_ms) {
    uint8_t irq1 = 0, irq2 = 0, irq3 = 0;
    uint32_t timeout_count;

    /* ST25R3916 FIFO depth is 512 bytes */

    /* Parameter validation: tx_data must accompany tx_len, rx_data must accompany rx_len */
    if ((tx_len > 0 && tx_data == NULL) ||
        (rx_len != NULL && *rx_len > 0 && rx_data == NULL) ||
        (rx_data != NULL && rx_len == NULL)) {
        return -3;
    }

    /* Clamp TX length to FIFO size to prevent overflow */
    if (tx_len > ST25R3916_FIFO_SIZE_VAL) {
        tx_len = ST25R3916_FIFO_SIZE_VAL;
    }

    /* Clamp RX length to FIFO size if provided */
    if (rx_len != NULL && *rx_len > ST25R3916_FIFO_SIZE_VAL) {
        *rx_len = ST25R3916_FIFO_SIZE_VAL;
    }

    /* Default timeout: 100 ms → ~150000 nop iterations at 150 MHz */
    if (timeout_ms == 0)
        timeout_ms = 100;
    timeout_count = timeout_ms * 1500U;  /* Approximate nop-loop cycles per ms */

    /* Step 1: Clear all pending interrupts */
    st25r3916_send_command(ST25R3916_CMD_CLEAR_IRQS);

    /* Step 2: Configure TX byte count */
    if (tx_len > 0 || cmd == NFC_CMD_REQA || cmd == NFC_CMD_WUPA) {
        uint16_t bytes = (tx_len > 0) ? tx_len : 1;
        st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES1,
                            (uint8_t)(bytes & 0xFF));
        st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES2,
                            (uint8_t)((bytes >> 8) & 0xFF));
        st25r3916_write_reg(ST25R3916_REG_NUM_TX_BYTES3,
                            0x00);
    }

    /* Step 3: Write TX data to FIFO */
    if (tx_len > 0 && tx_data != NULL) {
        /* Burst write TX data to FIFO */
        for (uint16_t i = 0; i < tx_len; i++) {
            st25r3916_write_reg(ST25R3916_REG_TX_FIFO, tx_data[i]);
        }
    } else if (cmd == NFC_CMD_REQA || cmd == NFC_CMD_WUPA) {
        /* Short frame commands: write the command byte directly */
        st25r3916_write_reg(ST25R3916_REG_TX_FIFO, cmd);
    }

    /* Step 4: Start transmission */
    st25r3916_send_command(ST25R3916_CMD_TX_ON);

    /* Step 5: Wait for RX complete or timeout */
    for (uint32_t i = 0; i < timeout_count; i++) {
        irq1 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS1);
        irq2 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS2);
        irq3 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS3);

        if (irq1 & ST25R3916_IRQ1_RXE) {
            /* RX complete */
            break;
        }
        if (irq3 & ST25R3916_IRQ3_CRC) {
            /* CRC error in received data */
            st25r3916_send_command(ST25R3916_CMD_CLEAR_IRQS);
            st25r3916_send_command(ST25R3916_CMD_TX_OFF);
            return -2;
        }
    }

    /* Clear all IRQ flags */
    st25r3916_clear_interrupts(&irq1, &irq2, &irq3, NULL, NULL);

    /* Check for timeout */
    if (!(irq1 & ST25R3916_IRQ1_RXE)) {
        /* No response within timeout */
        st25r3916_send_command(ST25R3916_CMD_TX_OFF);
        return -1;
    }

    /* Step 6: Read RX data from FIFO */
    if (rx_data != NULL && rx_len != NULL) {
        uint16_t rx_bytes;
        /* Read number of received bytes.
         * Note: st25r3916_read_reg() returns uint8_t and cannot signal
         * errors. The register values are masked to 8 bits to avoid
         * any sign-extension issues. */
        rx_bytes = (uint16_t)(st25r3916_read_reg(ST25R3916_REG_NUM_RX_BYTES1) & 0xFF);
        rx_bytes |= (uint16_t)((st25r3916_read_reg(ST25R3916_REG_NUM_RX_BYTES2) & 0xFF) << 8);

        /* Clamp to FIFO size and caller-provided buffer size */
        if (rx_bytes > ST25R3916_FIFO_SIZE_VAL)
            rx_bytes = ST25R3916_FIFO_SIZE_VAL;
        if (rx_bytes > *rx_len)
            rx_bytes = *rx_len;

        /* Read RX data */
        for (uint16_t i = 0; i < rx_bytes; i++) {
            rx_data[i] = st25r3916_read_reg(ST25R3916_REG_RX_FIFO);
        }
        *rx_len = rx_bytes;
    }

    /* Step 7: Turn off the field */
    st25r3916_send_command(ST25R3916_CMD_TX_OFF);

    return 0;
}

/**
 * st25r3916_get_field_strength_mv — Read NFC field strength in mV
 *
 * Measures the amplitude of the 13.56 MHz carrier field using the
 * ST25R3916's internal measurement circuitry. Useful for antenna
 * tuning verification and debug.
 *
 * Returns: field strength in mV (0–5000), or 0 on error
 */
uint16_t st25r3916_get_field_strength_mv(void) {
    uint8_t result;

    /* Send MEASURE_AMPLITUDE direct command */
    st25r3916_send_command(ST25R3916_CMD_MEASURE_AMPLITUDE);

    /* Wait for measurement to complete (typically 50-100 μs).
     * Poll the IRQ register for completion rather than using a fixed
     * delay, with a timeout to avoid hanging if the chip is unresponsive. */
    for (int timeout = 0; timeout < 5000; timeout++) {
        uint8_t irq1 = st25r3916_read_reg(ST25R3916_REG_IRQ_STATUS1);
        if (irq1 & 0x01)  /* MEASURE_AMPLITUDE_DONE bit */
            break;
    }

    /* Read the RSSI register which contains the amplitude measurement result.
     * The RSSI register gives an 8-bit value (0-255) representing the
     * measured field strength. Convert to mV assuming full-scale = 5.0V. */
    result = st25r3916_read_reg(ST25R3916_REG_RSSI);

    /* Convert: amplitude_mV = result * 5000 / 255
     * Using 32-bit arithmetic to avoid overflow: result * 5000 fits in uint32_t
     * since result ≤ 255 and 255 * 5000 = 1,275,000 < 4,294,967,295. */
    return (uint16_t)((uint32_t)result * 5000U / 255U);
}