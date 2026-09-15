/*
 * es8388_driver.h — ES8388 Audio Codec Driver API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Controls the ES8388 stereo audio codec on the GhostBlade board.
 * The ES8388 is connected to the RP2350B via I2C0 (addr 0x11) for
 * register control and to the RK3576 via I2S for audio data.
 *
 * The RP2350B manages:
 *   - Codec power sequencing (enable/disable via PMIC)
 *   - Mute/unmute (for PTT walkie-talkie mode)
 *   - Input source selection (MEMS mic PDM vs. line-in)
 *   - Volume control (host-commanded via SPI protocol)
 *   - PTT GPIO assertion to LMS7002M TX enable
 *
 * The RK3576 handles all I2S audio data (mic capture → encode → TX,
 * RX decode → DAC playback). The RP2350B does NOT touch I2S.
 *
 * I2C address: 0x11 (ADDR pin tied low)
 * Reference: ES8388 Datasheet Rev 2.1
 */

#ifndef ES8388_DRIVER_H
#define ES8388_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * ES8388 I2C Address
 * ======================================================================== */

#define ES8388_I2C_ADDR         0x11    /* ADDR pin LOW → address 0x11 */

/* ========================================================================
 * ES8388 Register Map (partial — used by this driver)
 * ======================================================================== */

#define ES8388_CHIP_CTRL1       0x00    /* Chip control 1 */
#define ES8388_CHIP_CTRL2       0x01    /* Chip control 2 (power down control) */
#define ES8388_CHIP_POWER       0x02    /* Chip power management */
#define ES8388_ADC_POWER        0x03    /* ADC power management */
#define ES8388_DAC_POWER        0x04    /* DAC power management */
#define ES8388_CHIP_LOPWR       0x05    /* Chip low-power mode */
#define ES8388_CHIP_MONO        0x06    /* Mono mix */
#define ES8388_MASTER_MODE      0x08    /* Master/slave mode */
#define ES8388_ADC_CTRL1        0x09    /* ADC control 1 (input select) */
#define ES8388_ADC_CTRL2        0x0A    /* ADC control 2 */
#define ES8388_ADC_CTRL3        0x0B    /* ADC control 3 */
#define ES8388_ADC_CTRL4        0x0C    /* ADC control 4 (I2S format) */
#define ES8388_ADC_CTRL5        0x0D    /* ADC control 5 (MCLK divider) */
#define ES8388_ADC_CTRL6        0x0E    /* ADC control 6 */
#define ES8388_ADC_CTRL7        0x0F    /* ADC control 7 */
#define ES8388_ADC_CTRL8        0x10    /* ADC control 8 (left PGA gain) */
#define ES8388_ADC_CTRL9        0x11    /* ADC control 9 (right PGA gain) */
#define ES8388_ADC_CTRL10       0x12    /* ADC control 10 (ALC) */
#define ES8388_ADC_CTRL14       0x16    /* ADC control 14 */
#define ES8388_DAC_CTRL1        0x17    /* DAC control 1 */
#define ES8388_DAC_CTRL2        0x18    /* DAC control 2 */
#define ES8388_DAC_CTRL3        0x19    /* DAC control 3 (mute) */
#define ES8388_DAC_CTRL4        0x1A    /* DAC control 4 (left vol) */
#define ES8388_DAC_CTRL5        0x1B    /* DAC control 5 (right vol) */
#define ES8388_DAC_CTRL6        0x1C    /* DAC control 6 */
#define ES8388_DAC_CTRL7        0x1D    /* DAC control 7 */
#define ES8388_DAC_CTRL8        0x1E    /* DAC control 8 */
#define ES8388_DAC_CTRL16       0x26    /* DAC control 16 (LIN/RIN mix) */
#define ES8388_DAC_CTRL17       0x27    /* DAC control 17 (LOUT1 vol) */
#define ES8388_DAC_CTRL20       0x2A    /* DAC control 20 (ROUT1 vol) */
#define ES8388_DAC_CTRL21       0x2B    /* DAC control 21 (LOUT2 vol) */
#define ES8388_DAC_CTRL24       0x2E    /* DAC control 24 (ROUT2 vol) */
#define ES8388_DAC_CTRL26       0x30    /* DAC control 26 */
#define ES8388_DAC_CTRL27       0x31    /* DAC control 27 */

