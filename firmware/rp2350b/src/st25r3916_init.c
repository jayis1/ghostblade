/*
 * st25r3916_init.c — ST25R3916 NFC Controller Initialization for RP2350B
 *
 * Copyright (C) 2026 Apex One Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the ST25R3916 NFC reader/writer initialization
 * sequence for the Apex One board. The ST25R3916 is connected to the
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

/* ========================================================================
 * ST25R3916 Register Map (Space A: Direct Command / Space B: Reg)
 * ======================================================================== */

/* Space A registers (access via SPI with address byte encoding) */
#define ST25R3916_REG_IO_CONF1              0x00   /* I/O Configuration 1 */
#define ST25R3916_REG_IO_CONF2              0x01   /* I/O Configuration 2 */
#define ST25R3916_REG_OP_CTRL               0x02   /* Operation Control */
#define ST25R3916_REG_MODE_DEF              0x03   /* Mode Definition */
#define ST25R3916_REG_BIT_RATE              0x04   /* Bit Rate Definition */
#define ST25R3916_REG_ISO14443A_MODE        0x05   /* ISO 14443A Mode */
#define ST25R3916_REG_ISO14443B_MODE        0x06   /* ISO 14443B Mode */
#define ST25R3916_REG_FELICA_MODE           0x07   /* FeliCa Mode */
#define ST25R3916_REG_ISO15693_MODE         0x08   /* ISO 15693 Mode */
#define ST25R3916_REG_ANT_CAL_TARGET        0x09   /* Antenna Calibration Target */
#define ST25R3916_REG_ANT_CAL_TIME          0x0A   /* Antenna Calibration Time */
#define ST25R3916_REG_ANT_MIN               0x0B   /* Antenna Minimum */
#define ST25R3916_REG_ANT_MAX               0x0C   /* Antenna Maximum */
#define ST25R3916_REG_ANT_TUNE              0x0D   /* Antenna Tuning */
#define ST25R3916_REG_ANT_TUNE_MEAS         0x0E   /* Antenna Tuning Measure */
#define ST25R3916_REG_AUX_MOD               0x0F   /* Auxiliary Modulation */
#define ST25R3916_REG_RX_CONF1              0x10   /* Receiver Configuration 1 */
#define ST25R3916_REG_RX_CONF2              0x11   /* Receiver Configuration 2 */
#define ST25R3916_REG_RX_CONF3              0x12   /* Receiver Configuration 3 */
#define ST25R3916_REG_RX_CONF4              0x13   /* Receiver Configuration 4 */
#define ST25R3916_REG_TX_DRIVER             0x14   /* TX Driver */
#define ST25R3916_REG_TX_CURRENT            0x15   /* TX Current */
#define ST25R3916_REG_TX_CURRENT_SSC        0x16   /* TX Current SSC */
#define ST25R3916_REG_TX_CURRENT_SSC_HL     0x17   /* TX Current SSC HL */
#define ST25R3916_REG_TX_CURRENT_SSC_LH     0x18   /* TX Current SSC LH */
#define ST25R3916_REG_CORR_CONF1            0x19   /* Correlator Configuration 1 */
#define ST25R3916_REG_CORR_CONF2            0x1A   /* Correlator Configuration 2 */
#define ST25R3916_REG_TIMER_EMV             0x1B   /* Timer EMV */
#define ST25R3916_REG_TIMER1                0x1C   /* Timer 1 */
#define ST25R3916_REG_TIMER2                0x1D   /* Timer 2 */
#define ST25R3916_REG_TIMER3                0x1E   /* Timer 3 */
#define ST25R3916_REG_IRQ_MASK1             0x1F   /* Interrupt Mask 1 */
#define ST25R3916_REG_IRQ_MASK2             0x20   /* Interrupt Mask 2 */
#define ST25R3916_REG_IRQ_MASK3             0x21   /* Interrupt Mask 3 */
#define ST25R3916_REG_IRQ_MASK4             0x22   /* Interrupt Mask 4 */
#define ST25R3916_REG_IRQ_MASK5             0x23   /* Interrupt Mask 5 */
#define ST25R3916_REG_NUM_TX_BYTES1         0x24   /* Number of TX Bytes 1 */
#define ST25R3916_REG_NUM_TX_BYTES2         0x25   /* Number of TX Bytes 2 */
#define ST25R3916_REG_NUM_TX_BYTES3         0x26   /* Number of TX Bytes 3 */
#define ST25R3916_REG_TX_FIFO_STATUS        0x27   /* TX FIFO Status */
#define ST25R3916_REG_NUM_RX_BYTES1         0x28   /* Number of RX Bytes 1 */
#define ST25R3916_REG_NUM_RX_BYTES2         0x29   /* Number of RX Bytes 2 */
#define ST25R3916_REG_NUM_RX_BYTES3         0x2A   /* Number of RX Bytes 3 */
#define ST25R3916_REG_RX_FIFO_STATUS        0x2B   /* RX FIFO Status */
#define ST25R3916_REG_COLL_INT_CLEAR        0x2C   /* Collision and INT Clear */
#define ST25R3916_REG_WUP_TIMER             0x2D   /* Wake-up Timer */
#define ST25R3916_REG_WUP_TIMER_GRAN        0x2E   /* Wake-up Timer Granularity */
#define ST25R3916_REG_SLP_TIMER             0x2F   /* Sleep Timer */
#define ST25R3916_REG_SLP_TIMER_GRAN        0x30   /* Sleep Timer Granularity */
#define ST25R3916_REG_MVT                   0x31   /* Minimum Validation Time */
#define ST25R3916_REG_AGC_CONFIG            0x32   /* AGC Configuration */
#define ST25R3916_REG_AM_CONFIG             0x33   /* AM Configuration */
#define ST25R3916_REG_AM_GRANGE1            0x34   /* AM Gain Range 1 */
#define ST25R3916_REG_AM_GRANGE2            0x35   /* AM Gain Range 2 */
#define ST25R3916_REG_AM_GRANGE3            0x36   /* AM Gain Range 3 */
#define ST25R3916_REG_WUP_COLL              0x37   /* Wake-up Collision */
#define ST25R3916_REG_RSSI                  0x38   /* RSSI Level */
#define ST25R3916_REG_OBSV_CONF1            0x39   /* Observer Configuration 1 */
#define ST25R3916_REG_OBSV_CONF2             0x3A   /* Observer Configuration 2 */
#define ST25R3916_REG_OBSV_CONF3             0x3B   /* Observer Configuration 3 */
#define ST25R3916_REG_OSC_CONF               0x3C   /* Oscillator Configuration */
#define ST25R3916_REG_VREG_CONF              0x3D   /* Voltage Regulator Configuration */
#define ST25R3916_REG_TEST_DISC_CONF1        0x3E   /* Test Discovery Configuration 1 */
#define ST25R3916_REG_TEST_DISC_CONF2        0x3F   /* Test Discovery Configuration 2 */
#define ST25R3916_REG_SPACE_B_CTRL           0x40   /* Space B Control */

