<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# GhostBlade Audio Subsystem

## Overview

The GhostBlade board includes a full walkie-talkie audio path built around the
ES8388 stereo audio codec, two digital MEMS microphones, and dual 1-watt
speakers. This document describes the hardware architecture, firmware driver,
SPI command interface, and PTT (Push-To-Talk) flow for all four voice radio
modes.

## Hardware Architecture

```
MEMS Mic Array (front-facing, PDM)
       |
       v
  ES8388 ADC (I2C ctrl: RP2350B, I2S data: RK3576 I2S2)
       |  ^
       |  |  I2S master clock: MCLK_OUT2 (RK3576)
       v  |
  ES8388 DAC
       |
  1W + 1W stereo speakers (LOUT1 / ROUT1)
```

The RP2350B controls the ES8388 codec via I2C0 (400 kHz, address 0x11) for:
- Power sequencing (CHIP_CTRL2 PDWN bit)
- DAC mute/unmute (DAC_CTRL3 SOFTMT bit)
- Output volume (DAC_CTRL4/5, 0.5 dB steps, -96 to 0 dB)
- Microphone PGA gain (ADC_CTRL8/9, 3 dB steps, 0 to 24 dB)
- PTT GPIO assertion to LMS7002M TX enable (PIN_AUDIO_PTT, GPIO 6)

The RK3576 handles all I2S audio data:
- Microphone capture → Linux ALSA driver → encode → GNU Radio pipeline → TX
- RX → GNU Radio decode → Linux ALSA driver → ES8388 DAC → speakers

The RP2350B does NOT interact with I2S. Its role is control-plane only.

## Pins

| Signal            | RP2350B Pin | Function                            |
|-------------------|-------------|-------------------------------------|
| AUDIO_I2C_SDA     | 4           | I2C0 SDA → ES8388 control           |
| AUDIO_I2C_SCL     | 5           | I2C0 SCL → ES8388 control           |
| AUDIO_PTT         | 6           | PTT → LMS7002M SDR_GPIO0 TX enable  |

ES8388 ADDR pin is tied low → I2C address 0x11.

## ES8388 Initialization Sequence

The RP2350B calls `es8388_init()` during board startup (Step 9 in `main.c`).
The init sequence follows the ES8388 datasheet Rev 2.1 recommended power-up
procedure:

1. Release power-down (CHIP_CTRL2 = 0x00)
2. Enable reference and bias (CHIP_POWER = 0x00, CHIP_LOPWR = 0x00)
3. Set I2S slave mode (MASTER_MODE = 0x00 — RK3576 is I2S master)
4. Configure ADC for MEMS microphone:
   - Differential LINPUT1/RINPUT1 (PDM front-end)
   - I2S 16-bit left-justified (ADC_CTRL4 = 0x0C)
   - MCLK/4 clock (ADC_CTRL5 = 0x02)
   - +24 dB PGA gain on both channels (ADC_CTRL8/9 = 0x20)
5. Configure DAC for speaker output:
   - I2S 16-bit (DAC_CTRL1 = 0x18)
   - MCLK/4 (DAC_CTRL2 = 0x02)
   - Start muted (DAC_CTRL3 = SOFTMT bit)
   - 0 dB volume (DAC_CTRL4/5 = 0x00)
6. Route DAC to LOUT1/ROUT1 (DAC_CTRL17/20 = 0xB8, DAC_POWER = 0x3C)

If `es8388_init()` fails (I2C NAK, no codec on bus), the board continues
operating without audio. The codec is a non-critical subsystem — SDR, NFC,
CC1101, and Wi-Fi functions are unaffected.

## SPI Command Interface

The RK3576 host can control the audio codec via three SPI bridge commands:

### CMD_AUDIO_VOLUME (0x08)

Set the ES8388 DAC output volume.

Payload: 1 byte (signed int8, dB).
- Range: -96 to 0 dB (0 = full scale, -96 = minimum)
- Positive values are clamped to 0 dB
- Values below -96 are clamped to -96 dB
- Register encoding: `reg = (-vol_db) × 2` (1 step = 0.5 dB per ES8388 spec)

Example: set volume to -20 dB
```
Payload: 0xEC  (twos-complement of -20)
```

### CMD_AUDIO_MIC_GAIN (0x09)

Set the ES8388 ADC/PGA microphone gain.

Payload: 1 byte (unsigned uint8, dB).
- Range: 0 to 24 dB in 3 dB steps
- Values above 24 are clamped to 24 dB
- Non-multiples of 3 are rounded down to the nearest 3 dB step
- Register encoding: `reg = (gain_db / 3) << 2` (per ES8388 ADC_CTRL8/9 spec)

Example: set PGA gain to 18 dB
```
Payload: 0x12  (18 decimal)
```

### CMD_AUDIO_PTT (0x0A)

Assert or release Push-To-Talk for voice transmission.

Payload: 2 bytes.
- Byte 0: PTT mode (uint8):
  - 0 = off (force-release / no mode)
  - 1 = SDR (LMS7002M, any frequency 100 kHz–3.8 GHz)
  - 2 = CC1101 (sub-GHz OOK/FSK voice)
  - 3 = Wi-Fi 6E (VoIP / Mumble / SIP)
  - 4 = Bluetooth (SCO/HFP headset)
  - 5–255 = INVALID (rejected, error counter incremented)
