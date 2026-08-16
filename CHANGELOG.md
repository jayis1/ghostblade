<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Changelog

All notable changes to the GhostBlade (Project NullSpectre) project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Hardware revisions follow CERN-OHL-S v2 version numbering. Firmware and software follow GPL-2.0-or-later versioning.

---

## [Unreleased]

### Fixed

- **LMS7002M PLL calculation uint32 overflow**: `lms7002m_calc_pll_params()` multiplied `vco_freq` (up to 3.8 GHz) by 2 before casting to `uint64_t`, causing silent overflow when VCO frequency exceeded 2.15 GHz (UINT32_MAX/2). Fixed by casting to `uint64_t` before the multiply, ensuring correct PLL programming at all supported frequencies.
- **NFC RX scratch buffer incomplete wipe**: `handle_cmd_nfc_transact()` in `spi_protocol.c` only wiped `rx_len` bytes of the `nfc_rx_scratch` buffer after each transaction, leaving residual tag data (UID, keys) from longer transactions in the tail. Fixed to wipe the full `SPI_NFC_MAX_RX_DATA` (256) bytes. Also added secure wipe of the response buffer and telemetry struct.
- **ST25R3916 field strength measurement polled wrong IRQ bit**: `st25r3916_get_field_strength_mv()` polled IRQ_STATUS1 bit 0 (OSC — oscillator on) to detect measurement completion, but this bit indicates oscillator status, not measurement completion. Replaced with a bounded delay loop since the ST25R3916 does not provide a dedicated measurement-complete IRQ.
- **Dead code in SPI0 ISR frame assembly**: Removed impossible `pos > SPI_HDR_SIZE` check in `spi0_process_byte()` FRAME_STATE_HEADER case — the condition could never be true since `pos` increments by 1 and the `== SPI_HDR_SIZE` check catches the transition.
- **pyapex unused variable**: Removed unused `rx_len` variable in `ApexBridge_nfc_transact()` that triggered cppcheck unreadVariable warning.

### Added

- **Critical battery graceful shutdown**: Main loop now detects VBAT < 3000 mV and initiates graceful shutdown — stops SDR DMA, puts CC1101 to sleep, stops NFC polling, powers off non-essential rails (SDR 3V3, NFC, Sub-GHz), and logs a critical message. This preserves remaining battery for watchdog and brownout detection circuitry.
- **Overtemperature event counter sysfs attribute**: Added `overtemp_count` sysfs attribute to the kernel driver, counting rising-edge transitions of the OVERTEMP telemetry flag. Complements the existing `overtemp` (current state) attribute with a historical count for diagnostics.
- **Interrupt event counter sysfs attribute**: Added `irq_count` sysfs attribute counting INT_REQ GPIO interrupt events since driver load. Useful for diagnosing communication issues — a high IRQ count with low frame count indicates SPI transfer failures.
- **Secure memory wipe in pyapex NFC transaction**: Added `explicit_bzero()` (with `memset` fallback) to wipe the NFC transaction struct after building the Python response object, preventing sensitive tag data from persisting on the C stack.
- **LMS7002M gain clamping**: `lms7002m_set_rx_gain()` now clamps `gain_db_x10` to the valid range [0, 730] (0–73 dB) to prevent out-of-range values from programming invalid LNA/TIA register settings.
- **LMS7002M bandwidth validation**: `lms7002m_set_rx_bandwidth()` now rejects zero bandwidth to prevent invalid baseband filter configuration.
- **sysfs documentation for new attributes**: Documented `overtemp_count`, `irq_count`, and `sg_frames_crc_err` attributes in `docs/sysfs-attributes.md`. Updated troubleshooting table with diagnostic guidance for the new counters.

### Added

- **6 missing KiCad footprints**: `Crystal_3.2x1.5mm`, `Crystal_3.2x2.5mm`, `Inductor_2x1.6mm`, `Inductor_4x4mm`, `FPC_30pin_0.4mm`, `FPC_40pin_0.4mm` — all referenced in the BOM but previously absent from the footprint library. Added with complete pad definitions, silkscreen outlines, and courtyard boundaries.
- **7 missing KiCad symbol definitions**: `TPS63020`, `TLV75533`, `TLV75518`, `NCP303`, `W25Q128JVS`, `SY8120B`, `THGBMJG6C1LBAB7` — all present in the BOM and netlist components section but previously missing from the symbol library. Added with correct pin definitions, power pin types, and footprint references.
- **13 missing 3D model references** in `hardware/kicad/3dmodels/README.md` for TLV75533, TLV75518, NCP303, SY8120B, crystals, inductors, FPC connectors, tactile switch, LED 0402, and TVS diode.

