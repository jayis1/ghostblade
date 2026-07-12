<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade Contributor's Guide

Welcome to the GhostBlade (Project NullSpectre) open-source hardware project! This guide will help you get started contributing to the dual-processor pentesting device with RK3576 + RP2350B, LMS7002M SDR, CC1101 sub-GHz, ST25R3916 NFC, and MT7922 Wi-Fi 6E.

## Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade
```

### 2. Directory Layout

```
ghostblade/
├── firmware/rp2350b/        # RP2350B coprocessor firmware
│   ├── src/                 #   Source files (C)
│   └── include/             #   Header files
├── hardware/                # KiCad schematic and layout files
├── software/
│   ├── linux-drivers/       # Kernel drivers for RK3576
│   │   └── src/
│   └── libapex/             # Userspace library with Python bindings
│       ├── src/
│       └── python/
├── docs/                    # Documentation
└── tests/                   # Unit and integration tests
```

### 3. Build and Run Tests

All userspace tests use a simple Makefile-based build system:

```bash
cd tests
make                # Build all userspace tests
make run            # Build and run all userspace tests
make check          # Same as 'make run'

# Build individual tests
make test_spi_protocol
make test_sdr_dma
make test_spi0_isr
make test_st25r3916_init

# Run individual tests
./test_spi_protocol
./test_sdr_dma
./test_spi0_isr
./test_st25r3916_init
```

The kernel module test (`test_apex_bridge.c`) must be built and loaded on the RK3576 target:

```bash
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
insmod test_apex_bridge.ko
echo 1 > /sys/kernel/test_apex_bridge/run
cat /sys/kernel/test_apex_bridge/result
```

### 4. Run the Hardware-in-the-Loop (HIL) Test

The HIL test requires a GhostBlade device with the firmware running:

```bash
cd tests
./hil_spi_bridge_test.sh           # Run all HIL tests
./hil_spi_bridge_test.sh --quick   # Quick smoke tests
./hil_spi_bridge_test.sh --loop    # Run continuously until failure
```

## Architecture Overview

### SPI Bridge Protocol

The RK3576 (Linux host) and RP2350B (MCU coprocessor) communicate via SPI using a framed protocol:

```
Offset  Size  Field
0x00    1     SYNC (0xAA)
0x01    1     CMD (command opcode)
0x02    2     LEN (payload length, little-endian)
0x04    4     RESERVED (must be 0x00000000)
0x08    8     HDR_CRC (CRC-64/ECMA-182 over bytes 0-7, little-endian)
0x10    N     PAYLOAD (0 to 4092 bytes)
0x10+N  4     CRC32 (CRC-32/ISO-3309 over payload, little-endian)
```

**Key properties:**
- CRC-64 (reflected ECMA-182) protects the header; CRC-32 (ISO 3309) protects the payload
- Minimum frame: 20 bytes (NOP with no payload)
- Maximum frame: 4112 bytes (4092-byte payload + 20-byte overhead)
- Single-bit-flip errors are always detected by both CRC algorithms

See [`docs/spi-protocol-timing.md`](spi-protocol-timing.md) for detailed timing diagrams.

### SDR DMA Ring Buffer

The RP2350B receives IQ samples from the LMS7002M SDR via DMA and places them into an 8-block ring buffer (8 × 512 bytes = 4096 bytes total). The SPI protocol handler reads completed blocks and streams them to the RK3576 host.

Key files:
- [`firmware/rp2350b/src/sdr_dma.c`](../firmware/rp2350b/src/sdr_dma.c) — Ring buffer manager and DMA ISR handler
- [`firmware/rp2350b/include/sdr_dma.h`](../firmware/rp2350b/include/sdr_dma.h) — Ring buffer API

### SPI0 ISR Frame Assembly

The RP2350B SPI0 slave receives bytes from the RK3576 host. The ISR assembles bytes into complete frames using a state machine:

```
IDLE → (sync byte 0xAA) → HEADER → (16 bytes, CRC-64 valid) → PAYLOAD → (payload + CRC-32) → COMPLETE
                                                                              ↓ (bad CRC)
                                                                            ERROR → (sync byte) → HEADER
