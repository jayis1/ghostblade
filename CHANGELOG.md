# Changelog

All notable changes to the GhostBlade (Project NullSpectre) project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Hardware revisions follow CERN-OHL-S v2 version numbering. Firmware and software follow GPL-2.0-or-later versioning.

---

## [Unreleased]

### Added

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