### Fixed

- **Documentation GPIO reference errors**: FAQ troubleshooting and flashing guide incorrectly referenced "GPIO25 (INT_REQ)" and "GPIO24 (HOST_RDY)" — these are RK3576 GPIO bank numbers that don't match the actual RP2350B pin assignments. Corrected to reference RP2350B pin 20 (INT_REQ → RK3576 GPIO1_B0) and pin 21 (HOST_RDY → RK3576 GPIO1_B1).
- **Reset circuit design doc**: Incorrectly described a dedicated "GPIO25 (NFC_RESET)" line to the ST25R3916. The ST25R3916 actually uses power-on reset via the NFC power rail (POWER_RAIL_NFC, GPIO 11) and SPI SET_DEFAULT command for soft reset. Rewrote the section to accurately describe the power-cycle reset mechanism.
- **Power sequencing timing doc**: Brownout shutdown sequence referenced incorrect GPIO numbers (GPIO28-30 for SDR rails, GPIO22-24 for peripherals). Corrected to match the actual `PWR_GPIO_*` definitions in `peripheral_power.c`: GPIO 6/7/9 for SDR 1V8/1V1/3V3, GPIO 11 for NFC, GPIO 22 for Sub-GHz, GPIO 25 for SDIO. Also fixed "ADC GPIO26" to "ADC channel 0" and "GPIO1_B1" to "Pin 21" for the HOST_RDY deassert.

### Added

- **SDR DMA engine integration in SPI protocol handler**: `handle_cmd_sdr_stream()` in `spi_protocol.c` now calls `sdr_dma_start()` / `sdr_dma_stop()` when the host sends `CMD_SDR_STREAM` with enable=1/0. Previously, the handler only toggled GPIO enables (`apex_sdr_rx_enable`, `apex_sdr_lna_enable`) but never started the DMA ring buffer engine, so no IQ data would ever be captured or sent to the host. The fix also calls `sdr_dma_set_frequency()` from `handle_cmd_sdr_tune()` to keep the DMA engine's frequency tracking in sync with tuning commands.
- **ESD protection components in KiCad netlist**: 8 TVS diode components (D1-D8) added to `ghostblade.net` covering all external connectors: USB-C (TPD4E05U06), SMA antenna ports ×2 and sub-GHz u.FL (BGS8N4 RF ESD protectors), NFC antenna (PRTR5V0U2X), microSD (TPD4E05U06), MIPI-CSI-2 camera (TPD4E05U06), and HDMI 2.1 TX (TPD4E05U06). All components include manufacturer, MPN, footprint, and datasheet references.
- **Test points in KiCad netlist**: 12 test point components (TP1-TP12) added to `ghostblade.net` for debugging and manufacturing: debug UART TX/RX/GND/3V3, SPI bridge SCK/MOSI/MISO/CS, power rails VBAT/1V8/SDR_1V2, and INT_REQ signal. Debug UART nets (code 160-161) added with RK3576 UART0 pin connections.
- **Telemetry flag bit mapping test** (test 33): Verifies all 10 telemetry flag bits are unique single-bit values, combine to 0x03FF, and round-trip correctly through the SPI frame builder/validator.
- **NFC response status code test** (test 34): Validates all 5 NFC status codes (OK, TIMEOUT, CRC_ERR, BAD_PARAMS, NOT_READY) are distinct and round-trip correctly through the SPI frame with the correct wire format (status + cmd_echo + rx_len + rx_data).
- **Antenna select validation test** (test 35): Tests all 4 valid antenna IDs (MIMO_TX, MIMO_RX, SUBGHZ, TERMINATED) with round-trip frame validation and constant value verification.
- **CC1101 burst config test** (test 36): Tests a 5-register burst write frame (IOCFG2 through SYNC1) with full payload round-trip and field-level verification.
- **DMA continuous streaming overrun recovery test** (test 16): Simulates 20-block overrun phase, consumer recovery drain, and 50-block balanced streaming phase — verifies overrun counting, data continuity, and correct total block counts.
- **DMA pointer alignment multi-cycle test** (test 17): Runs 3 complete fill-drain cycles (7 blocks each) and verifies write/read pointer equality and zero overruns across multiple wrap-around events.
- **SDR streaming documentation**: Updated `docs/spi-protocol-timing.md` section 3.2 with detailed dual-core architecture description, DMA engine start/stop sequence, Core 1 processing loop, and overrun handling notes.

