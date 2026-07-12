<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade — Hardware Test Procedures

**Project:** GhostBlade (Project NullSpectre)  
**Revision:** 1.0  
**Date:** 2026-06-14  
**License:** CC-BY-SA 4.0  

---

## Table of Contents

1. [Test Equipment Required](#1-test-equipment-required)
2. [Visual Inspection](#2-visual-inspection)
3. [Power-On Smoke Test](#3-power-on-smoke-test)
4. [Power Rail Verification](#4-power-rail-verification)
5. [RK3576 Boot Test](#5-rk3576-boot-test)
6. [RP2350B SPI Bridge Test](#6-rp2350b-spi-bridge-test)
7. [SDR (LMS7002M) Functional Test](#7-sdr-lms7002m-functional-test)
8. [Sub-GHz (CC1101) Functional Test](#8-sub-ghz-cc1101-functional-test)
9. [NFC (ST25R3916) Functional Test](#9-nfc-st25r3916-functional-test)
10. [Wi-Fi 6E / BT 5.4 (MT7922) Test](#10-wi-fi-6e--bt-54-mt7922-test)
11. [LPDDR5 Memory Test](#11-lpddr5-memory-test)
12. [eMMC Storage Test](#12-emmc-storage-test)
13. [NVMe SSD Test](#13-nvme-ssd-test)
14. [USB-C Test](#14-usb-c-test)
15. [Thermal Validation](#15-thermal-validation)
16. [RF Shielding Verification](#16-rf-shielding-verification)
17. [Pass/Fail Criteria](#17-passfail-criteria)

---

## 1. Test Equipment Required

| Equipment | Model (Suggested) | Purpose |
|-----------|-------------------|---------|
| Digital Multimeter | Fluke 87V | Voltage, current, continuity |
| Oscilloscope | Keysight DSOX3024T (4ch, 200MHz) | Signal integrity, timing |
| Logic Analyzer | Saleae Logic Pro 16 | SPI, I2C, UART protocol decode |
| Spectrum Analyzer | Rigol DSA815 (9kHz-1.5GHz) | SDR output verification |
| RF Signal Generator | Siglent SSG5040X (up to 4GHz) | SDR stimulus |
| Vector Network Analyzer | NanoVNA V2 (50kHz-3GHz) | Antenna matching |
| DC Power Supply | Keysight E36312A | Bench power (0-6V, 0-5A) |
| Thermal Camera | FLIR E8 Pro | Thermal validation |
| JTAG/SWD Probe | Segger J-Link Plus | RP2350B + RK3576 debug |
| MicroSD Card | 32GB Class 10 | Boot media |
| NFC Tag | ISO 14443 Type A (NTAG215) | NFC functional test |
| USB-C PD Sink | 15W minimum | Power delivery test |
| M.2 NVMe SSD | 2230 form factor | NVMe slot test |

---

## 2. Visual Inspection

### 2.1 Bare Board Inspection (Before Assembly)

- [ ] Board dimensions: 130mm x 68mm ±0.1mm
- [ ] No solder mask on edge connector pads
- [ ] All fiducial markers present and clear (3 top, 3 bottom)
- [ ] Silkscreen legible: reference designators, polarity markers, pin 1 dots
- [ ] No copper slivers, no shorts on inner layers (visible via light table)
- [ ] Impedance test coupons present on panel edge
- [ ] Board finish: ENIG, gold thickness 3-5 microinches Ni, 1-2 microinches Au
- [ ] Solder mask: uniform coverage, no tenting on thermal vias under RK3576

### 2.2 Post-Assembly Inspection

- [ ] All components placed per BOM (no missing, no wrong parts)
- [ ] Polarized components correctly oriented (U1 pin A1 marker, electrolytic caps, diodes, ICs)
- [ ] No solder bridges on fine-pitch components (RK3576 BGA, LPDDR5 BGA)
- [ ] X-ray inspection of BGA components (RK3576, LPDDR5, eMMC): no voids >25% pad area
- [ ] RF shielding can soldered to pads around LMS7002M and CC1101 zones
- [ ] SMA connectors properly seated and soldered (no wicking into center pin)
- [ ] No tombstoned passives (0402 components)

---

## 3. Power-On Smoke Test

**WARNING:** Do NOT apply power until power rail verification is complete.

### 3.1 Continuity Check

1. Measure resistance between VBAT and GND at PMIC input:
   - **Expected:** >1kΩ (no dead short)
   - **FAIL:** <100Ω — do NOT apply power, inspect for solder bridges

2. Measure resistance between 3.3V rail and GND:
   - **Expected:** >500Ω
   - **FAIL:** <50Ω — check for bypass cap shorts

3. Measure resistance between 1.1V (VDD_CORE) and GND:
   - **Expected:** >100Ω
   - **FAIL:** <10Ω — inspect RK3576 BGA for shorts

### 3.2 Current-Limited Power-On

1. Set bench supply to 3.7V (single-cell LiPo simulation), current limit 100mA
2. Connect to VBAT and GND test points
3. Press power button (SW1)
4. Observe current draw:
   - **Expected:** 10-50mA (PMIC active, rails sequencing)
   - **FAIL:** >100mA — disconnect immediately, check for shorts on sequenced rails
5. Increase current limit to 500mA after initial verification

---

## 4. Power Rail Verification

### 4.1 PMIC Rail Checkout

Use DMM to measure each rail at test points after power-on:

| Rail | PMIC Output | Nominal Voltage | Tolerance | Test Point | Min | Max |
|------|------------|-----------------|-----------|------------|-----|-----|
| VDD_CORE | BUCK1 | 0.90V | ±3% | TP1 | 0.873V | 0.927V |
| VDD_DDR | BUCK2 | 1.10V | ±3% | TP2 | 1.067V | 1.133V |
| VDD_LOGIC | BUCK3 | 1.80V | ±3% | TP3 | 1.746V | 1.854V |
| VDD_3V3 | BUCK4 | 3.30V | ±3% | TP4 | 3.201V | 3.399V |
| LDO1 (SDR_1V2) | LDO1 | 1.20V | ±3% | TP5 | 1.164V | 1.236V |
| LDO2 (SDR_1V8) | LDO2 | 1.80V | ±3% | TP6 | 1.746V | 1.854V |
| LDO3 (SDR_PLL) | LDO3 | 1.80V | ±3% | TP7 | 1.746V | 1.854V |
| VBAT_BOOST | BOOST | 5.00V | ±5% | TP8 | 4.750V | 5.250V |
| RP2350B_3V3 | TLV75533 | 3.30V | ±2% | TP9 | 3.234V | 3.366V |
| RP2350B_1V8 | TLV75518 | 1.80V | ±2% | TP10 | 1.764V | 1.836V |
| SDR_CORE | SY8120B | 1.20V | ±3% | TP11 | 1.164V | 1.236V |

### 4.2 Power Sequencing (Oscilloscope)

Probe each rail with oscilloscope, CH1=VBAT, CH2=rail under test:
- [ ] BUCK1 ramps to 0.9V within 5ms of PWR_ON assertion
- [ ] BUCK2 follows BUCK1 by <2ms
- [ ] BUCK3 follows BUCK2 by <2ms
- [ ] BUCK4 follows BUCK3 by <2ms
- [ ] LDO1-LDO3 stabilize within 10ms of BUCK4 reaching 90%
- [ ] Total power-up sequence: <50ms from PWR_ON to all rails stable

### 4.3 Quiescent Current (Deep Sleep)

1. Hold RK3576 in reset (assert MCU_RESET low)
2. Hold RP2350B in deep sleep (WFE with all clocks gated)
3. Measure total board current:
   - **Expected:** 15-25mA (PMIC quiescent + LDO loads)
   - **FAIL:** >50mA — check for rail leaks or active peripherals

---

## 5. RK3576 Boot Test

### 5.1 Boot Sequence Verification

1. Insert MicroSD card with U-Boot SPL + U-Boot + Linux kernel
2. Connect USB-UART debug cable to J9 (UART0: 1500000 8N1)
3. Power on and observe boot log:

```
Expected sequence:
[    0.000] SPL: initializing LPDDR5...
[    0.050] SPL: DRAM: 8 GiB @ 3200MHz
[    0.100] SPL: loading U-Boot from eMMC...
[    0.500] U-Boot 2024.01 (Jun 2026)
[    0.600] U-Boot: detecting SPI0 slave (RP2350B)...
[    0.610] U-Boot: RP2350B detected at SPI0 CS0
[    1.000] U-Boot: loading kernel...
[    2.500] Kernel: RK3576 SoC initialized
[    3.000] Kernel: apex_bridge driver loaded
[    3.100] Kernel: /dev/apex_bridge0 created
```

- [ ] U-Boot SPL completes within 500ms
- [ ] LPDDR5 initializes at 3200 MT/s
- [ ] Kernel boots to login prompt within 5 seconds
- [ ] No kernel panics or oops

### 5.2 CPU Stress Test

```bash
# Run on target
stress-ng --cpu 8 --timeout 60s --metrics-brief
```

- [ ] All 8 CPU cores (4xA72 + 4xA53) operational
- [ ] No thermal throttling below 1.8GHz (A72) / 1.4GHz (A53)
- [ ] CPU temperature < 85°C under sustained load

---

## 6. RP2350B SPI Bridge Test

### 6.1 SPI Communication

1. Connect logic analyzer to SPI0 pins on RP2350B:
   - CH0: SPI0_SCK (PIN_18)
   - CH1: SPI0_TX (PIN_19)
   - CH2: SPI0_RX (PIN_16)
   - CH3: SPI0_CSn (PIN_17)
   - CH4: INT_REQ (PIN_20)
   - CH5: HOST_RDY (PIN_21)

2. Run bridge test from Linux:

```bash
# Write test frame to RP2350B
echo -n "APEX_TEST_PING" > /dev/apex_bridge0

# Read response (should echo with CRC)
cat /dev/apex_bridge0

# IOCTL test: get firmware version
apex-ctl --ioctl APEX_IOC_FW_VERSION

# Expected: 0x00010000 (v1.0.0)
```

- [ ] SPI clock frequency: 50MHz (measured on oscilloscope)
- [ ] CSn active low, minimum 100ns setup time
- [ ] INT_REQ assertion within 10us of RP2350B response ready
- [ ] CRC-64 header validation passes on all frames
- [ ] CRC-32 payload validation passes on all frames
- [ ] Sustained throughput > 5 MB/s over SPI bridge

### 6.2 DMA Transfer Test

```bash
# Stream 1MB of SDR IQ data from RP2350B
dd if=/dev/apex_bridge0 of=/dev/null bs=4096 count=256

# Expected: 1048576 bytes (1 MB) transferred in < 200ms
```

- [ ] No CRC errors in 1MB transfer
- [ ] Transfer time < 200ms (> 5 MB/s sustained)
- [ ] kfifo read pointer advances correctly
- [ ] No kernel warnings or DMA errors in dmesg

---

## 7. SDR (LMS7002M) Functional Test

### 7.1 SPI Register Access

1. Verify RP2350B can read LMS7002M SPI registers:
   - [ ] Read register 0x0020 (chip ID): expected 0x3840 (LMS7002M)
   - [ ] Write/readback register 0x0021: expected write value matches read

### 7.2 RX Functional Test

1. Connect signal generator to SDR SMA (J3) via SMA cable
2. Set signal generator: 433.0 MHz, -50 dBm, CW
3. Configure LMS7002M via apex-ctl:

```bash
apex-ctl --sdr-tune 433000000 --sdr-bw 10 --sdr-gain 40 --sdr-rx
```

4. Capture 1 second of IQ data:

```bash
dd if=/dev/apex_bridge0 of=/tmp/sdr_iq.bin bs=4096 count=244
# 1s at 1 MSPS = 2MB (I+Q, 16-bit each)
```

5. Verify signal in captured data:
   - [ ] FFT shows clear peak at 433 MHz
   - [ ] Noise floor below -80 dBFS
   - [ ] Image rejection > 40 dBc

### 7.3 TX Functional Test

1. Connect spectrum analyzer to SDR SMA (J3)
2. Configure LMS7002M for TX:

```bash
apex-ctl --sdr-tune 433000000 --sdr-bw 10 --sdr-tx --sdr-power -20
```

3. Transmit CW tone:
   - [ ] Spectrum analyzer shows carrier at 433.0 MHz
   - [ ] Output power within ±3 dB of -20 dBm
   - [ ] Harmonics < -30 dBc
   - [ ] Spurious emissions < -40 dBc

---

## 8. Sub-GHz (CC1101) Functional Test

### 8.1 SPI Register Access

1. Verify RP2350B can read CC1101 SPI registers:
   - [ ] Read register 0x30 (PARTNUM): expected 0x00
   - [ ] Read register 0x31 (VERSION): expected 0x04 or higher
   - [ ] Read register 0x40 (CHIPSTATUS): expected 0x01 (idle)

### 8.2 RX Test (433 MHz OOK)

1. Connect signal generator to Sub-GHz SMA (J4)
2. Set signal generator: 433.92 MHz, OOK modulation, 1 kbps, -60 dBm
3. Configure CC1101:

```bash
apex-ctl --cc1101-tune 433920000 --cc1101-mod OOK --cc1101-rx
```

4. Verify:
   - [ ] CC1101 GDO0 asserts on received data
   - [ ] RSSI > -80 dBm (measured via register 0x13)
   - [ ] Packet error rate < 1% over 1000 packets

### 8.3 TX Test (433 MHz FSK)

1. Connect spectrum analyzer to Sub-GHz SMA (J4)
2. Transmit test packet:

```bash
apex-ctl --cc1101-tune 433920000 --cc1101-mod FSK --cc1101-tx --cc1101-power 0
```

3. Verify:
   - [ ] Carrier at 433.92 MHz ± 10 kHz
   - [ ] Output power within ±2 dB of +10 dBm
   - [ ] Modulation visible on spectrum analyzer

---

## 9. NFC (ST25R3916) Functional Test

### 9.1 SPI Register Access

1. Verify RP2350B can read ST25R3916 registers:
   - [ ] Read register IC_TYPE (0x00): expected 0x03
   - [ ] Read register IC_REV (0x01): expected >= 0x03

### 9.2 Reader Mode Test

1. Place ISO 14443 Type A tag (NTAG215) near NFC antenna
2. Enable reader mode:

```bash
apex-ctl --nfc-reader-on
```

3. Verify:
   - [ ] Tag detected within 30mm range
   - [ ] UID read successfully (7 or 10 bytes)
   - [ ] NDEF data read without CRC error

### 9.3 Field Strength Test

1. Use NFC field probe to measure H-field at antenna surface:
   - [ ] H-field > 1.5 A/m at 5mm from antenna center
   - [ ] No spurious oscillations in field

---

## 10. Wi-Fi 6E / BT 5.4 (MT7922) Test

### 10.1 Wi-Fi Interface Detection

```bash
# Check interface
ip link show wlan0
iw dev wlan0 info
```

- [ ] wlan0 interface present
- [ ] Supports 2.4 GHz, 5 GHz, 6 GHz bands
- [ ] Supports monitor mode (iw dev wlan0 set type monitor)

### 10.2 Monitor Mode + Packet Injection

```bash
# Enable monitor mode
iw dev wlan0 set type monitor
ip link set wlan0 up

# Set channel
iw dev wlan0 set channel 6

# Test packet injection
aireplay-ng -9 wlan0
```

- [ ] Monitor mode activates without error
- [ ] Packet injection test passes all frame types
- [ ] Capture rate > 1000 packets/second on busy channel

### 10.3 Bluetooth Test

```bash
# Check Bluetooth
hciconfig hci0 up
hcitool scan
```

- [ ] hci0 interface present
- [ ] Can discover nearby Bluetooth devices
- [ ] BD address reads correctly

---

## 11. LPDDR5 Memory Test

```bash
# Run memtester (1GB = 1 iteration)
memtester 1G 1

# Or use stress-ng memory tests
stress-ng --vm 2 --vm-bytes 4G --timeout 120s
```

- [ ] 8GB detected at boot (dmesg | grep "Memory")
- [ ] LPDDR5 running at 3200 MT/s
- [ ] memtester 1G: 0 errors
- [ ] stress-ng vm: 0 failures

---

## 12. eMMC Storage Test

```bash
# Check eMMC
mmc info
cat /sys/class/mmc_host/mmc0/mmc0:0001/name

# Read/write test
dd if=/dev/urandom of=/tmp/emmc_test bs=1M count=512
md5sum /tmp/emmc_test
dd if=/tmp/emmc_test of=/dev/null bs=1M
md5sum /dev/stdin
```

- [ ] eMMC detected: 32GB Kioxia THGBMJG6C1LBAB7
- [ ] eMMC 5.1 mode
- [ ] Sequential read > 300 MB/s
- [ ] Sequential write > 100 MB/s
- [ ] No I/O errors in kernel log

---

## 13. NVMe SSD Test

```bash
# Check NVMe
nvme list
nvme smart-log /dev/nvme0

# Performance test
fio --name=nvme-read --filename=/dev/nvme0n1 --rw=read --bs=4k --numjobs=4 --size=1G --group_reporting
fio --name=nvme-write --filename=/dev/nvme0n1 --rw=write --bs=4k --numjobs=4 --size=1G --group_reporting
```

- [ ] NVMe SSD detected at /dev/nvme0
- [ ] PCIe Gen3 x2 link negotiated
- [ ] Sequential read > 1500 MB/s
- [ ] Sequential write > 800 MB/s
- [ ] SMART log shows 0 media errors

---

## 14. USB-C Test

### 14.1 Data Transfer

1. Connect USB-C to host computer
2. Verify device enumeration:

```bash
lsusb
# Expected: Linux Foundation 3.0 root hub (USB 3.0)
```

- [ ] USB 3.0 SuperSpeed enumerated
- [ ] USB 2.0 fallback works when SS not available
- [ ] Sustained transfer > 300 MB/s (USB 3.0)

### 14.2 Power Delivery

1. Connect USB-C PD power supply (15W+)
2. Measure charging current at battery:
   - [ ] Charging current > 1.5A at 3.7V (5.5W+)
   - [ ] PMIC reports VBUS voltage within ±5% of negotiated PDO

---

## 15. Thermal Validation

### 15.1 Idle Temperature

1. Let board idle for 10 minutes in still air (no fan)
2. Measure with thermal camera or internal sensors:

```bash
cat /sys/class/thermal/thermal_zone0/temp  # RK3576
cat /sys/class/thermal/thermal_zone1/temp  # LMS7002M
```

- [ ] RK3576 idle: < 45°C
- [ ] LMS7002M idle: < 40°C
- [ ] RP2350B idle: < 35°C
- [ ] No hot spots > 60°C on PCB

### 15.2 Stress Temperature

1. Run all subsystems simultaneously:
   - RK3576: `stress-ng --cpu 8`
   - SDR: continuous RX at 1 MSPS
   - Wi-Fi: continuous monitor mode capture
   - NFC: continuous reader polling

2. Measure after 10 minutes:
   - [ ] RK3576 stress: < 85°C
   - [ ] LMS7002M stress: < 70°C
   - [ ] No thermal throttling on RK3576
   - [ ] PMIC junction < 100°C

---

## 16. RF Shielding Verification

1. With LMS7002M in RX mode at 1 GHz, measure radiated emissions from SDR section:
   - [ ] Emissions from digital section < -70 dBm at antenna port (with can installed)
   - [ ] Digital clock harmonics suppressed > 40 dB below carrier

2. Test antenna isolation between ports:
   - [ ] SDR ↔ Sub-GHz isolation: > 40 dB
   - [ ] SDR ↔ Wi-Fi isolation: > 30 dB
   - [ ] NFC ↔ any RF port: > 50 dB (different frequency band)

---

## 17. Pass/Fail Criteria

| Test | Must Pass | Margin |
|------|-----------|--------|
| Visual Inspection | ALL items checked | N/A |
| Power Rail Voltages | Within tolerance | ±3% (±2% for LDOs) |
| Power Sequencing | All rails sequence correctly | < 50ms total |
| Quiescent Current | < 25mA | 15-25mA |
| RK3576 Boot | Boot to shell < 5s | < 7s |
| SPI Bridge CRC | 0 errors in 1MB transfer | 0 errors |
| SPI Throughput | > 5 MB/s sustained | > 4 MB/s |
| SDR RX | Signal at expected freq | ±100 Hz |
| SDR TX | Output power ±3 dBm | ±5 dBm |
| CC1101 RX | PER < 1% | < 3% |
| NFC Reader | Tag detection < 30mm | < 20mm |
| Wi-Fi Monitor | Packet injection passes | All frame types |
| LPDDR5 | 0 errors in memtester | 0 errors |
| eMMC | 0 I/O errors | 0 errors |
| NVMe | PCIe Gen3 x2 | Gen3 x1 min |
| Thermal Idle | < 45°C all chips | < 50°C |
| Thermal Stress | < 85°C all chips | < 90°C |
| RF Isolation | > 40 dB cross-port | > 30 dB |

**VERDICT:** Board PASSES only when ALL must-pass criteria are met.

---

*Document generated for GhostBlade Project NullSpectre. All measurements at 25°C ambient unless otherwise noted.*