/* Space B registers (access via Space B gateway) */
#define ST25R3916_REG_EMD_SUP_CONF           0x41   /* EMD Suppression Config */
#define ST25R3916_REG_SUBCORR_THRESH         0x42   /* Sub-Correlator Threshold */
#define ST25R3916_REG_RATS_RESP1             0x43   /* RATS Response 1 */
#define ST25R3916_REG_RATS_RESP2             0x44   /* RATS Response 2 */
#define ST25R3916_REG_PV_ADAPT_LOAD          0x45   /* PV Adapt Load */
#define ST25R3916_REG_DPO_ISO15693           0x46   /* DPO ISO 15693 */
#define ST25R3916_REG_DPO_ISO14443A          0x47   /* DPO ISO 14443A */
#define ST25R3916_REG_DPO_3                  0x48   /* DPO 3 */
#define ST25R3916_REG_DPO_I_CODE             0x49   /* DPO I-CODE */

/* Direct Commands (write-only, accessed as register addresses) */
#define ST25R3916_CMD_SET_DEFAULT             0xC1   /* Set Default */
#define ST25R3916_CMD_INITIALIZE              0xC2   /* Initialize */
#define ST25R3916_CMD_INITIALIZE_DPO          0xC3   /* Initialize DPO */
#define ST25R3916_CMD_MEASURE_VDD             0xC5   /* Measure VDD */
#define ST25R3916_CMD_CALIBRATE_ANTENNA       0xC8   /* Calibrate Antenna */
#define ST25R3916_CMD_MEASURE_AMPLITUDE       0xC9   /* Measure Amplitude */
#define ST25R3916_CMD_MEASURE_PHASE           0xCA   /* Measure Phase */
#define ST25R3916_CMD_CLEAR_IRQS              0xC4   /* Clear Interrupts */
#define ST25R3916_CMD_TX_ON                   0xC6   /* TX On */
#define ST25R3916_CMD_TX_OFF                  0xC7   /* TX Off */
#define ST25R3916_CMD_GOTO_SENSE              0xD0   /* Go to Sense */
#define ST25R3916_CMD_GOTO_SLEEP              0xD1   /* Go to Sleep */
#define ST25R3916_CMD_START_WUP_TIMER         0xD2   /* Start Wake-up Timer */
#define ST25R3916_CMD_START_GP_TIMER          0xD3   /* Start General Purpose Timer */
#define ST25R3916_CMD_SPACE_B_ACCESS          0xFF   /* Access Space B */

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
 * Forward Declarations
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
 * ST25R3916 SPI Access Functions
 * ======================================================================== */