### Fixed

- **Critical: SDR stream command never started DMA engine** — `handle_cmd_sdr_stream()` in `spi_protocol.c` toggled `apex_sdr_rx_enable()` and `apex_sdr_lna_enable()` but never called `sdr_dma_start()` / `sdr_dma_stop()`. This meant that when the host sent `CMD_SDR_STREAM` with enable=1, the LMS7002M RX path was enabled but the DMA ring buffer engine was never started, so no IQ samples would be captured from SPI1 and no `CMD_SDR_IQ_CHUNK` frames would ever be sent to the host. The fix adds `sdr_dma_start()` on enable and `sdr_dma_stop()` on disable, completing the data path from LMS7002M → DMA ring → SPI0 TX → RK3576.

### NFC transaction response path: The `CMD_NFC_TRANSACT` (0x05) handler in `spi_protocol.c` now actually calls `st25r3916_transact()` instead of a TODO stub, forwarding the host's NFC command and TX payload to the ST25R3916 and returning the RX response. A new `CMD_NFC_RESPONSE` (0x83) MCU→Host opcode carries the response (status byte + echoed command + RX length + RX data) back to the host. The kernel driver (`apex_bridge.c`) now recognizes `APEX_CMD_NFC_RESPONSE` and pushes it to the RX FIFO for userspace `read()` / `apex_nfc_transact()`.
- `st25r3916_is_ready()` function and `g_nfc_ready` flag in `st25r3916_init.c` — the SPI protocol handler checks this before issuing NFC transactions and reports `SPI_NFC_STATUS_NOT_READY` to the host instead of touching an unresponsive chip. Set on successful `st25r3916_init()`, cleared on failure.
- `SPI_NFC_STATUS_*` constants (`OK`, `TIMEOUT`, `CRC_ERR`, `BAD_PARAMS`, `NOT_READY`) defined in `spi_protocol.h` and mirrored in `apex_bridge_regs.h` for the NFC response wire format.
- New `CMD_NFC_RESPONSE` opcode added to the SPI protocol unit tests, kernel test harness (`test_apex_bridge.c`), and `test_libapex_framing.c` — round-trip framing and opcode distinctness checks now cover the new opcode.
- `NFC_RESPONSE` payload format documented in `docs/memory-map.md` with the full status byte table and wire layout.

- `vdd_ddr` fixed regulator node (1.1V) added to base DTS — the manifest references `VDD_DDR 1.1V (BUCK3)` but the DTS was missing this regulator definition
- `#include <dt-bindings/gpio/gpio.h>` and `#include <dt-bindings/input/linux-event-codes.h>` added to base DTS — required for `GPIO_ACTIVE_LOW`, `IRQ_TYPE_*`, `KEY_RESTART`, `KEY_F24` macros used in `gpio_keys` and pinctrl nodes
- `&pcie_clk_pins` pinctrl reference added to `&pcie` node — the `pcie_clk_pins` group was defined but never referenced
- `&usb_pins` pinctrl reference added to `&usb3_otg` node — the `usb_pins` group was defined but never referenced
- `&ant_sel_pins` pinctrl reference added to `apex_bridge` node — the `ant_sel_pins` group was defined but never referenced
- FAQ entries added: CC1101 SPI bus topology (SPI2, not shared SPI1), MT7922 Wi-Fi bus type (SDIO, not PCIe), ST25R3916 NFC interrupt routing (RP2350B, not RK3576)
- Missing SPDX license headers added to `docs/getting-started.md` and `docs/power-tree.md`
- All 7 DTS overlay DTBO outputs listed in build instructions (previously missing cc1101, sleep, and GPS overlays)
- All 15 test targets listed in build instructions (previously missing 6 test targets)

### Fixed

