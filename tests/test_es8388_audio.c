/*
 * test_es8388_audio.c — Unit Tests for ES8388 Audio Codec Logic
 *
 * Copyright (C) 2026 jayis1
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests for ES8388 driver logic and the SPI audio command dispatch path.
 * These are host-buildable userspace tests (no hardware required).
 *
 * The tests verify:
 *   - Volume register encoding (dB → ES8388 reg value)
 *   - Mic PGA gain encoding (dB → reg value in 3 dB steps)
 *   - PTT mode enum range validation
 *   - SPI audio command payload parsing (AUDIO_VOLUME, AUDIO_MIC_GAIN,
 *     AUDIO_PTT) including short-payload rejection
 *   - PTT mode boundary conditions (valid modes 0–4, reject ≥5)
 *   - Volume clamping at -96 dB and 0 dB
 *   - Gain clamping at 24 dB maximum
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -o test_es8388_audio test_es8388_audio.c
 *
 * Run:
 *   ./test_es8388_audio
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int g_tests_run  = 0;
static int g_tests_pass = 0;
static int g_tests_fail = 0;

#define ASSERT_EQ(a, b) do {                                        \
    g_tests_run++;                                                  \
    if ((a) == (b)) {                                               \
        g_tests_pass++;                                             \
    } else {                                                        \
        g_tests_fail++;                                             \
        printf("FAIL [%s:%d] %s == %s  (%d != %d)\n",              \
               __FILE__, __LINE__, #a, #b, (int)(a), (int)(b));    \
    }                                                               \
} while (0)

#define ASSERT_TRUE(x) do {                                         \
    g_tests_run++;                                                  \
    if (x) {                                                        \
        g_tests_pass++;                                             \
    } else {                                                        \
        g_tests_fail++;                                             \
        printf("FAIL [%s:%d] expected true: %s\n",                  \
               __FILE__, __LINE__, #x);                             \
    }                                                               \
} while (0)

#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))

/* ========================================================================
 * ES8388 Volume Register Encoding
 *
 * ES8388 DAC volume register:
 *   0x00 =   0 dB (full scale)
 *   0x02 =  -1 dB
 *   0x04 =  -2 dB
 *   ...  (1 step = 0.5 dB)
 *   0xC0 = -96 dB
 *
 * Formula: reg = (-vol_db) * 2, clamped to [0, 0xC0]
 * ======================================================================== */

static uint8_t es8388_vol_encode(int8_t vol_db)
{
    if (vol_db > 0)   vol_db = 0;
    if (vol_db < -96) vol_db = -96;
    return (uint8_t)((-vol_db) * 2);
}

static void test_volume_encoding(void)
{
    printf("Test: Volume register encoding\n");

    /* 0 dB → reg 0x00 */
    ASSERT_EQ(es8388_vol_encode(0),    0x00);

    /* -1 dB → reg 0x02 */
    ASSERT_EQ(es8388_vol_encode(-1),   0x02);

    /* -6 dB → reg 0x0C */
    ASSERT_EQ(es8388_vol_encode(-6),   0x0C);

    /* -20 dB → reg 0x28 */
    ASSERT_EQ(es8388_vol_encode(-20),  0x28);

    /* -48 dB → reg 0x60 */
    ASSERT_EQ(es8388_vol_encode(-48),  0x60);

    /* -96 dB → reg 0xC0 (maximum attenuation) */
    ASSERT_EQ(es8388_vol_encode(-96),  0xC0);

    /* Positive values clamped to 0 dB */
    ASSERT_EQ(es8388_vol_encode(1),    0x00);
    ASSERT_EQ(es8388_vol_encode(10),   0x00);
    ASSERT_EQ(es8388_vol_encode(127),  0x00);

    /* Below -96 dB clamped to -96 dB */
    ASSERT_EQ(es8388_vol_encode(-97),  0xC0);
    ASSERT_EQ(es8388_vol_encode(-128), 0xC0);
}

