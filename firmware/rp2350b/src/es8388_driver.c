/*
 * es8388_driver.c — ES8388 Audio Codec Driver
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Controls the ES8388 stereo codec via I2C0 on the RP2350B.
 * The ES8388 provides:
 *   - ADC: MEMS PDM microphone capture (front-facing mic array)
 *   - DAC: dual 1W speaker output (LOUT1/ROUT1)
 *   - PTT coordination with LMS7002M TX enable for SDR voice TX
 *
 * Audio data path (NOT handled here — managed by RK3576 Linux kernel):
 *   MIC → ES8388 ADC → I2S → RK3576 → encode → GNU Radio → LMS7002M TX
 *   LMS7002M RX → GNU Radio → decode → RK3576 → I2S → ES8388 DAC → speaker
 *
 * This driver handles only: power, mute, volume, gain, and PTT signalling.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "es8388_driver.h"
#include "board_pins.h"

/* ─── Pico SDK headers ────────────────────────────────────────────────── */
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

/* ========================================================================
 * I2C helper
 * ======================================================================== */

static int es8388_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    int ret = i2c_write_blocking(i2c0, ES8388_I2C_ADDR, buf, 2, false);
    if (ret < 0) {
        printf("ES8388: I2C write reg 0x%02X failed (%d)\r\n", reg, ret);
        return -1;
    }
    return 0;
}

static int es8388_read_reg(uint8_t reg, uint8_t *val)
{
    int ret = i2c_write_blocking(i2c0, ES8388_I2C_ADDR, &reg, 1, true);
    if (ret < 0)
        return -1;
    ret = i2c_read_blocking(i2c0, ES8388_I2C_ADDR, val, 1, false);
    if (ret < 0)
        return -1;
    return 0;
}

/* ========================================================================
 * Module state
 * ======================================================================== */

static struct {
    bool             initialized;
    bool             powered_down;
    bool             dac_muted;
    int8_t           vol_db;           /* Current DAC volume (-96 to 0) */
    uint8_t          mic_gain_db;      /* Current PGA gain (0–24 dB) */
    es8388_ptt_mode_t ptt_mode;        /* Active PTT mode */
} g_es8388 = {
    .initialized  = false,
    .powered_down = false,
    .dac_muted    = true,
    .vol_db       = 0,
    .mic_gain_db  = 24,
    .ptt_mode     = ES8388_PTT_OFF,
};

/* ========================================================================
 * Initialization
 * ======================================================================== */

int es8388_init(void)
{
    int ret;

    /* ── I2C0 peripheral setup (shared with ES8388; NFC uses I2C1) ── */
    i2c_init(i2c0, 400000);  /* 400 kHz Fast Mode */
    gpio_set_function(PIN_AUDIO_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_AUDIO_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_AUDIO_I2C_SDA);
    gpio_pull_up(PIN_AUDIO_I2C_SCL);

    /* ── PTT GPIO: drives LMS7002M SDR_GPIO0 (TX enable) ── */
    gpio_init(PIN_AUDIO_PTT);
    gpio_set_dir(PIN_AUDIO_PTT, GPIO_OUT);
    gpio_put(PIN_AUDIO_PTT, 0);  /* PTT released (RX mode) */

    /* ── ES8388 register initialization sequence ─────────────────── */

    /* 1. Release power-down, enable ref/bias */
    ret  = es8388_write_reg(ES8388_CHIP_CTRL2,  0x00);  /* No power-down */
    ret |= es8388_write_reg(ES8388_CHIP_POWER,  0x00);  /* Enable all power */
    ret |= es8388_write_reg(ES8388_CHIP_LOPWR,  0x00);  /* Disable low-power */

    /* 2. Set slave mode (RK3576 I2S is master) */
    ret |= es8388_write_reg(ES8388_MASTER_MODE, 0x00);  /* I2S slave */

    /* 3. ADC: select differential LINPUT1/RINPUT1 (MEMS mic via PDM conv.) */
    ret |= es8388_write_reg(ES8388_ADC_CTRL1,   0x00);  /* Lin/Rin sel: LINPUT1/RINPUT1 */
    ret |= es8388_write_reg(ES8388_ADC_CTRL2,   0x00);  /* Differential */
    ret |= es8388_write_reg(ES8388_ADC_CTRL4,   0x0C);  /* I2S 16-bit, left-justified */
    ret |= es8388_write_reg(ES8388_ADC_CTRL5,   0x02);  /* MCLK/4 */
    ret |= es8388_write_reg(ES8388_ADC_CTRL8,   0x20);  /* Left PGA: +24 dB */
    ret |= es8388_write_reg(ES8388_ADC_CTRL9,   0x20);  /* Right PGA: +24 dB */
    ret |= es8388_write_reg(ES8388_ADC_POWER,   0x00);  /* Power on ADC/PGA/MIC */

    /* 4. DAC: I2S 16-bit, output to LOUT1/ROUT1 speakers */
    ret |= es8388_write_reg(ES8388_DAC_CTRL1,   0x18);  /* I2S 16-bit */
    ret |= es8388_write_reg(ES8388_DAC_CTRL2,   0x02);  /* MCLK/4 */
    ret |= es8388_write_reg(ES8388_DAC_CTRL3,   ES8388_DAC_MUTE_BIT);  /* Start muted */
    ret |= es8388_write_reg(ES8388_DAC_CTRL4,   0x00);  /* Left vol: 0 dB */
    ret |= es8388_write_reg(ES8388_DAC_CTRL5,   0x00);  /* Right vol: 0 dB */

    /* 5. Mixer: route DAC to LOUT1/ROUT1 (1W speakers) */
    ret |= es8388_write_reg(ES8388_DAC_CTRL17,  0xB8);  /* LOUT1 from LDAC, vol 0 dB */
    ret |= es8388_write_reg(ES8388_DAC_CTRL20,  0xB8);  /* ROUT1 from RDAC, vol 0 dB */
    ret |= es8388_write_reg(ES8388_DAC_POWER,   0x3C);  /* Enable LOUT1/ROUT1 */

    if (ret != 0) {
        printf("ES8388: init failed — I2C error\r\n");
        return -1;
    }

    g_es8388.initialized  = true;
    g_es8388.dac_muted    = true;
    g_es8388.vol_db       = 0;
    g_es8388.mic_gain_db  = 24;

    printf("ES8388: initialized (I2S slave, MEMS mic, 1W×2 speakers, DAC muted)\r\n");
    return 0;
}

