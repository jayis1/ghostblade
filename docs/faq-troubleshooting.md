<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# FAQ & Troubleshooting — GhostBlade (Project NullSpectre)

Frequently asked questions and common troubleshooting solutions for the
GhostBlade hardware and software.

---

## General

### What is GhostBlade?

GhostBlade (Project NullSpectre) is a pocket-sized penetration testing
device with a dual-processor architecture: RK3576 (Linux SoC) and
RP2350B (real-time MCU). It includes wideband SDR, sub-GHz radio,
NFC, and Wi-Fi 6E.

### What licenses apply?

- **Hardware** (schematics, PCB, BOM): CERN-OHL-S v2
- **Firmware & software**: GPL-2.0-or-later
- **Documentation**: CC-BY-SA 4.0

### Where can I buy hardware?

GhostBlade is an open-source hardware project. You can fabricate the
PCB and assemble components using the BOM and Gerber files in the
`hardware/` directory. Check the project wiki for recommended assembly
houses.

---

## Build Issues

### Firmware build fails: "PICO_SDK_PATH not set"

Set the `PICO_SDK_PATH` environment variable to point to the Pico SDK:

```bash
export PICO_SDK_PATH=/opt/pico-sdk
```

Or pass it to CMake directly:

```bash
cmake .. -DPICO_SDK_PATH=/opt/pico-sdk -DPICO_PLATFORM=rp2350
```

### Kernel driver build fails: "No rule to make target 'modules'"

The kernel headers must be installed and accessible:

```bash
sudo apt-get install linux-headers-$(uname -r)
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

For cross-compilation, point to the target kernel source tree:

```bash
make -C /path/to/kernel/src ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(pwd) modules
```

### "arm-none-eabi-gcc: command not found"

Install the ARM bare-metal toolchain:

```bash
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi
```

### CMake can't find Pico SDK submodules

Initialize the Pico SDK submodules:

```bash
cd $PICO_SDK_PATH && git submodule update --init --recursive
```

---

## Flashing & Boot Issues

### RP2350B won't enter BOOTSEL mode

1. Ensure USB cable supports data (not charge-only)
2. Hold BOOTSEL for at least 2 seconds before releasing
3. Try a different USB port (USB 2.0 preferred)
4. Verify VDD_3V3 power rail is stable (3.25–3.60V)
5. Check that the BOOTSEL button (SW2) is properly soldered

### Kernel driver doesn't probe

1. Verify the device tree node exists and has the correct `compatible` string:
   ```
   compatible = "apex,apex-bridge";
   ```
2. Check that the SPI controller is enabled (`CONFIG_SPI_ROCKCHIP=y`)
3. Ensure the `reg = <0>` property matches the SPI chip select
4. Check dmesg for probe errors:
   ```bash
   dmesg | grep apex_bridge
   ```

### MCU_READY signal not detected

The RP2350B needs approximately 150–200ms after reset release before
asserting MCU_READY. The kernel driver waits up to 500ms. If you see
timeouts:

1. Verify RP2350B firmware is running (check UART output)
2. Check GPIO25 (INT_REQ) and GPIO24 (HOST_RDY) connections
3. Probe the MCU_READY signal with an oscilloscope
4. Increase the driver timeout in `apex_bridge_regs.h`:
   ```c
   #define APEX_MCU_READY_TIMEOUT_MS  1000  /* Increase from 500 */
   ```

---

## SPI Communication Issues

### Intermittent CRC errors on SPI

1. **Reduce SPI clock speed**: Edit the DTS `max-frequency` property:
   ```dts
   max-frequency = <10000000>;  /* 10 MHz (down from 50 MHz) */
   ```
2. **Check signal integrity**: Probe SPI signals with an oscilloscope
3. **Verify ground connections**: Ensure solid ground between RK3576 and RP2350B
4. **Check trace lengths**: SPI traces should be < 50mm on the PCB

### SPI transfer hangs / no response

1. Verify both HOST_RDY and INT_REQ GPIOs are correctly configured
2. Check that the RP2350B SPI0 slave handler is running (poll in main loop)
3. Ensure no other SPI devices are on SPI0 bus (CS conflict)
4. Reset the MCU via `apex_mcu_reset()` and retry

### SDR IQ data stream drops frames

1. The SDR streams IQ data via MIPI CSI-2, not SPI — check CSI-2 lane connections
2. Reduce sample rate to stay within DMA bandwidth limits
3. Ensure the RP2350B DMA ring buffer isn't overflowing (increase buffer count)
4. Check for thermal throttling on the RP2350B (die temp > 85°C)

---

## Hardware Issues

### Board doesn't power on

1. **Check power supply**: USB-C PD must deliver 5V/3A minimum
2. **Check PMIC**: RK817 should output VDD_3V3, VDD_1V8, VDD_DDR, VDD_CORE
3. **Measure voltages** at test points:
   - TP1: VDD_3V3 (3.25–3.60V)
   - TP2: VDD_1V8 (1.71–1.89V)
   - TP3: VDD_CORE (0.80–0.95V)
   - TP4: VDD_DDR (0.57–0.63V)
4. **Check battery**: If running on battery, verify > 3.0V at BATT+ terminal

### LMS7002M SDR not responding

1. Verify SPI1 bus connectivity (RP2350B → LMS7002M)
2. Check LMS7002M reset GPIO (GPIO31 on RP2350B)
3. Ensure VDD_3V3 and 1.8V rails are stable before LMS7002M reset release
4. Check MIPI CSI-2 lanes between LMS7002M and RK3576
5. Verify LMS7002M register 0x002F (reset) reads back correctly

### CC1101 sub-GHz radio not transmitting

1. Verify antenna connected to the correct SMA port (J4 for sub-GHz)
2. Check PE42422 antenna switch routing (must select RF3 for sub-GHz)
3. Verify CC1101 PA_TABLE register configuration (0x3E7: PATABLE)
4. Ensure CC1101 is in TX mode (MARCSTATE register should read 0x13)
5. Check VDD_3V3 rail current (CC1101 draws up to 30mA in TX)
6. Apply the CC1101 device tree overlay for default configuration: `dtc -I dts -O dtb -@ -o ghostblade-cc1101-overlay.dtbo ghostblade-cc1101-overlay.dts`

### ST25R3916 NFC not detecting tags

1. Verify NFC antenna coil is connected (J7 header)
2. Check ST25R3916 SPI2 bus connectivity
3. Measure antenna tuning: expect ~5Vpp at 13.56 MHz across the coil
4. Verify the IRQ line (GPIO44 on RP2350B) is not stuck
5. Try `apex_nfc_field_on()` and measure field strength with telemetry

### Overheating / thermal shutdown

1. The RK3576 TDP is ~5W — ensure the heatsink is properly mounted
2. Thermal vias under the RK3576 must be unobstructed
3. Check thermal pad placement (31×31 grid of 0.3mm vias)
4. Monitor temperature via telemetry:
   ```c
   apex_telemetry_t telem;
   apex_get_telemetry(handle, &telem);
   printf("Temperature: %.1f C\n", telem.temp_c_x10 / 10.0);
   ```
5. The MCU will assert APEX_TELEM_OVERTEMP when die temp exceeds 85°C

---

## Wi-Fi / MT7922 Issues

### Wi-Fi adapter not detected

1. Verify MT7922 is connected via SDIO or USB (check DTS bus type)
2. Check that the MT7922 firmware is loaded:
   ```bash
   dmesg | grep mt7922
   ```
3. Ensure `CONFIG_MT7922E=m` or `=y` in kernel config
4. For USB mode, verify USB 3.0 host port connectivity

### Wi-Fi 6E not operating in 6 GHz band

1. The 6 GHz band requires regulatory domain configuration
2. Set the regulatory domain:
   ```bash
   iw reg set US
   ```
3. Verify the MT7922 firmware supports 6 GHz operation
4. Check antenna connections (J5 for 2.4 GHz, J6 for 5/6 GHz)

---

## Software Issues

### /dev/apex_bridge0 not created

1. Load the kernel module: `sudo insmod apex_bridge.ko`
2. Check `dmesg` for probe errors
3. Verify the DTS node matches the driver's `compatible` string
4. Ensure `CONFIG_SPI=y` and `CONFIG_SPI_ROCKCHIP=y` in kernel config

### libapex returns APEX_ERR_NO_DEVICE

1. Check that `/dev/apex_bridge0` exists:
   ```bash
   ls -la /dev/apex_bridge0
   ```
2. Verify permissions: the device needs read/write access
   ```bash
   sudo chmod 666 /dev/apex_bridge0
   # Or add a udev rule for permanent access
   ```
3. The driver must be loaded and MCU_READY detected

### Python bindings fail to import

1. Ensure libapex is installed: `sudo ldconfig`
2. Rebuild the Python extension:
   ```bash
   cd software/libapex
   pip3 install --force-reinstall .
   ```
3. Check for ABI mismatch between Python versions

---

## Contributing

### How do I report a bug?

Open an issue at https://github.com/jayis1/ghostblade/issues with:
- Hardware revision (check PCB silkscreen)
- Firmware version (`git describe --tags`)
- Kernel version (`uname -r`)
- Steps to reproduce
- Relevant dmesg / serial output

### How do I submit a patch?

See [CONTRIBUTING.md](../CONTRIBUTING.md) for the full contribution guide.
In short: fork, branch, commit, push, open a PR.

### My PR isn't getting reviewed

Be patient — maintainers review PRs as time permits. You can bump by
adding a polite comment after a few days. Make sure your branch is
up-to-date with `main` and that all local checks pass.

---

## Testing

### `make tests` fails to build `test_libapex_framing`

This was caused by the battery helper test functions being declared
as block-scope prototypes inside the test body rather than defined as
file-scope `static` functions. If you see linker errors such as
`undefined reference to 'battery_percent'`, make sure the helpers are
defined at file scope (not declared as prototypes inside the function):

```c
/* Correct — file-scope static definitions */
static uint8_t battery_percent(uint16_t vbat_mv) { ... }
static bool is_low_battery(uint16_t vbat_mv) { ... }
static bool is_overtemp(int16_t temp_c_x10) { ... }
```

The helper logic must match `battery_get_percent()`, `battery_is_low()`,
and `temperature_is_overtemp()` in `firmware/rp2350b/src/battery_monitor.c`.

### Test assertion failures in `test_battery_helpers`

The battery percentage curve is piecewise-linear with four segments
(3000–3300, 3300–3700, 3700–4200, and clamp). Common mistakes:
- `battery_percent(3900)` returns **70** (not 80) — the 3700–4200
  segment computes `(3900-3700)*50/500 + 50 = 70`.
- `battery_percent(3500)` returns **30** — the 3300–3700 segment
  computes `(3500-3300)*40/400 + 10 = 30`.
- `battery_percent(3200)` returns **6** — the 3000–3300 segment
  computes `(3200-3000)*10/300 = 6` (integer division).

### Telemetry flag assertions fail

The telemetry flag bits are defined in `test_libapex_framing.c`:

| Bit | Mask | Flag |
|-----|------|------|
| 0 | `0x01` | `TELEM_FLAG_SDR_RX_ACTIVE` |
| 1 | `0x02` | `TELEM_FLAG_SDR_TX_ACTIVE` |
| 2 | `0x04` | `TELEM_FLAG_CC1101_RX` |
| 3 | `0x08` | `TELEM_FLAG_CC1101_TX` |
| 4 | `0x10` | `TELEM_FLAG_NFC_ACTIVE` |
| 5 | `0x20` | `TELEM_FLAG_NFC_TAG_PRESENT` |
| 6 | `0x40` | `TELEM_FLAG_OVERTEMP` |
| 7 | `0x80` | `TELEM_FLAG_LOW_BATTERY` |

A common error is encoding `NFC_ACTIVE | SDR_RX_ACTIVE | LOW_BATTERY`
as `0x85` (which omits bit 4) instead of `0x91`.

---

## Hardware / BOM

### Duplicate reference designators in the BOM

Each reference designator (U-number, R-number, etc.) must be unique.
A previous version of the BOM assigned `U10` to both the eMMC
(`THGBMJG6C1LBAB7`) and the battery DC-DC converter (`TPS63020`).
The TPS63020 is now `U16`. Run `tools/validate_netlist.py` after
editing the BOM to catch such collisions:

```bash
python3 tools/validate_netlist.py
```

### Component appears in netlist but not in components section

The KiCad netlist (`hardware/kicad/ghostblade.net`) must include a
`(comp (ref "..."))` entry for every reference designator used in a
`(node (ref "..."))` net entry. Missing component definitions cause
ERC "unresolved reference" warnings. The `validate_netlist.py` tool
cross-checks the manifest, netlist, DTS, and firmware pin definitions.

### Which SPI bus is the CC1101 on?

The CC1101 sub-GHz radio is on the RP2350B's **SPI2** bus — a dedicated
bus separate from the LMS7002M SDR (which is on SPI1). The manifest
(`GhostBlade.mf`), `board_pins.h`, and KiCad netlist all agree on this.
Earlier versions of the documentation incorrectly described the CC1101
as sharing SPI1 with the LMS7002M; these references have been corrected.

### Which bus connects the MT7922 Wi-Fi to the RK3576?

The MT7922 Wi-Fi 6E module is connected via **SDIO** (4-bit, SDR104 at
up to 200 MHz) to the RK3576, not PCIe. Bluetooth on the MT7922 is
connected via UART1 at 3 Mbps with hardware flow control. The base DTS
(`ghostblade-rk3576.dts`) defines the MT7922 under the `&sdio` node.

### Why does the ST25R3916 NFC node not have an interrupt-parent?

The ST25R3916 is controlled entirely by the RP2350B coprocessor, not the
RK3576. Its interrupt line connects to RP2350B GPIO44 (PIN_NFC_IRQ).
The RK3576 never directly interfaces with the ST25R3916 — all NFC
operations are proxied through the SPI bridge protocol. Therefore the
DTS overlay does not reference any RK3576 GPIO interrupt-parent.