/* ========================================================================
 * ES8388 PGA Gain Register Encoding
 *
 * ES8388 ADC PGA gain register (ADC_CTRL8 / ADC_CTRL9):
 *   0x00 = 0 dB
 *   0x04 = 3 dB
 *   0x08 = 6 dB
 *   ...  (1 register unit = 3 dB)
 *   0x20 = 24 dB (maximum)
 *
 * Formula: reg = (gain_db / 3) << 2, clamped to max 24 dB
 * ======================================================================== */

static uint8_t es8388_gain_encode(uint8_t gain_db)
{
    if (gain_db > 24) gain_db = 24;
    return (uint8_t)((gain_db / 3u) << 2);
}

static void test_gain_encoding(void)
{
    printf("Test: PGA gain register encoding\n");

    /* 0 dB → 0x00 */
    ASSERT_EQ(es8388_gain_encode(0),   0x00);

    /* 3 dB → 0x04 */
    ASSERT_EQ(es8388_gain_encode(3),   0x04);

    /* 6 dB → 0x08 */
    ASSERT_EQ(es8388_gain_encode(6),   0x08);

    /* 12 dB → 0x10 */
    ASSERT_EQ(es8388_gain_encode(12),  0x10);

    /* 18 dB → 0x18 */
    ASSERT_EQ(es8388_gain_encode(18),  0x18);

    /* 24 dB → 0x20 (maximum, per ES8388 datasheet) */
    ASSERT_EQ(es8388_gain_encode(24),  0x20);

    /* Values above 24 dB clamped to 24 dB */
    ASSERT_EQ(es8388_gain_encode(25),  0x20);
    ASSERT_EQ(es8388_gain_encode(30),  0x20);
    ASSERT_EQ(es8388_gain_encode(255), 0x20);

    /* Intermediate values: 9 dB → 0x0C */
    ASSERT_EQ(es8388_gain_encode(9),   0x0C);

    /* Non-multiple of 3 rounds down: 11 dB → floor(11/3)=3 steps → 0x0C */
    ASSERT_EQ(es8388_gain_encode(11),  0x0C);
    /* 14 dB → floor(14/3)=4 steps → 0x10 */
    ASSERT_EQ(es8388_gain_encode(14),  0x10);
}

/* ========================================================================
 * PTT Mode Validation
 *
 * Valid modes: 0 (OFF), 1 (SDR), 2 (CC1101), 3 (Wi-Fi), 4 (Bluetooth)
 * Invalid: 5 and above
 * ======================================================================== */

#define PTT_MODE_OFF     0
#define PTT_MODE_SDR     1
#define PTT_MODE_CC1101  2
#define PTT_MODE_WIFI    3
#define PTT_MODE_BT      4
#define PTT_MODE_MAX     PTT_MODE_BT

static bool ptt_mode_is_valid(uint8_t mode)
{
    return mode <= PTT_MODE_MAX;
}

static void test_ptt_mode_validation(void)
{
    printf("Test: PTT mode range validation\n");

    /* All valid modes */
    ASSERT_TRUE(ptt_mode_is_valid(PTT_MODE_OFF));
    ASSERT_TRUE(ptt_mode_is_valid(PTT_MODE_SDR));
    ASSERT_TRUE(ptt_mode_is_valid(PTT_MODE_CC1101));
    ASSERT_TRUE(ptt_mode_is_valid(PTT_MODE_WIFI));
    ASSERT_TRUE(ptt_mode_is_valid(PTT_MODE_BT));

    /* Boundary: just above max is invalid */
    ASSERT_FALSE(ptt_mode_is_valid(PTT_MODE_MAX + 1));
    ASSERT_FALSE(ptt_mode_is_valid(5));
    ASSERT_FALSE(ptt_mode_is_valid(10));
    ASSERT_FALSE(ptt_mode_is_valid(0xFF));
}

/* ========================================================================
 * SPI Audio Command Payload Parsing
 *
 * Simulate the dispatch logic from spi_protocol.c without needing
 * the full firmware environment.
 * ======================================================================== */