- **Critical: Missing closing brace in `main.c`** — the `if (init_peripherals() != 0)` block was missing its `}` after `watchdog_reboot(true)`, causing the "Peripherals initialized" message and all subsequent initialization code to be unreachable inside the fatal-error branch. This would have caused a compile error or silent boot failure
- Wi-Fi overlay: "Bluetooth 5.3" corrected to "Bluetooth 5.4" to match manifest, BOM, and all other documentation
- Wi-Fi overlay: Fragment 3 corrected from `&pcie0` (nonexistent node) to `&sdio` — the MT7922 is on SDIO in the base DTS, not PCIe
- Wi-Fi overlay: Bus description corrected from "PCIe" to "SDIO" throughout
- NFC overlay: Removed `interrupt-parent = <&gpio1>` and `interrupts = <44 IRQ_TYPE_EDGE_FALLING>` from ST25R3916 node — the ST25R3916 interrupt goes to RP2350B GPIO44, not RK3576 GPIO1_44. The RK3576 cannot receive this interrupt
- NFC overlay: Removed `spi-max-frequency` property from ST25R3916 node (not an RK3576 SPI device)
- NFC overlay: NFC field regulator GPIO changed from `<&gpio1 22>` (RK3576) to `<0>` placeholder — this is an RP2350B-controlled GPIO
- SDR overlay: Removed duplicate CC1101 configuration fragment — the SDR overlay had a full CC1101 config fragment that duplicated the dedicated `ghostblade-cc1101-overlay.dts`. Replaced with a cross-reference node
- CC1101 overlay: Comment corrected from "shared SPI1 bus" to "SPI2 bus (dedicated, separate from LMS7002M SDR SPI1)"
- `board_pins.h`: CC1101 SPI bus comments corrected from "shared SPI1 bus" to "SPI2 bus" to match manifest and KiCad netlist
- `getting-started-guide.md`: Bluetooth version corrected from "5.3" to "5.4"
- Documentation: All "shared SPI1" references for CC1101 corrected to "SPI2" across `glossary.md`, `gpio-cross-reference.md`, `pin-assignments.md`, `memory-map.md`, `architecture.md`
- Glossary: Removed duplicate SPI2 entry
- `sdr_dma_get_underrun_count()` function and header declaration for API completeness, complementing the existing `sdr_dma_get_overrun_count()`
- 15 missing KiCad footprint definitions added to `ghostblade-footprints.kicad_mod`: BGA-153 (eMMC), SOT-23-5 (LDOs/supervisor), SOT-23-6 (DC-DC), SOP-8 (SPI flash), VQFN-14 (buck-boost), USB-C 24-pin, MicroSD push-push, SOT-23 (TVS diodes), R_0402, C_0402, C_1206, LED_0402, Crystal_HC49_SMD, Pin_Header_2x05_1.27mm (JTAG), Switch_Tactile_6x3.5mm — all BOM-referenced footprints now have definitions
- 3D model references added to new footprints: `eMMC.step`, `W25Q128JVS.step`, `TPS63020.step`, `USB-C_24pin.step`
- GPS device tree overlay (`ghostblade-gps-overlay.dts`) for optional u-blox NEO-M10N on UART2 + I2C2 with 1PPS time synchronization
- Pinctrl references added to `gpio_keys`, `leds`, and `sdio` DTS nodes — previously defined pinctrl groups were not referenced by their consumer nodes
- `test_libapex_framing` added to `.gitignore` (was missing, causing tracked binary)
- DTS validator (`tools/validate_dts.py`) improved: multi-reference pinctrl-0 parsing (`<&foo &bar>`) and hyphenated pinctrl label matching (`gpio-keys-pins`)

### Fixed

