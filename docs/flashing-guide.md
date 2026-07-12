<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# Flashing Guide — GhostBlade (Project NullSpectre)

This guide covers flashing firmware to the RP2350B coprocessor and loading
the kernel driver on the RK3576 SoC.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Flashing the RP2350B Firmware](#flashing-the-rp2350b-firmware)
3. [Loading the Kernel Driver on RK3576](#loading-the-kernel-driver-on-rk3576)
4. [Cross-Compiling the Kernel Driver](#cross-compiling-the-kernel-driver)
5. [Verifying the SPI Bridge](#verifying-the-spi-bridge)
6. [Updating the Device Tree](#updating-the-device-tree)
7. [Troubleshooting](#troubleshooting)

---

## Prerequisites

- GhostBlade hardware (RK3576 + RP2350B)
- USB-C cable for RP2350B programming
- Serial console access to RK3576 (UART, 1500000 baud)
- Built firmware image (`ghostblade.uf2` or `ghostblade.elf`)
- Built kernel module (`apex_bridge.ko`)

---

## Flashing the RP2350B Firmware

### Method 1: USB BOOTSEL Mode (Recommended)

The RP2350B supports USB mass-storage bootloader mode, similar to the
Raspberry Pi Pico.

1. **Disconnect power** from the GhostBlade board
2. **Hold the BOOTSEL button** on the RP2350B (SW2 on the schematic)
3. **Connect USB-C cable** from the RP2350B USB port to your host PC
4. **Release BOOTSEL button** — the RP2350B will appear as a USB mass storage device
5. **Copy the firmware**:

```bash
# The RP2350B appears as a drive (e.g., /media/$USER/RPI-RP2)
cp build/ghostblade.uf2 /media/$USER/RPI-RP2/
```

6. The RP2350B will automatically reboot with the new firmware
7. Verify with serial console:
```
RP2350B: Clocks configured - SYS=150MHz, PERI=150MHz, PLL=1200MHz
RP2350B: SPI0 slave initialized at 50MHz
RP2350B: GPIO pins configured (23 total)
RP2350B: All peripherals initialized - MCU_READY asserted
```

### Method 2: OpenOCD / SWD Debug Probe

For development and debugging, use SWD via a debug probe:

```bash
# Using a Raspberry Pi Debug Probe or compatible SWD adapter
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg

# In another terminal, flash via GDB
arm-none-eabi-gdb build/ghostblade.elf
(gdb) target remote :3333
(gdb) load
(gdb) continue
```

### Method 3: picotool

```bash
# Erase and flash
picotool load -x build/ghostblade.uf2

# Or load ELF directly with debug symbols
picotool load -x build/ghostblade.elf
```

---

## Loading the Kernel Driver on RK3576

### Method 1: Manual Load (Development)

```bash
# Build the module
cd software/linux-drivers
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load the module
sudo insmod apex_bridge.ko

# Verify
dmesg | tail -20
# Should see:
# apex_bridge: SPI driver for GhostBlade bridge device registered
# apex_bridge apx_bridge0: MCU ready detected (status=0x01)
# apex_bridge apx_bridge0: character device /dev/apex_bridge0 created

# Check device node
ls -la /dev/apex_bridge0
```

### Method 2: Auto-Load via /etc/modules (Persistent)

```bash
# Copy module to kernel modules tree
sudo cp apex_bridge.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a

# Add to module list for auto-loading
echo "apex_bridge" | sudo tee /etc/modules-load.d/apex_bridge.conf

# Reboot or load manually
sudo modprobe apex_bridge
```

### Unloading the Module

```bash
sudo rmmod apex_bridge
```

---

## Cross-Compiling the Kernel Driver

If building on an x86_64 host for the RK3576 (aarch64):

```bash
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export KERNEL_SRC=/path/to/rk3576-kernel-source

cd software/linux-drivers
make -C $KERNEL_SRC ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE M=$(pwd) modules

# Copy to target
scp apex_bridge.ko root@192.168.1.100:/lib/modules/$(uname -r)/extra/
```

### Reproducible Builds

For reproducible builds, set the `SOURCE_DATE_EPOCH` and `KBUILD_BUILD_TIMESTAMP`:

```bash
export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
export KBUILD_BUILD_TIMESTAMP="$(date -u -d @${SOURCE_DATE_EPOCH})"
make -C $KERNEL_SRC M=$(pwd) modules
```

---

## Verifying the SPI Bridge

After loading both the firmware and the kernel driver, verify communication:

```bash
# Check driver status
cat /sys/class/apex_bridge/apex_bridge0/status

# Read telemetry using libapex
cd software/libapex
./examples/telemetry_read
# Expected output:
# Battery: 4120 mV
# Temperature: 42.3 C
# Uptime: 15234 ms
# SDR RX: inactive
# CC1101 RSSI: -87.5 dBm

# Run the Python bindings test
python3 -c "import pyapex; d=pyapex.open(); print(d.get_telemetry())"
```

### SPI Debug

If the bridge is not responding:

```bash
# Enable dynamic debug
echo 'module apex_bridge +p' | sudo tee /sys/kernel/debug/dynamic_debug/control

# Check SPI controller status
cat /sys/bus/spi/devices/spi0.0/modalias
cat /sys/bus/spi/devices/spi0.0/statistics/transfers

# Monitor SPI traffic (if CONFIG_SPI_DEBUG is enabled)
dmesg | grep -i spi
```

---

## Updating the Device Tree

The device tree blob must include the `apex_bridge` node. The provided
DTS file at `software/dts/ghostblade-rk3576.dts` includes all required nodes.

### Compile the DTS

```bash
# Native
dtc -I dts -O dtb -o ghostblade-rk3576.dtb software/dts/ghostblade-rk3576.dts

# Install to boot partition
sudo cp ghostblade-rk3576.dtb /boot/dtbs/$(uname -r)/rockchip/
```

### Apply as Overlay (if supported)

```bash
# Compile overlays
dtc -I dts -O dtb -@ -o ghostblade-sdr-overlay.dtbo software/dts/ghostblade-sdr-overlay.dts
dtc -I dts -O dtb -@ -o ghostblade-nfc-overlay.dtbo software/dts/ghostblade-nfc-overlay.dts
dtc -I dts -O dtb -@ -o ghostblade-wifi-overlay.dtbo software/dts/ghostblade-wifi-overlay.dts
dtc -I dts -O dtb -@ -o ghostblade-options.dtbo software/dts/ghostblade-options.dts

# Apply at runtime
sudo mkdir -p /sys/kernel/config/device-tree/overlays/sdr
sudo cp ghostblade-sdr-overlay.dtbo /sys/kernel/config/device-tree/overlays/sdr/dtbo

sudo mkdir -p /sys/kernel/config/device-tree/overlays/nfc
sudo cp ghostblade-nfc-overlay.dtbo /sys/kernel/config/device-tree/overlays/nfc/dtbo

sudo mkdir -p /sys/kernel/config/device-tree/overlays/wifi
sudo cp ghostblade-wifi-overlay.dtbo /sys/kernel/config/device-tree/overlays/wifi/dtbo

sudo mkdir -p /sys/kernel/config/device-tree/overlays/options
sudo cp ghostblade-options.dtbo /sys/kernel/config/device-tree/overlays/options/dtbo
```

---

## Troubleshooting

### RP2350B not entering BOOTSEL mode

- Ensure the USB cable provides data (not charge-only)
- Try a different USB port (USB 2.0 preferred)
- Hold BOOTSEL for at least 2 seconds before releasing
- Check that RP2350B has power (VDD_3V3 rail must be stable)

### Kernel driver not probing

- Verify the DTS node matches the SPI bus (`spi0` on RK3576)
- Check `compatible = "apex,apex-bridge"` matches the driver
- Ensure `reg = <0>` for SPI chip select 0
- Confirm SPI controller is enabled in the kernel config (`CONFIG_SPI_ROCKCHIP=y`)

### MCU_READY not detected

- Check SPI0 physical connections (CLK, MOSI, MISO, CS, INT_REQ, HOST_RDY)
- Verify RP2350B firmware is running (check serial output)
- The MCU may need up to 200ms after reset release to assert MCU_READY
- Check GPIO25 (INT_REQ) and GPIO24 (HOST_RDY) are correctly mapped in DTS

### SPI communication errors

- Reduce SPI clock speed (try 10 MHz instead of 50 MHz)
- Check for signal integrity issues (probe with oscilloscope)
- Verify CRC alignment in protocol frames
- Enable `CONFIG_SPI_DEBUG` for detailed SPI logging
- Check `max-frequency` in DTS matches hardware capability

### More Help

See [FAQ & Troubleshooting](faq-troubleshooting.md) for additional
common issues and solutions.