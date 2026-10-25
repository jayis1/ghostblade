# Kernel SPI buffer hardening

Author: jayis1

## Confirmed defects and changes

The receive work handler builds a 20-byte NOP in a maximum-sized `kmalloc`
buffer, then transmits the entire allocation to clock out a response. Previously,
bytes after the NOP trailer were uninitialized. Those bytes could disclose stale
kernel heap contents to the coprocessor and could be interpreted as further SPI
commands. `apex_build_frame()` now initializes the entire advertised destination
capacity after checking arguments and capacity, before constructing the frame.
Frame lengths, opcodes, payloads, and checksum computation remain unchanged.
All current callers supply separate payload and destination buffers.

`apex_spi_xfer()` previously limited only the maximum protocol length. The SPI
controller could write `tx_len` bytes into a smaller receive buffer before the
function clamped its return value. It now rejects insufficient receive capacity
before submitting the transfer and rejects missing transmit or receive buffers.
The frame builder also rejects a missing destination and nonempty payloads with
no source buffer. Rejected frame builds leave the destination untouched.

## Regression evidence

Run locally:

```sh
python3 tests/test_kernel_spi_buffers.py -v
make -C tests run
python3 -m unittest discover -s devices/ghostwisp/tests -v
```

The new test compiles the actual `apex_build_frame()` and `apex_spi_xfer()`
function bodies extracted from the driver, not copies of their implementations.
Kernel APIs and CRC calculations are narrowly stubbed: these tests establish
buffer contracts, not CRC correctness, kernel ABI compatibility, or hardware
behavior. Compilation uses GCC with `-Wall -Wextra -Werror` and undefined-behavior
and bounds sanitizers. Assertions remain enabled.

A poisoned destination exposed the padding defect before the fix. After the
fix, the test passes for empty and short payloads, maximum payload with guard
bytes around the destination, invalid pointers, oversized payloads, insufficient
frame and receive capacities, and transfer-error propagation. Invalid transfer
requests must not invoke the SPI submission stub. The test is included in the
normal host test target.

The complete existing host test target passed before the changes and passes
with the new regression included. The separate GhostWisp Python discovery run
passed 104 tests.

## Static analysis and limitations

Cppcheck 2.17.1 completed all 22 firmware C translation units with no reported
diagnostics using:

```sh
cppcheck --enable=warning,performance,portability --check-level=exhaustive \
  --platform=unix32 --inline-suppr --error-exitcode=1 \
  -Ifirmware/rp2350b/include \
  -Idevices/ghostwisp/firmware/rp2350b/include \
  firmware/rp2350b/src devices/ghostwisp/firmware/rp2350b/src
```

The 32-bit Unix model is an approximation, not a complete embedded ABI model.
No Pico SDK compilation database is available; external SDK includes and target
configuration are not fully modeled. An initial normal-level analysis reported
branch-analysis limits; the exhaustive run eliminated those diagnostics without
suppressing them.

Linux v6.12 `checkpatch.pl --no-tree` reports zero errors and zero warnings for
the kernel patch. The full-file baseline has 810 errors and 1618 warnings;
these pre-existing diagnostics remain outside this bounded fix. The downloaded
standalone script lacks its optional spelling and const-struct dictionaries,
so those supplemental checks were not performed.

`make check` fails because the AArch64 compiler, Arm bare-metal compiler,
CMake, Pico SDK, and configured kernel build tree are unavailable. No full
firmware or kernel-module build is claimed. `make dtb` exits successfully for
available overlay outputs but skips the base board DTB without `rk3576.dtsi`.
No physical hardware, RF transmission, flashing, suspend/removal concurrency,
or power-failure tests were performed.

This is a bounded SPI-buffer fix, not a completed whole-codebase security audit.
Watchdog, brownout, sleep/wake, runtime power management, scatter-gather DMA,
interrupt and device lifetime, polling, Python interfaces, and devicetree wiring
need separate target-backed reviews. In particular, the existing asynchronous
SG transfer lifetime/timeout handling and device removal with open file handles
remain review priorities. No speculative hardware behavior was added.