- `battery_monitor.c`: Removed duplicate definitions of `battery_monitor_init()`, `battery_monitor_update()`, `battery_monitor_get_vbat_mv()`, `battery_monitor_get_temp_c_x10()`, and `battery_voltage_to_percent()` that were accidentally introduced when the functions were already present earlier in the file — duplicate symbols would cause link errors
- `sdr_dma.c`: Removed `DMA_CTRL_IRQ_QUIET` from DMA channel 0 control word — this bit suppresses the per-channel completion interrupt, preventing the DMA IRQ handler from firing and the ring buffer from advancing
- `rp2350b_init.c`: Added 256-iteration safety limit to SPI0 ISR FIFO drain loop to prevent infinite loops if the RNE bit is stuck due to hardware fault
- `apex_bridge.c`: Fixed memory leak in `APEX_IOC_SOFT_RESET` handler — was `kmalloc`-ing temporary frame/RX buffers instead of reusing the already-allocated `frame`/`rx_buf` from the ioctl function scope
- `apex_bridge.c`: Added `overtemp` sysfs attribute (`/sys/class/apex/.../overtemp`) exposing the `APEX_FLAG_OVERTEMP` telemetry flag for userspace thermal monitoring
- `main.c`: Added `lms7002m_init()` call to peripheral initialization sequence and `lms7002m_read_rssi()` for SDR RSSI telemetry — replaces the TODO placeholder
- `battery_monitor.c`: Added missing public API function implementations (`battery_monitor_init`, `battery_monitor_update`, `battery_monitor_get_vbat_mv`, `battery_monitor_get_temp_c_x10`, `battery_voltage_to_percent`) that were declared in the header and called from `main.c` but were absent from the .c file, causing unresolved symbol link errors
- `spi_protocol.c`: Removed dead `rx_ring`/`rx_head`/`rx_tail` variables (8 KB of unused SRAM) — the actual SPI0 RX ring buffer lives in `rp2350b_init.c` as `spi_rx_buf`/`spi_rx_head`/`spi_rx_tail`; the local copies were never written by the ISR and confused the code
- `spi_protocol.c`: Guarded `PIN_INT_REQ` redefinition with `#ifndef` to avoid macro redefinition warning when `board_pins.h` is included in the same translation unit
- `main.c`: Replaced `watchdog_enable(WATCHDOG_TIMEOUT_MS, true)` (Pico SDK function not available in bare-metal register-level build) with `watchdog_reboot(true)` in the fatal-error path
- `main.c`: Fixed incorrect comment stating INT_REQ is "active-high" — it is active-low; driving LOW asserts the interrupt
- `main.c`: Fixed comment "Reassert INT_REQ" to "Deassert INT_REQ" when brownout condition clears (driving HIGH = inactive for active-low signal)
- `GhostBlade.mf` manifest: CC1101 SPI bus labels corrected from `SPI1_SCK`/`SPI1_TX`/`SPI1_RX` to `SPI2_SCK`/`SPI2_TX`/`SPI2_RX` — matches KiCad netlist and `board_pins.h` (CC1101 is on a separate SPI2 bus, not the shared SDR SPI1 bus)
- `tools/validate_dts.py`: regex for pinctrl group definitions now correctly matches hyphenated label names (e.g. `gpio-keys-pins`, `bridge-gpio-pins`)

### Previous Added

- Netlist cross-reference validation tool (`tools/validate_netlist.py`) — checks GhostBlade.mf manifest nets against KiCad netlist, DTS GPIO assignments, board_pins.h pin definitions, and 3D model references
- `validate-netlist` target in top-level Makefile for integrated netlist verification
- SPDX-License-Identifier and copyright headers added to `tools/validate_dts.py`, `tools/check_links.py`, `tools/check_internal_links.py`
- FAQ & Troubleshooting expanded with test build guidance, battery percentage curve reference, telemetry flag bit table, and BOM/netlist reference designator troubleshooting
- libapex Makefile now installs the `libapex.pc` pkg-config file to `$(PREFIX)/lib/pkgconfig/` and removes it on `uninstall`

### Fixed

- `tests/test_libapex_framing.c` — battery helper functions (`battery_percent`, `is_low_battery`, `is_overtemp`) were declared as block-scope prototypes instead of file-scope `static` definitions, causing linker errors. Now defined at file scope with logic matching `firmware/rp2350b/src/battery_monitor.c`.
- `tests/test_libapex_framing.c` — telemetry test data corrections: `rssi_dbm_x10` byte changed from `0x38` to `0x3E` to encode -450 correctly, flags byte changed from `0x85` to `0x91` to correctly set `NFC_ACTIVE` (bit 4), and `battery_percent(3900)` expected value corrected from 80 to 70 to match the firmware piecewise-linear curve.
- `hardware/bom/ghostblade-bom.csv` and `ghostblade-bom-interactive.html` — duplicate `U10` reference designator resolved: TPS63020 DC-DC converter renumbered from `U10` to `U16` (the eMMC `THGBMJG6C1LBAB7` retains `U10`).
- `hardware/kicad/ghostblade.net` — VBAT net node updated to reference `U16` (TPS63020) instead of `U10`. Missing component definitions for U10 (eMMC), U11 (TLV75533), U12 (TLV75518), U13 (NCP303), U14 (W25Q128JVS), U15 (SY8120B), and U16 (TPS63020) added to the components section to resolve ERC "unresolved reference" warnings.
- `hardware/kicad/ghostblade.kicad_pro` — project comment corrected from "Project Cyber-Swiss" to "Project NullSpectre".
- `README.md` and `stats.json` — BOM component count corrected from "80+" / 66 to 67 (actual line-item count).