```

Key files:
- [`firmware/rp2350b/src/spi0_isr.c`](../firmware/rp2350b/src/spi0_isr.c) — ISR handler and frame assembly
- [`firmware/rp2350b/include/spi0_isr.h`](../firmware/rp2350b/include/spi0_isr.h) — ISR public API

### Command Opcodes

| Opcode | Direction | Name | Description |
|--------|-----------|------|-------------|
| `0xFF` | Host→MCU | NOP | No operation (ping/keepalive) |
| `0x01` | Host→MCU | SDR_TUNE | Configure SDR frequency/gain |
| `0x02` | Host→MCU | SDR_STREAM | Start/stop SDR IQ streaming |
| `0x03` | Host→MCU | ANT_SELECT | Select antenna port (1-4) |
| `0x04` | Host→MCU | CC1101_CFG | Configure sub-GHz radio |
|| `0x05` | Host→MCU | NFC_TRANSACT | Execute NFC transaction |
|| `0x06` | Host→MCU | TELEMETRY_REQ | Request telemetry data |
|| `0x07` | Host→MCU | RESET_MCU | Reset MCU (requires magic 0x52534554) |
|| `0x81` | MCU→Host | TELEMETRY | Telemetry response |
| `0x82` | MCU→Host | SDR_IQ_CHUNK | IQ sample chunk |

MCU→Host commands have bit 7 set (`0x80`); Host→MCU commands have bit 7 clear (except NOP=`0xFF`).

## Development Guidelines

### Code Style

- **C language**: Follow the Linux kernel coding style for firmware and driver code
- **Headers**: Use `#ifndef` include guards; prefix with project name (e.g., `GHOSTBLADE_SPI_PROTOCOL_H`)
- **Comments**: Use C89-compatible `/* */` style for firmware; C99 `//` is acceptable for test code
- **Naming**: `snake_case` for functions and variables; `UPPER_SNAKE_CASE` for macros and constants
- **License headers**: Add SPDX-License-Identifier and copyright notice to all new files

### Adding a New Command Opcode

1. Add the opcode constant to `firmware/rp2350b/include/spi_protocol.h`
2. Add the command handler in `firmware/rp2350b/src/spi_protocol.c`
3. Add the corresponding `apex_cmd_*` constant in `software/libapex/src/libapex.c`
4. Add unit tests in `tests/test_spi_protocol.c` (CRC validation, round-trip)
5. Update the command table in this README and `docs/spi-protocol-timing.md`

### Adding a New Firmware Module

1. Create the source file in `firmware/rp2350b/src/`
2. Create the header file in `firmware/rp2350b/include/`
3. Add unit tests in `tests/test_<module>.c`
4. Add the test target to `tests/Makefile`
5. Document the module's API in the header file

### Testing

Every code change should include appropriate tests:

- **Protocol/communication code**: Add tests to `test_spi_protocol.c` or `test_spi0_isr.c`
- **Hardware abstraction**: Add tests to the corresponding module's test file
- **NFC controller**: Add tests to `test_st25r3916_init.c`
- **Kernel driver**: Add tests to `test_apex_bridge.c` (must run on target)
- **Userspace library**: Add tests to `test_libapex.c`

All userspace tests should build with:
```bash
gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -g -O2 -o test_<name> test_<name>.c
```

The `-DNO_CMOCKA` flag enables the built-in minimal test framework when cmocka is not available.

### Pull Request Checklist

Before submitting a PR:

- [ ] All existing tests pass (`make run` in `tests/`)
- [ ] New code has corresponding unit tests
- [ ] New header files have include guards and SPDX license identifiers
- [ ] No `.github/workflows/` files or CI references added (this project has no CI)
- [ ] Code follows the project's naming conventions
- [ ] Hardware-specific code is documented with register references
- [ ] SPI protocol changes update both firmware and driver sides

## Key Technical References

### RP2350B (MCU Coprocessor)

