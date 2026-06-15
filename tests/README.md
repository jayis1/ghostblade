# GhostBlade Test Suite

This directory contains the test suite for the GhostBlade (Project NullSpectre) SPI bridge protocol and kernel driver.

## Test Files

### `test_spi_protocol.c` — Userspace SPI Protocol Unit Tests

Comprehensive unit tests for the SPI bridge protocol framing, matching the CRC-64 (ECMA-182, reflected) and CRC-32 (ISO 3309) algorithms used in both the RP2350B firmware and the RK3576 kernel driver.

**Test coverage:**
- CRC-64 known vectors (reflected ECMA-182)
- CRC-32 known vectors (Ethernet check value)
- Frame construction (NOP, payload, max payload, all command types)
- Frame validation (sync byte, header CRC-64, payload CRC-32)
- Error detection (bad sync, corrupted CRC, truncated frames, length mismatch, overflow)
- Single-bit-flip detection for both CRC-64 and CRC-32
- Fuzz testing with 200 random bit corruptions (all detected)
- Round-trip integrity for all 9 command opcodes
- Boundary conditions (payload lengths 0, 1, 255, 256, 4092)
- Payload size overflow rejection
- Command opcode distinctness verification

**Build:**
```bash
make test_spi_protocol
```

**Run:**
```bash
./test_spi_protocol
```

**Result:** 158/158 assertions pass.

### `test_apex_bridge.c` — Kernel Module Test Harness

In-kernel test harness for the `apex_bridge` SPI bridge driver. Runs as a loadable kernel module on the RK3576 target platform.

**Test coverage:**
- CRC-64 and CRC-32 vector validation (kernel-side implementation)
- SPI frame construction and validation (matches driver's `build_frame`/`validate_frame`)
- Error detection (bad sync, corrupted CRC, truncated frames, overflow)
- Bit-flip detection for both header and payload
- Round-trip integrity for all command opcodes
- Frame size calculations

**Build (on target):**
```bash
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

**Load:**
```bash
insmod test_apex_bridge.ko
dmesg | tail -50
```

**Run via sysfs:**
```bash
echo 1 > /sys/kernel/test_apex_bridge/run
cat /sys/kernel/test_apex_bridge/result
```

### `hil_spi_bridge_test.sh` — Hardware-in-the-Loop (HIL) Test

Shell script that runs on the RK3576 host and verifies the full SPI bridge communication path to the RP2350B coprocessor. Requires:
- The `apex_bridge` kernel driver loaded (`/dev/apex_bridge0`)
- The RP2350B firmware running and responsive
- The `libapex` shared library installed

**Test coverage:**
1. Device open/close
2. Sysfs telemetry readback (RSSI, temperature, battery voltage, uptime)
3. SPI NOP communication (raw frame build/validate)
4. Antenna selection (all 4 antennas)
5. MCU reset cycle
6. SDR tune (433 MHz ISM)
7. Sustained SPI stress test (100 iterations)
8. Watchdog recovery verification
9. CC1101 register access
10. Scatter-gather DMA statistics

**Usage:**
```bash
./hil_spi_bridge_test.sh           # Run all tests
./hil_spi_bridge_test.sh --quick   # Quick smoke tests (skip stress test)
./hil_spi_bridge_test.sh --loop    # Run continuously until failure
```

## CRC Implementation Notes

The SPI bridge protocol uses two CRC algorithms:

| Algorithm | Usage | Polynomial | Init | Final XOR |
|-----------|-------|-----------|------|-----------|
| CRC-64 | Header integrity | `0x42F0E1EBA9EA3693` (ECMA-182, reflected) | `0xFFFFFFFFFFFFFFFF` | `0xFFFFFFFFFFFFFFFF` |
| CRC-32 | Payload integrity | `0xEDB88320` (ISO 3309, reflected) | `0xFFFFFFFF` | `0xFFFFFFFF` |

**Important:** The CRC-64 implementation uses a reflected (LSB-first) computation, which produces different check values than the standard ECMA-182 MSB-first variant. The check value for "123456789" with our reflected implementation is `0xB86883E6FA710A9F`, not the standard `0x6C40DF5F0B497347`. Both the RP2350B firmware and the RK3576 kernel driver use this reflected variant consistently.

## SPI Frame Format

```
Offset  Size  Field
0x00    1     SYNC (0xAA)
0x01    1     CMD (command opcode)
0x02    1     LEN_LO (payload length, low byte)
0x03    1     LEN_HI (payload length, high byte)
0x04    4     RESERVED (0x00000000)
0x08    8     HDR_CRC (CRC-64 over bytes 0-7, little-endian)
0x10    N     PAYLOAD (0 to 4092 bytes)
0x10+N  4     CRC32 (CRC-32 over payload, little-endian)

Minimum frame: 20 bytes (16 header + 4 CRC-32)
Maximum frame: 4112 bytes (16 header + 4092 payload + 4 CRC-32)
```

## Command Opcodes

| Opcode | Direction | Name |
|--------|-----------|------|
| `0xFF` | Host→MCU | NOP |
| `0x01` | Host→MCU | SDR_TUNE |
| `0x02` | Host→MCU | SDR_STREAM |
| `0x03` | Host→MCU | ANT_SELECT |
| `0x04` | Host→MCU | CC1101_CFG |
| `0x05` | Host→MCU | NFC_TRANSACT |
| `0x06` | Host→MCU | TELEMETRY_REQ |
| `0x81` | MCU→Host | TELEMETRY |
| `0x82` | MCU→Host | SDR_IQ_CHUNK |

MCU→Host commands have bit 7 set (`0x80`); Host→MCU commands have bit 7 clear (except NOP=`0xFF`).