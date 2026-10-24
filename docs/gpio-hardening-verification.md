# GPIO register hardening verification

Author: jayis1

## Scope and fixes

This pass fixes the GhostWisp RP2350B GPIO implementation in this repository.
It is not a completed repository-wide security audit or a hardware qualification.

- Correct IO_BANK0 and PADS_BANK0 base addresses.
- Correct RP2350 interleaved low/high SIO register offsets for input, output
  set/clear/toggle, and output enable set/clear/read.
- Select the high bank for GPIO32–47 and use a bounded unsigned bit shift.
  Previously these pins could invoke undefined behavior or affect another pin.
- Correct output-disable and pull-up/pull-down pad bits while preserving other
  pad configuration, including isolation and input-enable state.
- Configure IRQ enables through processor-0 INTE instead of raw INTR status.
  IRQ words cover eight pins, each with four event bits; shift status, enable,
  and write-one-to-clear acknowledgement masks to the appropriate pin slot.
- Document caller serialization for shared IRQ enable read-modify-write.

Register definitions were checked against Raspberry Pi pico-sdk 2.2.0:
https://github.com/raspberrypi/pico-sdk/tree/2.2.0/src/rp2350/hardware_regs/include/hardware/regs
Specifically: addressmap.h, sio.h, pads_bank0.h, and io_bank0.h.

## Verification

- Baseline and final `make -C tests run`: passed.
- `python3 -m unittest discover -s devices/ghostwisp/tests -v`: passed.
- New `tests/test_ghostwisp_gpio_mmio.c` links the production implementation
  without DRIVER_HOST_TEST. Only register access and the architecture barrier
  are replaced. Tests cover all 48 pins, both SIO banks, each pin's pad and mux
  addressing, IRQ enable/disable/dispatch/acknowledgement, neighboring-pin
  enable preservation, and rejection of pins 48–255 without register access.
- The same register-level test passed with GCC `-Wall -Wextra -Werror -std=c11`
  and `-fsanitize=undefined,address -fno-sanitize-recover=all`.
- Cppcheck 2.17.1 on ghostwisp_gpio.c, with warning/style/performance/portability
  enabled and exhaustive checks: passed, including hardware and test branches.
- `git diff --check`: passed before publication.

Reproduce the register-level test with:

    make -C tests test_ghostwisp_gpio_mmio
    ./tests/test_ghostwisp_gpio_mmio

The backing arrays capture writes. They do not simulate SIO alias side effects,
write-one-to-clear behavior, timing, electrical pin levels, or memory barriers.
Assertions are kept enabled by the test build target.

## Outstanding work and environment limits

Cppcheck was also run over all 21 firmware C sources under firmware/rp2350b/src
and devices/ghostwisp/firmware/rp2350b/src with both include directories,
`--enable=warning,style,performance,portability --check-level=exhaustive
--inline-suppr --error-exitcode=1 --suppress=missingIncludeSystem`.
The final run reports no warning/error diagnostics, but exits 1 for 15 existing
style diagnostics in GhostWisp boot, I2C, and SPI code. Hardware SDK includes
are unavailable, so this is source-level analysis, not an SDK-configured build.

Linux v6.12 checkpatch was run using `--no-tree --file` on the unchanged driver
source and its register header. Existing findings remain:

- apex_bridge.c: 810 errors, 1618 warnings.
- apex_bridge_regs.h: 1 error, 49 warnings.

These findings are not fixed or suppressed by this patch. The kernel module
build was attempted but cannot proceed because the running kernel's build
headers are missing. The ARM cross-compiler and Pico SDK are also unavailable.
No target firmware build, driver load, or physical hardware test is claimed.

The boot path still owns pad isolation/input-enable configuration and NVIC
routing. gpio_init resets software IRQ bookkeeping, not hardware enables;
callers must retain the documented boot/configuration discipline. Concurrent
IRQ configuration from multiple contexts is not made safe by this patch.

New DMA, runtime power-management, brownout, watchdog, sleep/wake, and protocol
features were not added speculatively. Their hardware-dependent behavior and
the remaining repository-wide audit require separate verification. No radio
transmission or live-network testing was performed.
