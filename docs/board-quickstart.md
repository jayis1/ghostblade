# GhostBlade — Board Quick-Start Guide

<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

> **TL;DR** — Get a new GhostBlade board from "unpowered" to "fully operational" in 10 minutes.

This is the condensed quick-start guide. For detailed instructions, see:
- [Getting Started (full guide)](getting-started.md)
- [Build Instructions](build-instructions.md)
- [Flashing Guide](flashing-guide.md)
- [Hardware Bring-Up Checklist](hardware-bringup-checklist.md)

---

## Prerequisites

| Item | Minimum Version |
|------|-----------------|
| Host OS | Ubuntu 22.04+ or equivalent Linux |
| ARM toolchain | `arm-none-eabi-gcc` 12.3+ |
| AArch64 toolchain | `aarch64-linux-gnu-gcc` |
| Pico SDK | 2.0+ |
| Linux kernel headers | 6.6+ (for RK3576) |
| Python | 3.10+ |
| KiCad | 8.0+ (for hardware changes) |

```bash
# Quick toolchain setup
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib gcc-aarch64-linux-gnu \
    cmake ninja-build python3-pip dtc
pip3 install --user meson pytest
export PICO_SDK_PATH=/opt/pico-sdk  # or your Pico SDK location
```

---

## 1. Clone & Build

```bash
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade

# RP2350B firmware
cd firmware/rp2350b
mkdir -p build && cd build
cmake -G Ninja -DPICO_SDK_PATH=$PICO_SDK_PATH ..
ninja -j$(nproc)
# Output: ghostblade_rp2350b.uf2

# Linux kernel driver (cross-compile)
cd ../../software/linux-drivers
export KDIR=/path/to/rk3576-kernel
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make -C ${KDIR} M=$(pwd) modules

# DTS overlays
cd ../dts
make

# libapex + Python bindings
cd ../libapex
mkdir -p build && cd build
cmake -G Ninja .. && ninja
cd ../python && pip3 install --user .
```

---

## 2. Flash the RP2350B

### Method 1: UF2 (Recommended — No Debugger Needed)

1. Hold **BOOTSEL** on the RP2350B
2. Press and release **RESET**
3. Release **BOOTSEL** — RP2350B appears as USB mass storage
4. Copy `ghostblade_rp2350b.uf2` to the drive
5. MCU reboots automatically

### Method 2: OpenOCD/SWD

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "program ghostblade_rp2350b.elf verify reset exit"
```

### Method 3: Kernel Driver Managed Flash

```bash
apex-ctl --mcu-reset
sleep 0.5
apex-ctl --flash-firmware ghostblade_rp2350b.bin
```

---

## 3. Boot the RK3576

Connect USB-C for debug console:

```bash
# Serial console at 1500000 baud
minicom -b 1500000 -D /dev/ttyUSB0
```

Wait for Linux boot. Expected output:
```
U-Boot 2024.xx (GhostBlade)
Rockchip RK3576 SoC
...
apex_bridge: SPI0 slave probed at 50 MHz
```

---

## 4. Load the Kernel Driver

```bash
sudo insmod apex_bridge.ko
dmesg | tail -10  # Should show: "apex_bridge: MCU ready, firmware v1.0.0"
cat /sys/class/apex/apex_bridge0/driver_status  # Should show: "online"
```

---

## 5. Verify All Subsystems

```bash
# Install pyapex
pip3 install --user .

# Quick verification script
python3 -c "
import pyapex
dev = pyapex.ApexDevice()
print(f'Firmware: {dev.get_version()}')
print(f'VBat: {dev.get_vbat_mv()} mV')
print(f'Temp: {dev.get_temp_c()} °C')
dev.close()
print('All subsystems operational!')
"
```

Expected output:
```
Firmware: 1.0.0
VBat: 3700 mV
Temp: 25.0 °C
All subsystems operational!
```

---

## 6. Quick Smoke Tests

```bash
# SDR tune test
apex-ctl --sdr-tune 868000000 2000000 300

# NFC tag read (place ISO 14443A tag near antenna)
apex-ctl --nfc-transact 2600

# CC1101 receive test
apex-ctl --cc1101-rx 433920000

# Wi-Fi scan
iw dev wlan0 scan | grep SSID
```

---

## Common Issues

| Symptom | Fix |
|---------|-----|
| `/dev/apex_bridge0` not created | `sudo insmod apex_bridge.ko` |
| `ENODEV` on sysfs reads | Check MCU is running: `apex-ctl --mcu-reset` |
| SPI transfer timeouts | Check DTS: `spi-max-frequency = <50000000>` |
| UF2 not mounting | Try different USB cable, hold BOOTSEL longer |
| `dmesg` shows "MCU not responding" | Check RP2350B power rail (TP5 = 3.3V) |
| Brownout count > 0 | Check battery voltage, use bench supply at 3.7V |

---

## Next Steps

- **Full hardware bring-up**: [hardware-bringup-checklist.md](hardware-bringup-checklist.md)
- **Detailed build guide**: [build-instructions.md](build-instructions.md)
- **Flashing options**: [flashing-guide.md](flashing-guide.md)
- **Troubleshooting**: [faq-troubleshooting.md](faq-troubleshooting.md)
- **System architecture**: [architecture.md](architecture.md)

---

*Document version: 1.0 — 2026-07-05*