/* Simulated dispatch state */
static struct {
    int8_t  vol_db;          /* Last received volume */
    uint8_t mic_gain_db;     /* Last received mic gain (clamped) */
    uint8_t ptt_mode;        /* Last received PTT mode */
    bool    ptt_active;      /* Last received PTT active flag */
    int     error_count;     /* Short-payload or invalid-mode errors */
    int     vol_calls;
    int     mic_gain_calls;
    int     ptt_calls;
} sim = {0};

static void sim_reset(void)
{
    memset(&sim, 0, sizeof(sim));
}

/* Simulate handle_cmd_audio_volume */
static int sim_handle_audio_volume(const uint8_t *payload, uint16_t len)
{
    if (len < 1) { sim.error_count++; return -1; }
    int8_t vol_db = (int8_t)payload[0];
    if (vol_db > 0)   vol_db = 0;
    if (vol_db < -96) vol_db = -96;
    sim.vol_db = vol_db;
    sim.vol_calls++;
    return 0;
}

/* Simulate handle_cmd_audio_mic_gain */
static int sim_handle_audio_mic_gain(const uint8_t *payload, uint16_t len)
{
    if (len < 1) { sim.error_count++; return -1; }
    uint8_t gain_db = payload[0];
    if (gain_db > 24) gain_db = 24;
    sim.mic_gain_db = gain_db;
    sim.mic_gain_calls++;
    return 0;
}

/* Simulate handle_cmd_audio_ptt */
static int sim_handle_audio_ptt(const uint8_t *payload, uint16_t len)
{
    if (len < 2) { sim.error_count++; return -1; }
    uint8_t mode_raw = payload[0];
    if (!ptt_mode_is_valid(mode_raw)) { sim.error_count++; return -1; }
    sim.ptt_mode   = mode_raw;
    sim.ptt_active = (payload[1] != 0);
    sim.ptt_calls++;
    return 0;
}

static void test_audio_volume_dispatch(void)
{
    printf("Test: AUDIO_VOLUME command dispatch\n");

    uint8_t payload[2];

    /* Normal: set -20 dB */
    sim_reset();
    payload[0] = (uint8_t)(int8_t)-20;  /* twos-complement: 0xEC */
    ASSERT_EQ(sim_handle_audio_volume(payload, 1), 0);
    ASSERT_EQ(sim.vol_db, -20);
    ASSERT_EQ(sim.vol_calls, 1);
    ASSERT_EQ(sim.error_count, 0);

    /* Normal: set 0 dB */
    sim_reset();
    payload[0] = 0;
    ASSERT_EQ(sim_handle_audio_volume(payload, 1), 0);
    ASSERT_EQ(sim.vol_db, 0);

    /* Normal: set -96 dB */
    sim_reset();
    payload[0] = (uint8_t)(int8_t)-96;  /* 0xA0 */
    ASSERT_EQ(sim_handle_audio_volume(payload, 1), 0);
    ASSERT_EQ(sim.vol_db, -96);

    /* Clamping: +5 dB → should be clamped to 0 dB */
    sim_reset();
    payload[0] = 5;
    ASSERT_EQ(sim_handle_audio_volume(payload, 1), 0);
    ASSERT_EQ(sim.vol_db, 0);

    /* Short payload: reject */
    sim_reset();
    ASSERT_EQ(sim_handle_audio_volume(payload, 0), -1);
    ASSERT_EQ(sim.error_count, 1);
    ASSERT_EQ(sim.vol_calls, 0);

    /* Extra payload bytes: valid — only byte 0 matters */
    sim_reset();
    payload[0] = (uint8_t)(int8_t)-6;
    payload[1] = 0xFF;  /* ignored */
    ASSERT_EQ(sim_handle_audio_volume(payload, 2), 0);
    ASSERT_EQ(sim.vol_db, -6);
}

