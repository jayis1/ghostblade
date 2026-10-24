<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->
<!-- Author: jayis1 -->

# GhostWisp I2C address hardening

Author: jayis1

## Corrected behavior

The GhostWisp I2C API takes a 7-bit slave address. Previously the hardware
implementation masked its `uint8_t` address with `0x7f` before programming
`I2C_REG_TAR`. An invalid address such as `0xb6` therefore selected `0x36`,
potentially writing to a different device instead of reporting a caller error.
The host model returned a missing-device error rather than enforcing the API.

All public address-taking entry points now reject `0x80` through `0xff` with
`I2C_ERR_INVALID_ARG` before initialization checks or bus access. The register
write helper delegates to the guarded blocking write. Register-read helpers
validate before their host/hardware split. Valid 7-bit addresses retain their
existing behavior; this change does not implement a reserved-address policy,
10-bit addressing, bus recovery, or probe-transaction redesign.

## Regression coverage

`tests/test_ghostwisp_i2c_addresses.c` links the production I2C implementation.
`DRIVER_HOST_TEST` supplies the existing simulated bus, while the validation
runs in code shared with the hardware path. The test covers every invalid
`uint8_t` address before and after initialization, all public address-taking
operations, zero-length transfers, unchanged output buffers and error counts,
and preservation of registers on every valid 7-bit address. The simulator's
eight-device capacity is respected by testing valid devices in batches.
Assertions remain enabled for this target even with `-DNDEBUG` in build flags.

The regression failed against the original implementation and passed after
the guards were added. The local suite and targeted sanitizer run passed:

```sh
make -C tests run
gcc -Wall -Wextra -Werror -std=c11 -UNDEBUG -DDRIVER_HOST_TEST \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Idevices/ghostwisp/firmware/rp2350b/include \
  tests/test_ghostwisp_i2c_addresses.c \
  devices/ghostwisp/firmware/rp2350b/src/ghostwisp_i2c.c \
  -o "$TMPDIR/ghostwisp-i2c-addresses"
"$TMPDIR/ghostwisp-i2c-addresses"
```

Compiler: GCC 14.2.0. The suite passed both before and after the change.
The native libapex build and available DTS validation also passed. Base-board
DTS validation was skipped by the existing Makefile because vendor
`rk3576.dtsi` was unavailable; overlay validation is not binding validation.

## Analysis results and outstanding work

Cppcheck 2.17.1 analyzed all 21 firmware C translation units, including the
GhostWisp firmware, with:

```sh
cppcheck --enable=warning,performance,portability --check-level=exhaustive \
  --error-exitcode=1 --inline-suppr --std=c11 \
  -Ifirmware/rp2350b/include \
  -Idevices/ghostwisp/firmware/rp2350b/include \
  firmware/rp2350b/src devices/ghostwisp/firmware/rp2350b/src
```

The native-platform run exited zero, but repeating it with `--platform=unix32`
reported two `shiftTooManyBits` errors in `rp2350b_gpio_set()` in
`firmware/rp2350b/src/rp2350b_init.c`: NFC chip select uses GPIO43 with
`1UL << pin`. The nearby GPIO helpers also use IO_BANK0 addresses for data
access, requiring a separate SIO register-bank review and production-path
regression tests. These existing defects are not corrected by I2C validation.
The native analysis result must not be represented as a clean target-platform
analysis. Missing SDK headers and manually configured analysis also limit
coverage; this is not a compilation database or a complete firmware audit.

Linux v6.12 `scripts/checkpatch.pl --no-tree --file` was run on the unchanged
kernel driver and register header. It reported 810 errors / 1618 warnings in
`apex_bridge.c` and 1 error / 49 warnings in `apex_bridge_regs.h`, including
existing indentation and comment-style findings. No kernel source was changed,
and these findings remain unresolved rather than being suppressed.

No RP2350 cross-compiler, Pico SDK, kernel build headers, or target hardware
was available. Consequently there was no target firmware/module build,
electrical validation, RF operation, suspend/resume test, or DMA/IRQ lifecycle
verification. New power-management, DMA, watchdog, brownout, and sleep/wake
features were not added speculatively. Source inspection was focused, not a
complete manual audit of every C, Python, and devicetree file. The broader
maintenance request, including clearing all static-analysis findings, remains
incomplete despite the verified I2C fix.