/* DAC_CTRL3 mute bit */
#define ES8388_DAC_MUTE_BIT     (1 << 2)

/* CHIP_CTRL2 power-down bit */
#define ES8388_PDWN_BIT         (1 << 0)

/* ========================================================================
 * PTT (Push-To-Talk) State
 * ======================================================================== */

/** PTT mode: which radio link is active for voice TX */
typedef enum {
    ES8388_PTT_OFF = 0,     /* Not transmitting */
    ES8388_PTT_SDR,         /* TX via LMS7002M (any frequency) */
    ES8388_PTT_CC1101,      /* TX via CC1101 sub-GHz */
    ES8388_PTT_WIFI,        /* TX via Wi-Fi 6E (VoIP/Mumble) */
    ES8388_PTT_BT,          /* TX via Bluetooth SCO/HFP */
} es8388_ptt_mode_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * es8388_init — Initialize the ES8388 codec
 *
 * Configures the codec for:
 *   - I2S slave mode (RK3576 is I2S master)
 *   - MEMS PDM microphone input on ADC left/right channels
 *   - DAC output to dual 1W speakers (LOUT1/ROUT1)
 *   - Default volume: 0 dB output, +24 dB mic PGA gain
 *   - DAC muted at startup (unmuted when host enables audio)
 *
 * Returns 0 on success, negative on I2C error.
 */
int es8388_init(void);

/**
 * es8388_set_mute — Mute or unmute the DAC output
 *
 * Mutes/unmutes both LOUT1 and ROUT1 speaker outputs.
 * Called by the PTT logic to enforce TX/RX half-duplex on
 * CC1101/SDR links (full-duplex on Wi-Fi/BT).
 *
 * @mute: true = mute speakers, false = unmute
 * Returns 0 on success, negative on I2C error.
 */
int es8388_set_mute(bool mute);

/**
 * es8388_set_volume — Set DAC output volume
 *
 * @vol_db: Volume in dB, range -96 to 0 (0 = full scale)
 * Returns 0 on success, negative on I2C error.
 */
int es8388_set_volume(int8_t vol_db);

/**
 * es8388_set_mic_gain — Set ADC/PGA microphone gain
 *
 * @gain_db: PGA gain in dB, 0–24 dB in 3 dB steps.
 *           Clamped to 24 dB maximum.
 * Returns 0 on success, negative on I2C error.
 */
int es8388_set_mic_gain(uint8_t gain_db);

/**
 * es8388_ptt_set — Assert or release push-to-talk
 *
 * Controls the PTT GPIO to the LMS7002M TX enable pin (SDR mode)
 * and notifies the host via INT_REQ for Wi-Fi/BT PTT modes.
 * Automatically mutes the speaker during SDR/CC1101 TX to prevent
 * RF feedback through the MEMS microphone.
 *
 * @mode:   Which radio link to use for voice TX
 * @active: true = PTT pressed (start TX), false = PTT released (stop TX)
 * Returns 0 on success, negative on error.
 */
int es8388_ptt_set(es8388_ptt_mode_t mode, bool active);

/**
 * es8388_ptt_get_mode — Get current PTT state
 *
 * Returns the currently active PTT mode, or ES8388_PTT_OFF if idle.
 */
es8388_ptt_mode_t es8388_ptt_get_mode(void);

/**
 * es8388_power_down — Power down the codec
 *
 * Mutes outputs and asserts PDWN to put the ES8388 into
 * low-power standby. Called during system sleep.
 * Returns 0 on success, negative on I2C error.
 */
int es8388_power_down(void);

/**
 * es8388_power_up — Wake the codec from standby
 *
 * Releases PDWN and re-applies the active configuration.
 * Returns 0 on success, negative on I2C error.
 */
int es8388_power_up(void);

#endif /* ES8388_DRIVER_H */