static void test_audio_mic_gain_dispatch(void)
{
    printf("Test: AUDIO_MIC_GAIN command dispatch\n");

    uint8_t payload[2];

    /* Normal: 24 dB */
    sim_reset();
    payload[0] = 24;
    ASSERT_EQ(sim_handle_audio_mic_gain(payload, 1), 0);
    ASSERT_EQ(sim.mic_gain_db, 24);
    ASSERT_EQ(sim.mic_gain_calls, 1);

    /* Normal: 0 dB */
    sim_reset();
    payload[0] = 0;
    ASSERT_EQ(sim_handle_audio_mic_gain(payload, 1), 0);
    ASSERT_EQ(sim.mic_gain_db, 0);

    /* Clamp: 30 dB → clamped to 24 */
    sim_reset();
    payload[0] = 30;
    ASSERT_EQ(sim_handle_audio_mic_gain(payload, 1), 0);
    ASSERT_EQ(sim.mic_gain_db, 24);

    /* Clamp: 255 → clamped to 24 */
    sim_reset();
    payload[0] = 255;
    ASSERT_EQ(sim_handle_audio_mic_gain(payload, 1), 0);
    ASSERT_EQ(sim.mic_gain_db, 24);

    /* Short payload: reject */
    sim_reset();
    ASSERT_EQ(sim_handle_audio_mic_gain(payload, 0), -1);
    ASSERT_EQ(sim.error_count, 1);
    ASSERT_EQ(sim.mic_gain_calls, 0);
}

static void test_audio_ptt_dispatch(void)
{
    printf("Test: AUDIO_PTT command dispatch\n");

    uint8_t payload[2];

    /* PTT press, SDR mode */
    sim_reset();
    payload[0] = PTT_MODE_SDR;
    payload[1] = 0x01;  /* active */
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), 0);
    ASSERT_EQ(sim.ptt_mode, PTT_MODE_SDR);
    ASSERT_TRUE(sim.ptt_active);
    ASSERT_EQ(sim.ptt_calls, 1);

    /* PTT release */
    sim_reset();
    payload[0] = PTT_MODE_SDR;
    payload[1] = 0x00;  /* released */
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), 0);
    ASSERT_FALSE(sim.ptt_active);

    /* CC1101 mode */
    sim_reset();
    payload[0] = PTT_MODE_CC1101;
    payload[1] = 0x01;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), 0);
    ASSERT_EQ(sim.ptt_mode, PTT_MODE_CC1101);

    /* Wi-Fi VoIP mode */
    sim_reset();
    payload[0] = PTT_MODE_WIFI;
    payload[1] = 0x01;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), 0);
    ASSERT_EQ(sim.ptt_mode, PTT_MODE_WIFI);

    /* Bluetooth SCO mode */
    sim_reset();
    payload[0] = PTT_MODE_BT;
    payload[1] = 0x01;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), 0);
    ASSERT_EQ(sim.ptt_mode, PTT_MODE_BT);

    /* PTT_OFF: valid mode, can be used to force-release */
    sim_reset();
    payload[0] = PTT_MODE_OFF;
    payload[1] = 0x00;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), 0);
    ASSERT_EQ(sim.ptt_mode, PTT_MODE_OFF);

    /* Invalid mode 5: must be rejected */
    sim_reset();
    payload[0] = 5;
    payload[1] = 0x01;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), -1);
    ASSERT_EQ(sim.error_count, 1);
    ASSERT_EQ(sim.ptt_calls, 0);

    /* Invalid mode 0xFF: must be rejected */
    sim_reset();
    payload[0] = 0xFF;
    payload[1] = 0x01;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), -1);
    ASSERT_EQ(sim.error_count, 1);

    /* Short payload (1 byte): reject */
    sim_reset();
    payload[0] = PTT_MODE_SDR;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 1), -1);
    ASSERT_EQ(sim.error_count, 1);
    ASSERT_EQ(sim.ptt_calls, 0);

    /* Zero-length payload: reject */
    sim_reset();
    ASSERT_EQ(sim_handle_audio_ptt(payload, 0), -1);
    ASSERT_EQ(sim.error_count, 1);

    /* Non-zero active flag (e.g., 0xFF) is treated as active=true */
    sim_reset();
    payload[0] = PTT_MODE_SDR;
    payload[1] = 0xFF;
    ASSERT_EQ(sim_handle_audio_ptt(payload, 2), 0);
    ASSERT_TRUE(sim.ptt_active);
}

