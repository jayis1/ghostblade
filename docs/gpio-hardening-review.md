# RP2350B GPIO data-path hardening

Author: jayis1

## Scope and correction

The public `rp2350b_gpio_set()` and `rp2350b_gpio_get()` functions previously
used IO_BANK0 mux/interrupt-space addresses for GPIO data and shifted a 32-bit
word by the full pin number. Cppcheck's 32-bit analysis reported undefined
shifts for NFC chip select on GPIO43. The same problem affects GPIO32..47.

The public data-path functions now live in `src/rp2350b_gpio.c`, included in
the firmware CMake source list. They use SIO's interleaved low/high banks,
shift only by 0..31, and reject pins outside 0..47 before accessing registers.
Output writes use SIO's atomic SET/CLR registers, not read-modify-write.
The public signatures are unchanged. Invalid writes are ignored and invalid
reads return false; initialization, muxing and pad configuration are unchanged.

Register definitions were checked against Raspberry Pi pico-sdk tag 2.2.0:

- [addressmap.h](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/addressmap.h): SIO base 0xd0000000.
- [sio.h](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/sio.h): input offsets 0x04/0x08, SET 0x18/0x1c, CLR 0x20/0x24.

## Executed verification

- `make -C tests run`: passed before and after the change.
- `make -C devices/ghostwisp test`: passed before and after the change.
- `make dtb`: passed after the change.
- The new `test_rp2350b_gpio_mmio` links the production implementation and
  checks both output values, input bit and bank isolation for every pin 0..47,
  and absence of register accesses for every invalid uint8_t pin 48..255.
  It runs with the normal test suite and is removed by its clean target.
- The new regression also passed a GCC 14.2.0 host build with `-Werror`,
  `-Wconversion`, AddressSanitizer and UndefinedBehaviorSanitizer enabled.
- `git diff --check`: passed.
- Independent read-only review of the exact implementation and test found
  no blocking correctness or security issues.

Firmware analysis command (Cppcheck 2.17.1):

    cppcheck --enable=warning,style,performance,portability --platform=unix32 --std=c11 --inline-suppr --check-level=exhaustive --error-exitcode=1 -Ifirmware/rp2350b/include firmware/rp2350b/src

This completed with exit status zero and no diagnostics. The baseline command
without exhaustive branch exploration reported two `shiftTooManyBits` errors
in the removed GPIO implementation. `unix32` approximates the firmware integer
widths; it does not model the complete target ABI or substitute for a build
against SDK headers.

Kernel analysis command:

    perl checkpatch-ghostblade.pl --no-tree --file software/linux-drivers/src/apex_bridge.c software/linux-drivers/include/apex_bridge_regs.h

Checkpatch returned nonzero. Existing findings remain: 810 errors and 1618
warnings in the driver, and 1 error and 49 warnings in its header. No kernel
source was changed. Full kernel warning cleanup was not completed; broad
formatting churn was intentionally kept out of the GPIO correction.

## Remaining limits and work

This is a bounded repair, not a comprehensive review of every source file or
completion of every requested feature. No new protocol handlers, watchdog,
brownout, sleep/wake, runtime power-management, DMA or telemetry behavior was
added. Existing GPIO mux/pad setup and other direct register accesses outside
the public set/get API need a separate vendor-register audit; this correction
does not establish that board initialization works.

`make check` failed because this host lacks the AArch64 and Arm bare-metal
cross-compilers, CMake, Pico SDK and a kernel build tree. Firmware and kernel
target builds therefore remain unverified. Hardware execution was unavailable.
The regression replaces only MMIO accesses and ARM barriers; memory backing
records SET/CLR writes but does not emulate atomic aliases, bus ordering,
mux/pad setup, voltage levels or electrical timing. On-device validation is
required before treating this as a board bring-up result.

Execution logs and fetched reference headers for this run are stored locally
under `/root/.hermes/cache/scratch/ghostblade-owner/`; these scratch artifacts
are not committed and may expire.
