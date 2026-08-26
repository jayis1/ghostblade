<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# Flashing Guide

This guide covers RP2350B firmware flashing, RK3576 driver bring-up, and DTB/overlay deployment.

## Required Artifacts

Build these first:

- `firmware/rp2350b/build/ghostblade.uf2` or `ghostblade.elf`
- `software/linux-drivers/apex_bridge.ko`
- optional `.dtb` / `.dtbo` outputs from `software/dts/`

## RP2350B Flashing

### Method 1: BOOTSEL mass-storage mode

1. Power down the board or hold the RP2350B in reset.
2. Hold **BOOTSEL**.
3. Connect the RP2350B USB port to the host.
4. Release **BOOTSEL** once the `RPI-RP2` volume appears.
5. Copy the UF2 image:

```bash
cp firmware/rp2350b/build/ghostblade.uf2 /media/$USER/RPI-RP2/
```

The RP2350B reboots automatically after the copy completes.

### Method 2: OpenOCD / SWD

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "program firmware/rp2350b/build/ghostblade.elf verify reset exit"
```

### Method 3: picotool

```bash
picotool load -x firmware/rp2350b/build/ghostblade.uf2
```

## RP2350B Post-Flash Checks

Recommended checks after flashing:

- serial console shows firmware boot output
- RK3576-side reset line can release the MCU cleanly
- `INT_REQ` and `HOST_RDY` behavior matches the timing docs

## Device Tree Deployment

### Build the base DTB and overlays

```bash
make -C software/dts all
```

### Install the base DTB

```bash
sudo cp software/dts/ghostblade-rk3576.dtb /boot/dtbs/$(uname -r)/rockchip/
```

### Install overlays for runtime use

```bash
sudo mkdir -p /sys/kernel/config/device-tree/overlays/sdr
sudo cp software/dts/ghostblade-sdr-overlay.dtbo /sys/kernel/config/device-tree/overlays/sdr/dtbo

sudo mkdir -p /sys/kernel/config/device-tree/overlays/nfc
sudo cp software/dts/ghostblade-nfc-overlay.dtbo /sys/kernel/config/device-tree/overlays/nfc/dtbo

sudo mkdir -p /sys/kernel/config/device-tree/overlays/wifi
sudo cp software/dts/ghostblade-wifi-overlay.dtbo /sys/kernel/config/device-tree/overlays/wifi/dtbo
```

If your kernel requires external include paths during compile, use `DTS_INCLUDE_PATHS` as documented in [Build Instructions](build-instructions.md).

## RK3576 Driver Bring-Up

### Load the module

```bash
sudo insmod software/linux-drivers/apex_bridge.ko
```

### Verify probe success

```bash
dmesg | tail -50
ls -l /dev/apex_bridge0
ls -l /sys/class/apex/apex_bridge0
```

Expected results:

- the driver probes without SPI or GPIO errors
- `/dev/apex_bridge0` exists
- sysfs entries are present under `/sys/class/apex/apex_bridge0/`

### Unload the module

```bash
sudo rmmod apex_bridge
```

## Cross-Built Driver Installation

If you built on a workstation:

```bash
scp software/linux-drivers/apex_bridge.ko root@target:/tmp/
ssh root@target 'install -m 0644 /tmp/apex_bridge.ko /lib/modules/'"$(uname -r)"'/extra/apex_bridge.ko && depmod -a'
```

Then on target:

```bash
sudo modprobe apex_bridge
```

## Bridge Validation

### Repository-level validation

```bash
python3 tools/validate_dts.py
python3 tools/validate_netlist.py
```

### Host-side tests

```bash
make -C tests run
```

### On-target smoke checks

```bash
cat /sys/class/apex/apex_bridge0/status 2>/dev/null || true
ls /sys/class/apex/apex_bridge0
```

## Recovery Paths

### RP2350B does not enumerate in BOOTSEL mode

- try a known-good data cable
- remove hubs/adapters
- verify 3V3 is present on the MCU side
- fall back to SWD with OpenOCD

### Driver does not probe

- verify the installed DTB contains the `apex,apex-bridge` node on `&spi0`
- confirm GPIO polarity matches `ghostblade-rk3576.dts`
- confirm the SPI controller is enabled in the kernel

### Overlay application fails

- ensure configfs overlay support is enabled
- confirm the target nodes exist in the running base tree
- rebuild the overlay with any required include paths

### `/dev/apex_bridge0` is missing

- check `dmesg` for probe failures
- confirm the module is loaded with `lsmod | grep apex_bridge`
- confirm the MCU firmware is running and bridge GPIOs are wired as documented

## Related Docs

- [Getting Started](getting-started.md)
- [Build Instructions](build-instructions.md)
- [SPI Protocol & Timing](spi-protocol-timing.md)
- [FAQ & Troubleshooting](faq-troubleshooting.md)
