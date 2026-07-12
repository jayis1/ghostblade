<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# Hardware Bring-Up Checklist — GhostBlade (Project NullSpectre)

This document provides a step-by-step checklist for bringing up a newly
manufactured GhostBlade board. Follow these steps in order to verify
power sequencing, clock initialization, peripheral connectivity, and
firmware operation.

## Table of Contents

- [Pre-Power Visual Inspection](#pre-power-visual-inspection)
- [Power Rail Verification](#power-rail-verification)
- [Clock and Reset Verification](#clock-and-reset-verification)
- [RK3576 Boot Verification](#rk3576-boot-verification)
- [RP2350B Boot Verification](#rp2350b-boot-verification)
- [SDR (LMS7002M) Verification](#sdr-lms7002m-verification)
- [NFC (ST25R3916) Verification](#nfc-st25r3916-verification)
- [Sub-GHz (CC1101) Verification](#sub-ghz-cc1101-verification)
- [Wi-Fi (MT7922) Verification](#wi-fi-mt7922-verification)
- [SPI Bridge Verification](#spi-bridge-verification)
- [ADC Calibration](#adc-calibration)
- [Full System Test](#full-system-test)

---

## Pre-Power Visual Inspection

Before applying power, inspect the board for manufacturing defects:

- [ ] **No solder bridges** between IC pins (check under magnification for
  RK3576 BGA, RP2350B QFN, LMS7002M QFN)
- [ ] **No tombstoned or misaligned passive components** (resistors, capacitors)
- [ ] **All polarized components correctly oriented** (electrolytic capacitors,
  diodes, USB-C connector)
- [ ] **No missing components** (compare against BOM)
- [ ] **Battery connector polarity** matches silkscreen markings
- [ ] **USB-C connector** is fully seated with no bent pins
- [ ] **SMA connectors** (J2, J3) are straight and soldered on all four
  ground tabs
- [ ] **u.FL connector** (J4) is centered and not tilted
- [ ] **NFC antenna connector** (J5) is secure
- [ ] **All test points** (TP1–TP30) have proper solder fillets
- [ ] **No copper exposure** on inner layers (check edge of board)
- [ ] **Board dimensions** match specification (verify with calipers if needed)
- [ ] **Silkscreen legible** — all reference designators and test point labels
  are readable

---

## Power Rail Verification

Connect a bench power supply to the battery connector (JST-PH 2-pin) or
USB-C port. **Do NOT insert a Li-Po battery yet** — use a current-limited
supply set to 3.7V with 100mA current limit initially.

### Step 1: PMIC and Core Rails

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 1.1 | Apply 3.7V to VBAT connector (100mA limit) | Current < 50mA (no short) | [ ] |
| 1.2 | Measure TP1 (VCC_SYS) | 3.4V ±0.17V | [ ] |
| 1.3 | Measure TP2 (VCC_1V8_SYS) | 1.8V ±0.054V | [ ] |
| 1.4 | Measure TP3 (VCC_1V0_CORE) | 1.0V ±0.03V | [ ] |
| 1.5 | Measure TP4 (VCC_3V3_SYS) | 3.3V ±0.099V | [ ] |
| 1.6 | Increase current limit to 3A | No overcurrent trip | [ ] |

### Step 2: RP2350B and Peripheral Rails

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 2.1 | Measure TP5 (VCC_3V3_MCU) | 3.3V ±0.099V | [ ] |
| 2.2 | Measure TP6 (VCC_1V1_MCU) | 1.1V ±0.033V | [ ] |
| 2.3 | Press BOOTSEL button, measure TP22 (MCU_RUNn) | < 0.1V | [ ] |
| 2.4 | Release BOOTSEL, measure TP22 (MCU_RUNn) | > 3.0V | [ ] |

### Step 3: SDR Power Rails (After firmware init)

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 3.1 | Enable SDR 1V8 rail, measure TP7 (VCC_3V3_SDR) | 3.3V ±0.099V | [ ] |
| 3.2 | Enable SDR 1V8 rail, measure TP8 (VCC_1V8_SDR) | 1.8V ±0.054V | [ ] |
| 3.3 | Enable SDR 1V1 rail, measure TP9 (VCC_1V2_SDR) | 1.2V ±0.036V | [ ] |
| 3.4 | Verify SDR rail sequencing on oscilloscope | 1V8 → 1V1 → 3V3, 5ms between each | [ ] |

### Step 4: NFC and Sub-GHz Rails

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 4.1 | Enable NFC rail, measure TP10 (VCC_3V3_NFC) | 3.3V ±0.099V | [ ] |
| 4.2 | Enable Sub-GHz rail, measure TP11 (VCC_3V3_RADIO) | 3.3V ±0.099V | [ ] |

### Step 5: Current Consumption

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 5.1 | Measure standby current (all rails off) | < 5mA at 3.7V | [ ] |
| 5.2 | Measure idle current (all rails on, no RF) | 50-150mA at 3.7V | [ ] |
| 5.3 | Measure SDR streaming current | 300-500mA at 3.7V | [ ] |
| 5.4 | Measure NFC active current (field on) | 150-300mA at 3.7V | [ ] |

---

## Clock and Reset Verification

Use an oscilloscope with a 10× probe on the test points.

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 6.1 | Probe TP13 (SPI0_CLK) during RK3576 boot | 21.4 MHz clock present after boot | [ ] |
| 6.2 | Probe TP22 (MCU_RUNn) at power-on | Low for ≥ 1ms, then high | [ ] |
| 6.3 | Verify RP2350B crystal oscillator (if external) | 12 MHz ±50 ppm | [ ] |
| 6.4 | Probe TP18 (LMS_RSTn) after SDR power-on | High after ≥ 10ms from rail stable | [ ] |
| 6.5 | Verify LMS7002M reference clock (via SPI read) | 30.72 MHz PLL locked | [ ] |
| 6.6 | Verify ST25R3916 oscillator after reset | 27.12 MHz ±50 ppm after 1ms | [ ] |

---

## RK3576 Boot Verification

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 7.1 | Connect USB-C to host computer (debug UART) | USB-serial device appears (/dev/ttyUSB0) | [ ] |
| 7.2 | Open serial console at 1500000 baud | U-Boot boot messages visible | [ ] |
| 7.3 | Wait for Linux kernel boot | Kernel log visible on console | [ ] |
| 7.4 | Check `dmesg` for RK3576 SoC identification | "Rockchip RK3576" in dmesg | [ ] |
| 7.5 | Verify LPDDR5 initialization | "DDR5" or "LPDDR5" in boot log | [ ] |
| 7.6 | Verify eMMC/NVMe root filesystem mounts | Root filesystem mounted read-write | [ ] |

---

## RP2350B Boot Verification

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 8.1 | Check RP2350B debug UART (TP23/TP24) | "GhostBlade RP2350B firmware vX.Y.Z" banner | [ ] |
| 8.2 | Verify SPI0 slave initialization | "SPI0 slave ready" in UART log | [ ] |
| 8.3 | Verify watchdog timer started | "Watchdog started, timeout=5000ms" | [ ] |
| 8.4 | Verify battery monitor reading | "Battery: XXXX mV (XX%)" | [ ] |
| 8.5 | Verify peripheral power sequencing | All rails powered on in order | [ ] |
| 8.6 | Check HOST_RDY signal (TP18) | HOST_RDY asserted (high) within 120ms | [ ] |

---

## SDR (LMS7002M) Verification

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 9.1 | Read LMS7002M chip ID via SPI | 0x3880 (default ID) | [ ] |
| 9.2 | Read LMS7002M revision register | Non-zero revision number | [ ] |
| 9.3 | Configure LMS7002M for RX at 868 MHz | "SDR tuned to 868000000 Hz" in log | [ ] |
| 9.4 | Verify LMS7002M PLL lock status | PLL locked bit = 1 | [ ] |
| 9.5 | Start SDR streaming (256 ksps) | IQ data flowing on SPI0 | [ ] |
| 9.6 | Verify IQ data rate matches configured sample rate | Measured rate within ±1% | [ ] |
| 9.7 | Check SDR DMA ring buffer | No overflows on Core 1 | [ ] |
| 9.8 | Connect SMA antenna to J3 (RX port) | Noise floor < -90 dBm | [ ] |

---

## NFC (ST25R3916) Verification

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 10.1 | Read ST25R3916 chip ID register | IC type code = 0x01 | [ ] |
| 10.2 | Verify ST25R3916 oscillator running | OSC bit in interrupt register set | [ ] |
| 10.3 | Send REQA (ISO 14443A) | No tag expected (field only) | [ ] |
| 10.4 | Place ISO 14443A tag near antenna | Tag UID detected | [ ] |
| 10.5 | Read tag NDEF or data | Correct data read | [ ] |
| 10.6 | Test anticollision with 2+ tags | Multiple UIDs detected | [ ] |
| 10.7 | Verify NFC IRQ (TP21) fires on tag detection | Interrupt observed | [ ] |

---

## Sub-GHz (CC1101) Verification

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 11.1 | Read CC1101 PARTNUM register | 0x00 (expected part number) | [ ] |
| 11.2 | Read CC1101 VERSION register | 0x14 or higher | [ ] |
| 11.3 | Configure CC1101 for 868 MHz GFSK | "CC1101 configured" in log | [ ] |
| 11.4 | Verify CC1101 crystal oscillator | MISO goes high after CSn | [ ] |
| 11.5 | Transmit test packet (loopback) | CRC pass on received packet | [ ] |
| 11.6 | Check CC1101 RSSI reading | < -80 dBm (no signal) or higher with signal | [ ] |

---

## Wi-Fi (MT7922) Verification

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 12.1 | Check MT7922 on SDIO bus | "mt7921s" in dmesg | [ ] |
| 12.2 | Bring up WLAN interface | `ip link set wlan0 up` succeeds | [ ] |
| 12.3 | Scan for Wi-Fi networks | `iw dev wlan0 scan` returns SSIDs | [ ] |
| 12.4 | Connect to a test AP | Association and DHCP succeed | [ ] |
| 12.5 | Run iperf3 throughput test | > 100 Mbps TCP downlink | [ ] |

---

## SPI Bridge Verification

The SPI bridge is the primary communication path between the RK3576
(host) and the RP2350B (MCU). This test verifies the full protocol stack.

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 13.1 | Open `/dev/apex_bridge` device node | Open succeeds | [ ] |
| 13.2 | Send APEX_CMD_NOP command | Response: status OK, CRC valid | [ ] |
| 13.3 | Send APEX_CMD_TELEMETRY_REQ | Response: firmware version, battery, temp | [ ] |
| 13.4 | Verify telemetry CRC-64 | Computed CRC matches received CRC | [ ] |
| 13.5 | Send APEX_CMD_SDR_TUNE (868 MHz) | Response: PLL locked, status OK | [ ] |
| 13.6 | Start SDR stream (APEX_CMD_SDR_STREAM) | IQ data flowing via read() | [ ] |
| 13.7 | Verify IQ data format (I16Q16) | Correct byte order, valid range | [ ] |
| 13.8 | Send APEX_CMD_ANT_SELECT (MIMO_RX) | Antenna switch confirmed | [ ] |
| 13.9 | Verify INT_REQ interrupt line | GPIO interrupt fires on data ready | [ ] |
| 13.10 | Test maximum payload (4092 bytes) | Transfer succeeds, CRC valid | [ ] |
| 13.11 | Test zero-length payload | Response: status OK | [ ] |
| 13.12 | Test CRC error injection (flip 1 bit) | Response: CRC_ERROR status | [ ] |

---

## ADC Calibration

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 14.1 | Run ADC self-test (`adc_cal_self_test`) | Returns 0 (all tests pass) | [ ] |
| 14.2 | Read VREF from internal reference | 3250-3350 mV (3.3V ±1.5%) | [ ] |
| 14.3 | Read battery voltage (no battery) | 0 mV or < 2800 mV | [ ] |
| 14.4 | Connect 3.600V precision supply | Reading within ±2% (3528-3672 mV) | [ ] |
| 14.5 | Run factory calibration | Returns 0 (success) | [ ] |
| 14.6 | Verify calibration stored in flash | Re-read: coefficients non-default | [ ] |
| 14.7 | Power cycle and verify persistence | Calibration still valid after reboot | [ ] |
| 14.8 | Read temperature sensor | Plausible room temperature (18-28°C) | [ ] |

---

## Full System Test

Run this test after all individual subsystems have been verified.

| Step | Action | Expected | Pass? |
|------|--------|----------|-------|
| 15.1 | Power on with Li-Po battery | All rails sequence correctly | [ ] |
| 15.2 | Boot RK3576 + RP2350B firmware | Both processors boot successfully | [ ] |
| 15.3 | Run `apex_selftest` userspace tool | All tests pass | [ ] |
| 15.4 | Stream SDR IQ at 2 Msps for 60s | No DMA overflow, no CRC errors | [ ] |
| 15.5 | Stream SDR IQ at 4.096 Msps for 30s | No DMA overflow | [ ] |
| 15.6 | Read NFC tag while SDR streaming | Both subsystems work simultaneously | [ ] |
| 15.7 | Transmit CC1101 while SDR streaming | No SPI bus contention | [ ] |
| 15.8 | Run all firmware unit tests | All tests pass | [ ] |
| 15.9 | Monitor battery voltage during load | Voltage stays above 3.0V under load | [ ] |
| 15.10 | Verify watchdog reset (force hang) | Watchdog resets MCU within 6s | [ ] |
| 15.11 | Verify sleep/wake cycle | MCU enters light sleep after idle timeout | [ ] |
| 15.12 | Run overnight soak test (8 hours) | No hangs, no memory leaks, no CRC errors | [ ] |

---

## Test Equipment Required

| Equipment | Purpose | Minimum Specification |
|-----------|---------|----------------------|
| Bench power supply | Power rail verification | 0-5V, 0-3A, current measurement ±1mA |
| Digital multimeter (2×) | Voltage measurements | ±0.1% accuracy, 0.1mV resolution |
| Oscilloscope (4-channel) | Timing and signal verification | 200 MHz bandwidth, 1 Gsps |
| Logic analyzer (16-channel) | SPI protocol debugging | 100 MHz sampling, SPI decode |
| Precision voltage source | ADC calibration | 3.0-4.2V, ±0.01% accuracy |
| Spectrum analyzer | SDR and RF verification | 10 kHz - 6 GHz, -110 dBm DANL |
| NFC tag set | NFC verification | ISO 14443A, 14443B, 15693 |
| USB-serial adapter | Debug console | 1500000 baud support |
| JTAG/SWD debugger | RP2350B firmware debug | SWD, 2-wire |

---

*Document version: 1.0 — 2026-06-23*