- Byte 1: active flag (0x01 = PTT pressed / TX, 0x00 = PTT released / RX)

On PTT active (byte 1 = 1):
- SDR mode: speaker muted (prevent acoustic feedback during TX); PTT GPIO
  asserted to LMS7002M TX enable pin. RK3576 GNU Radio pipeline handles
  actual I2S mic capture → SDR TX.
- CC1101 mode: speaker muted. CC1101 modulation is configured separately
  via CMD_CC1101_CFG.
- Wi-Fi / BT modes: speaker stays active (full-duplex links).

On PTT release (byte 1 = 0):
- LMS7002M TX enable deasserted (SDR mode).
- Speaker unmuted for RX audio playback.
- PTT mode returns to ES8388_PTT_OFF.

## PTT Half-Duplex Flow (SDR Voice)

```
Host                          RP2350B                   Hardware
  |                              |                          |
  |-- CMD_AUDIO_PTT(SDR, TX) --> |                          |
  |                              |-- mute speaker --------> |
  |                              |-- assert AUDIO_PTT ----> LMS7002M TX_EN
  |                              |                          |
  |  (host drives I2S TX via GNU Radio SDR pipeline)        |
  |                              |                          |
  |-- CMD_AUDIO_PTT(SDR, RX) --> |                          |
  |                              |-- deassert AUDIO_PTT --> LMS7002M TX_EN
  |                              |-- unmute speaker ------> |
  |                              |                          |
```

## Power Management

`es8388_power_down()` is called during system sleep (low-power mode):
- Mutes the DAC output
- Asserts CHIP_CTRL2 PDWN bit to put the codec in standby (~0.5 µA)

`es8388_power_up()` is called on wake:
- Releases PDWN
- Re-applies the last mute state

## ES8388 Register Summary (registers used by this driver)

| Register     | Address | Usage                                  |
|-------------|---------|----------------------------------------|
| CHIP_CTRL1  | 0x00    | Chip control (not written in init)     |
| CHIP_CTRL2  | 0x01    | PDWN bit — power management            |
| CHIP_POWER  | 0x02    | Enable all power blocks                |
| ADC_POWER   | 0x03    | ADC/PGA/MIC bias power enable          |
| DAC_POWER   | 0x04    | LOUT1/ROUT1 output amplifier enable    |
| CHIP_LOPWR  | 0x05    | Low-power mode (disabled)              |
| MASTER_MODE | 0x08    | I2S master/slave (slave = 0x00)        |
| ADC_CTRL1   | 0x09    | Input select (LINPUT1/RINPUT1)         |
| ADC_CTRL2   | 0x0A    | Differential input mode                |
| ADC_CTRL4   | 0x0C    | I2S format (16-bit, left-justified)    |
| ADC_CTRL5   | 0x0D    | MCLK divider (MCLK/4)                  |
| ADC_CTRL8   | 0x10    | Left PGA gain (0–24 dB)                |
| ADC_CTRL9   | 0x11    | Right PGA gain (0–24 dB)               |
| DAC_CTRL1   | 0x17    | I2S format (16-bit)                    |
| DAC_CTRL2   | 0x18    | MCLK divider                           |
| DAC_CTRL3   | 0x19    | SOFTMT — DAC mute control              |
| DAC_CTRL4   | 0x1A    | Left DAC volume (0.5 dB/step)          |
| DAC_CTRL5   | 0x1B    | Right DAC volume (0.5 dB/step)         |
| DAC_CTRL17  | 0x27    | LOUT1 mixer (route from LDAC)          |
| DAC_CTRL20  | 0x2A    | ROUT1 mixer (route from RDAC)          |

Reference: ES8388 Datasheet Rev 2.1 (Everest Semi).

## Unit Tests

The test suite at `tests/test_es8388_audio.c` covers:
- Volume register encoding and round-trip (-96 to 0 dB, every 1 dB step)
- PGA gain encoding and round-trip (0, 3, 6, 9, 12, 15, 18, 21, 24 dB)
- PTT mode validation (valid modes 0–4, reject 5–255)
- SPI audio command dispatch: AUDIO_VOLUME, AUDIO_MIC_GAIN, AUDIO_PTT
- Short-payload rejection for all three commands
- Volume clamping at boundaries
- Gain clamping at 24 dB maximum
- PTT half-duplex mute logic: SDR/CC1101 mutes speaker; Wi-Fi/BT does not

Build and run:
```bash
cd tests
make test_es8388_audio
./test_es8388_audio
# Expected: 198/198 passed — all OK
```

## Known Limitations

- I2S audio routing is handled entirely by the RK3576 Linux ALSA subsystem.
  The RP2350B driver code has been compile-tested only; hardware verification
  requires a populated board with the ES8388 assembled.
- ALC (Automatic Level Control) is not configured by this driver. Noise gate
  and ALC parameters (ADC_CTRL10–ADC_CTRL14) are left at power-on defaults.
  Tuning these registers for the specific MEMS microphone characteristics
  requires audio measurements on real hardware.
- Echo cancellation for half-duplex SDR voice requires a DSP pipeline on the
  RK3576 (e.g., WebRTC AEC in PulseAudio/PipeWire). This is outside the
  scope of the RP2350B codec driver.