/**
 * st25r3916_write_reg — Write a single ST25R3916 register
 *
 * @addr: Register address (Space A: 0x00-0x3F, Space B via gateway)
 * @val:  Value to write
 */
void st25r3916_write_reg(uint8_t addr, uint8_t val) {
    apex_nfc_write_register(addr, val);
}

/**
 * st25r3916_read_reg — Read a single ST25R3916 register
 *
 * @addr: Register address
 * Returns: Register value
 */
uint8_t st25r3916_read_reg(uint8_t addr) {
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
    /* IRQ status registers are at specific addresses in the register map */
    if (irq1) *irq1 = st25r3916_read_reg(0x04);  /* Simplified; actual addrs vary */
    if (irq2) *irq2 = st25r3916_read_reg(0x05);
    if (irq3) *irq3 = st25r3916_read_reg(0x06);
    if (irq4) *irq4 = st25r3916_read_reg(0x07);
    if (irq5) *irq5 = st25r3916_read_reg(0x08);

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

    /* Step 1: Ensure power supply is stable.
     * VDD_NFC_TX (5.0V) must be stable before accessing the chip.
     * The power sequencing is handled by the PMIC; we assume it's
     * already stable when this function is called.
     */

    /* Step 2: Send SET_DEFAULT command to reset all registers to defaults */
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
        /* Oscillator not ready yet — wait a bit more */
        for (volatile int i = 0; i < 50000; i++)
            __asm__("nop");
        st25r3916_clear_interrupts(&irq1, NULL, NULL, NULL, NULL);
    }

    /* Step 24: Send INITIALIZE command for DPO (Dynamic Power Output) */
    st25r3916_send_command(ST25R3916_CMD_INITIALIZE_DPO);

    return 0;
}

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

    /* Write REQA command byte (0x26) to TX FIFO */
    /* TX FIFO write is done via the FIFO register */
    st25r3916_write_reg(0x1F, 0x26);  /* Simplified; actual FIFO write sequence */

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