### Changed

- README repository structure updated with all current tools (`check_links.py`, `check_internal_links.py`, `validate_dts.py`, `validate_netlist.py`)
- Top-level Makefile help text updated to include `validate-netlist` target
- `stats.json` updated with current line counts and file totals
- CC1101, NFC, Wi-Fi, and sleep DTS overlay references added to system manifest (`GhostBlade.mf`)
- LMS7002M driver, sleep/wake, peripheral power, and ADC calibration source files added to README repository structure
- SPDX license headers verified on all source files
- Stats updated with current line counts

### Changed

- Cross-compilation toolchain configuration (`software/toolchain.conf`) with `ghostblade_check_toolchain` helper
- `make check` target in top-level Makefile for toolchain availability verification
- Mermaid power-on sequencing diagram to `docs/power-tree.md`
- Mermaid memory map overview diagram to `docs/memory-map.md`
- Mermaid SPI bridge transaction flow diagram to `docs/spi-protocol-timing.md`
- DTS verification cross-reference table to `docs/pin-assignments.md`
- Hardware bring-up checklist and ESD protection docs linked from `docs/index.md`
- Missing test files (`test_adc_calibration.c`, `test_peripheral_power.c`, `test_cc1101_lms7002m.c`) added to README repo structure
- Updated `stats.json` with current line counts and file totals
- `docs/timing-diagrams.md` — Mermaid sequence diagrams for cold boot, warm reset, power-down, SPI bridge, SDR DMA streaming, NFC transactions, CC1101 TX/RX, sleep/wake state machine, watchdog recovery, and brownout detection
- `docs/board-quickstart.md` — TL;DR quick-start guide (unpowered to operational in 10 minutes)
- `software/dts/ghostblade-sleep-overlay.dts` — Device tree overlay for sleep/wake power state management (idle, light sleep, deep sleep, brownout thresholds, thermal scaling)
- `VERSION` — Project version file (0.1.0-dev) for reproducible builds
- `.gitattributes` — Line-ending normalization (LF) and binary file markers for consistent cross-platform development
- DTS: Added `vbat_reg`, `vdd_sdio` fixed regulators, `tsadc` node (thermal shutdown at 105°C), and `saradc` node (battery voltage monitoring) to base DTS
- DTS: Added `tsadc_pins` and `saradc_pins` pinctrl entries to base DTS
- DTS Makefile: Added `ghostblade-sleep-overlay.dts` to overlay build targets
- `docs/index.md` — Added links to board-quickstart, getting-started-guide, and timing-diagrams
- README — Added links to new documentation files and sleep overlay in repo structure

- LMS7002M SDR transceiver driver for RP2350B (`firmware/rp2350b/src/lms7002m_driver.c`, `include/lms7002m_driver.h`)
  - PLL frequency synthesis with VCO_L (1.88–3.72 GHz) and VCO_H (3.72–5.8 GHz) range selection
  - RX/TX gain distribution across LNA (0–73 dB), TIA (12 dB fixed), and PGA (0–31 dB) stages
  - ADC/DAC sample rate configuration with decimation/interpolation (100 kSPS – 10 MSPS)
  - DC offset and IQ imbalance calibration routines
  - SPI register access (single and burst modes) with 4-byte command framing
  - FIFO-based IQ data streaming with configurable watermark
  - Channel selection (A/B) for MIMO operation
- CC1101 and LMS7002M initialization unit tests (`tests/test_cc1101_lms7002m.c`, 315 assertions)
  - CC1101: register address range, duplicate detection, frequency calculation, SPI encoding, PKTCTRL0, sync words, FIFO threshold, data rate, table completeness
  - LMS7002M: PLL calculation (868/433/915/2400 MHz), out-of-range rejection, SPI encoding, gain distribution, decimation selection
- Fixed LMS7002M PLL parameter calculation to use correct VCO range (1.88–5.8 GHz covering both VCO_L and VCO_H)
- Fixed LMS7002M NINT range to 8-bit (1–255) per LMS7002M datasheet