/* ========================================================================
 * Mute / Volume / Gain
 * ======================================================================== */

int es8388_set_mute(bool mute)
{
    if (!g_es8388.initialized)
        return -1;

    uint8_t val = mute ? ES8388_DAC_MUTE_BIT : 0x00;
    int ret = es8388_write_reg(ES8388_DAC_CTRL3, val);
    if (ret == 0)
        g_es8388.dac_muted = mute;
    return ret;
}

int es8388_set_volume(int8_t vol_db)
{
    if (!g_es8388.initialized)
        return -1;

    /* ES8388 DAC volume: 0x00 = 0 dB, 0xC0 = -96 dB, 1 step = 0.5 dB */
    if (vol_db > 0) vol_db = 0;
    if (vol_db < -96) vol_db = -96;

    uint8_t reg_val = (uint8_t)((-vol_db) * 2);  /* 0 dB → 0x00, -96 dB → 0xC0 */
    int ret  = es8388_write_reg(ES8388_DAC_CTRL4, reg_val);
    ret     |= es8388_write_reg(ES8388_DAC_CTRL5, reg_val);
    if (ret == 0)
        g_es8388.vol_db = vol_db;
    return ret;
}

int es8388_set_mic_gain(uint8_t gain_db)
{
    if (!g_es8388.initialized)
        return -1;

    /* PGA gain: 0 dB=0x00, 3 dB=0x04, 6 dB=0x08 … 24 dB=0x20 (3 dB steps) */
    if (gain_db > 24) gain_db = 24;
    uint8_t reg_val = (gain_db / 3) << 2;
    int ret  = es8388_write_reg(ES8388_ADC_CTRL8, reg_val);
    ret     |= es8388_write_reg(ES8388_ADC_CTRL9, reg_val);
    if (ret == 0)
        g_es8388.mic_gain_db = gain_db;
    return ret;
}

/* ========================================================================
 * Push-To-Talk
 * ======================================================================== */

int es8388_ptt_set(es8388_ptt_mode_t mode, bool active)
{
    if (!g_es8388.initialized)
        return -1;

    if (active) {
        /* Starting TX */
        g_es8388.ptt_mode = mode;

        if (mode == ES8388_PTT_SDR || mode == ES8388_PTT_CC1101) {
            /* Mute speaker to prevent acoustic feedback into the mic
             * during RF transmission on half-duplex links */
            es8388_set_mute(true);

            if (mode == ES8388_PTT_SDR) {
                /* Assert TX enable to LMS7002M via dedicated PTT GPIO
                 * The Linux host handles the actual I2S audio routing
                 * and GNU Radio SDR TX pipeline — we just gate the RF */
                gpio_put(PIN_AUDIO_PTT, 1);
                /* Debug only: suppress per-event printf in production to
                 * avoid UART flooding during walkie-talkie PTT use */
#ifdef APEX_DEBUG_AUDIO
                printf("ES8388: PTT active — SDR TX (LMS7002M TX enable asserted)\r\n");
#endif
            } else {
#ifdef APEX_DEBUG_AUDIO
                printf("ES8388: PTT active — CC1101 sub-GHz TX\r\n");
#endif
            }
        } else {
            /* Wi-Fi and BT are full-duplex — speaker stays on */
#ifdef APEX_DEBUG_AUDIO
            printf("ES8388: PTT active — %s (full-duplex)\r\n",
                   mode == ES8388_PTT_WIFI ? "Wi-Fi 6E VoIP" : "Bluetooth SCO/HFP");
#endif
        }
    } else {
        /* Releasing TX — return to RX */
        if (g_es8388.ptt_mode == ES8388_PTT_SDR) {
            gpio_put(PIN_AUDIO_PTT, 0);  /* Release LMS7002M TX enable */
        }

        /* Unmute speaker for RX audio playback */
        es8388_set_mute(false);

#ifdef APEX_DEBUG_AUDIO
        printf("ES8388: PTT released — RX mode\r\n");
#endif
        g_es8388.ptt_mode = ES8388_PTT_OFF;
    }

    return 0;
}

es8388_ptt_mode_t es8388_ptt_get_mode(void)
{
    return g_es8388.ptt_mode;
}

/* ========================================================================
 * Power Management
 * ======================================================================== */

int es8388_power_down(void)
{
    if (!g_es8388.initialized)
        return 0;

    es8388_set_mute(true);
    int ret = es8388_write_reg(ES8388_CHIP_CTRL2, ES8388_PDWN_BIT);
    if (ret == 0)
        g_es8388.powered_down = true;
    return ret;
}

int es8388_power_up(void)
{
    if (!g_es8388.initialized)
        return es8388_init();

    int ret = es8388_write_reg(ES8388_CHIP_CTRL2, 0x00);  /* Release PDWN */
    if (ret == 0) {
        g_es8388.powered_down = false;
        /* Re-apply mute state */
        es8388_set_mute(g_es8388.dac_muted);
    }
    return ret;
}