- ARM Cortex-M33 dual-core, 150 MHz
- 520 KB SRAM, PSRAM support
- SPI0 slave interface to RK3576
- Hardware watchdog timer
- ADC for battery and temperature monitoring

### RK3576 (Linux Host)

- ARM Cortex-A72 quad-core + Cortex-A53 quad-core
- Runs Linux kernel 6.x
- `apex_bridge` kernel driver provides `/dev/apex_bridge0` device
- `libapex` userspace library with Python bindings

### LMS7002M SDR

- 2×2 MIMO transceiver, 100 kHz – 3.8 GHz
- 12-bit ADC/DAC, up to 160 MHz bandwidth
- Configured via SPI (CMD_SDR_TUNE, CMD_SDR_STREAM)

### CC1101 Sub-GHz Radio

- 300–928 MHz ISM band transceiver
- Up to 500 kBaud data rate
- Configured via SPI (CMD_CC1101_CFG)

### ST25R3916 NFC

- NFC Forum reader, 13.56 MHz
- Supports ISO 14443 A/B, FeliCa, NFC-V
- Configured via SPI (CMD_NFC_TRANSACT)

## Common Tasks

### Updating the SPI Protocol CRC

The CRC-64 and CRC-32 implementations must stay synchronized between:
- `firmware/rp2350b/src/spi_protocol.c` (MCU side)
- `software/linux-drivers/src/apex_bridge.c` (kernel side)
- `software/libapex/src/libapex.c` (userspace side)
- `tests/test_spi_protocol.c` (test side)

If you change the CRC algorithm, update all four files and verify with the known test vectors.

### Adding Sysfs Attributes to the Kernel Driver

The `apex_bridge` driver exposes telemetry via sysfs:

```
/sys/class/apex/apex_bridge0/
├── rssi_dbm_x10          # SDR RSSI (dBm × 10)
├── temp_c_x10             # MCU temperature (°C × 10)
├── vbat_mv                # Battery voltage (mV)
├── cc1101_rssi_x10        # CC1101 sub-GHz RSSI (dBm × 10)
├── nfc_field_mv           # NFC field strength (mV)
├── mcu_flags              # MCU status flags (hex)
├── uptime_ms              # MCU uptime (ms)
├── driver_status          # Driver status flags (hex)
├── spi_errors             # Cumulative SPI error count
├── rx_fifo_count          # RX FIFO bytes pending
├── tx_fifo_count          # TX FIFO bytes pending
├── brownout_count         # Cumulative brownout events
├── low_battery            # Low battery flag (0/1)
├── sg_state               # SG DMA engine state (idle/running/error)
├── sg_total_bytes         # SG total bytes transferred
├── sg_overruns            # SG buffer overrun count
├── sg_errors              # SG transfer error count
├── sg_frames_rx           # SG frames received
├── sg_buf_count           # SG buffer count
└── sg_buf_size            # SG buffer size (bytes)
```

To add a new attribute, add a `DEVICE_ATTR_RO()` or `DEVICE_ATTR_RW()` definition in `apex_bridge.c` and update the attribute group array `apex_bridge_attrs[]`.

See [`docs/sysfs-attributes.md`](sysfs-attributes.md) for complete documentation of all attributes.

### Debugging SPI Communication

1. Use `test_spi_protocol` to verify CRC implementations
2. Use `test_spi0_isr` to verify frame assembly logic
3. On the RK3576, enable dynamic debug:
   ```bash
   echo 'apex_bridge +p' > /sys/kernel/debug/dynamic_debug/control
   dmesg -w | grep apex
   ```
4. Check statistics:
   ```bash
   cat /sys/class/apex/apex_bridge0/sdr_stats
   ```

## Getting Help

- **Issues**: Open an issue on [GitHub](https://github.com/jayis1/ghostblade/issues)
- **Discussions**: Use [GitHub Discussions](https://github.com/jayis1/ghostblade/discussions)
- **Documentation**: See the [`docs/`](./) directory for detailed specifications

## License

The GhostBlade project is licensed under GPL-2.0-or-later. All contributions are subject to the same license. Include `SPDX-License-Identifier: GPL-2.0-or-later` in all new source files.