- Top-level `Makefile` for convenient project-wide builds (firmware, driver, libapex, tests, DTS)
- DTS Makefile (`software/dts/Makefile`) for compiling and validating device tree sources
- Unit tests for battery monitor, CC1101 configuration, watchdog timer, and power state machine
- SDR DMA ring buffer unit tests (`tests/test_sdr_dma.c`, 52 assertions)
- SPI0 ISR frame assembly unit tests (`tests/test_spi0_isr.c`, 42 assertions)
- SPI0 slave interrupt handler firmware module (`firmware/rp2350b/src/spi0_isr.c`, `include/spi0_isr.h`)
- HIL (hardware-in-the-loop) SPI bridge test script (`tests/hil_spi_bridge_test.sh`)
- ST25R3916 NFC controller initialization unit tests (`tests/test_st25r3916_init.c`, 955 lines)
- Contributor onboarding guide (`docs/getting-started-contributors.md`)
- Pin assignment cross-reference document (`docs/pin-assignments.md`)
- ESD protection, reset circuits, and test points document (`docs/hardware-protection-and-testpoints.md`)
- CC1101 multi-band configuration tables for 433 MHz and 915 MHz ISM bands
- `cc1101_set_band()` API for runtime band switching (433/868/915 MHz)
- Multi-band frequency verification tests in `test_cc1101_config.c`
- `stats.json` updated with current line counts and file counts
- `SECURITY.md` — responsible disclosure policy for the pentesting hardware project

### Changed

- `.gitignore` updated to include all test binary targets (test_battery_monitor, test_cc1101_config, test_watchdog, test_power_states, test_sdr_dma, test_spi0_isr, test_libapex, test_st25r3916_init)
- `.gitignore` updated to include firmware build outputs (*.uf2, *.hex, *.bin, *.elf, *.map)
- `firmware/rp2350b/CMakeLists.txt` — added `spi0_isr.c` and `spi0_isr.h` to build
- `README.md` — updated repository structure and documentation index with new files (SPI0 ISR, SDR DMA tests, libapex tests, pin assignments doc, contributing guides)
- `docs/index.md` — added Contributing section and pin assignments link
- `docs/build-instructions.md` — added test_sdr_dma, test_spi0_isr, test_libapex to test build commands
- `docs/getting-started.md` — added SDR DMA and SPI0 ISR test sections
- `docs/contributing.md` — updated repository structure (fixed stale `netlists/` → `bom/` + `drc/`), updated "Areas That Need Help" to reflect completed SPI0 ISR and SDR DMA work
- `tests/README.md` — added test_libapex documentation section
- `tests/Makefile` — added `test_st25r3916_init` target and run target
- `stats.json` — updated line counts and file counts

---

## [0.1.0] — 2026-06-14

### Added

- RK3576 + RP2350B dual-processor hardware design (6-layer FR-4, IPC Class 3)
- LMS7002M SDR (100 kHz – 3.8 GHz, 2×2 MIMO)
- CC1101 sub-GHz radio (300–928 MHz, OOK/FSK/GFSK)
- ST25R3916 NFC controller (ISO 14443 A/B, 15693, FeliCa)
- MT7922 Wi-Fi 6E / BT 5.4
- RP2350B firmware with SPI bridge protocol, SDR DMA, CC1101 init, ST25R3916 init, battery monitor, watchdog
- Linux kernel SPI bridge driver (apex_bridge) with sysfs telemetry attributes
- libapex userspace C library and Python bindings
- KiCad 8 hardware design files (schematics, PCB, symbols, footprints, netlist, DRC rules)
- BOM (80+ components, interactive HTML)
- Device tree sources for RK3576 (base, options overlay, SDR overlay)
- Comprehensive documentation (getting started, build instructions, flashing guide, FAQ, power tree, SPI protocol timing, sysfs attributes, hardware test procedures, hardware contributor guide)
- Engineering phase documents (architecture/requirements, component selection/schematics, PCB layout, boot process/MMIO)
- Gerber generation script with fabrication notes
- Unit tests for SPI protocol (158 tests)
- `.clang-format`, `.editorconfig`, `.markdownlint.json`, `.codespell.ignore`

[Unreleased]: https://github.com/jayis1/ghostblade/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/jayis1/ghostblade/releases/tag/v0.1.0