/* ========================================================================
 * PTT half-duplex mute semantics
 *
 * Verify the expected mute/unmute behavior from the spec:
 *   - SDR/CC1101 TX: speaker muted
 *   - SDR/CC1101 release: speaker unmuted
 *   - Wi-Fi/BT TX: speaker NOT muted (full-duplex)
 * ======================================================================== */

static bool sim_speaker_muted = false;

static bool ptt_requires_mute(uint8_t mode, bool active)
{
    if (!active)
        return false;  /* Release always unmutes */
    return (mode == PTT_MODE_SDR || mode == PTT_MODE_CC1101);
}

static void test_ptt_mute_semantics(void)
{
    printf("Test: PTT mute semantics for half-duplex TX\n");

    /* SDR TX: mute speaker */
    sim_speaker_muted = ptt_requires_mute(PTT_MODE_SDR, true);
    ASSERT_TRUE(sim_speaker_muted);

    /* CC1101 TX: mute speaker */
    sim_speaker_muted = ptt_requires_mute(PTT_MODE_CC1101, true);
    ASSERT_TRUE(sim_speaker_muted);

    /* Wi-Fi TX: do NOT mute speaker (full-duplex) */
    sim_speaker_muted = ptt_requires_mute(PTT_MODE_WIFI, true);
    ASSERT_FALSE(sim_speaker_muted);

    /* BT TX: do NOT mute speaker (full-duplex) */
    sim_speaker_muted = ptt_requires_mute(PTT_MODE_BT, true);
    ASSERT_FALSE(sim_speaker_muted);

    /* PTT release (any mode): do not mute */
    ASSERT_FALSE(ptt_requires_mute(PTT_MODE_SDR, false));
    ASSERT_FALSE(ptt_requires_mute(PTT_MODE_CC1101, false));
    ASSERT_FALSE(ptt_requires_mute(PTT_MODE_OFF, false));
}

/* ========================================================================
 * Volume encode / decode round-trip
 * ======================================================================== */

static void test_volume_roundtrip(void)
{
    printf("Test: Volume encode round-trip across valid range\n");

    /* Walk every 1 dB step from -96 to 0 */
    for (int v = -96; v <= 0; v++) {
        uint8_t reg = es8388_vol_encode((int8_t)v);
        /* reg / 2 should recover the absolute dB value */
        int recovered = -(int)(reg / 2);
        ASSERT_EQ(recovered, v);
    }
}

/* ========================================================================
 * Gain encode / decode round-trip for 3 dB multiples
 * ======================================================================== */

static void test_gain_roundtrip(void)
{
    printf("Test: Gain encode round-trip for 3 dB multiples\n");

    /* Only 3 dB multiples round-trip exactly */
    for (uint8_t g = 0; g <= 24; g += 3) {
        uint8_t reg = es8388_gain_encode(g);
        uint8_t recovered = (uint8_t)((reg >> 2) * 3);
        ASSERT_EQ(recovered, g);
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("==============================================\n");
    printf(" GhostBlade ES8388 Audio Codec Unit Tests\n");
    printf("==============================================\n\n");

    test_volume_encoding();
    test_gain_encoding();
    test_ptt_mode_validation();
    test_audio_volume_dispatch();
    test_audio_mic_gain_dispatch();
    test_audio_ptt_dispatch();
    test_ptt_mute_semantics();
    test_volume_roundtrip();
    test_gain_roundtrip();

    printf("\n----------------------------------------------\n");
    printf("Results: %d/%d passed", g_tests_pass, g_tests_run);
    if (g_tests_fail > 0) {
        printf(", %d FAILED\n", g_tests_fail);
    } else {
        printf(" — all OK\n");
    }
    printf("----------------------------------------------\n");

    return (g_tests_fail == 0) ? 0 : 1;
}
