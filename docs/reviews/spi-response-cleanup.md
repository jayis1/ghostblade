# SPI response stack cleanup

Author: jayis1

## Confirmed defect and fix

`build_response_frame()` in `firmware/rp2350b/src/spi_protocol.c` copies
response payloads (including NFC and IQ data) into a stack frame before
checking TX ring capacity. Its insufficient-space return incremented the
overrun counter but bypassed the existing secure wipe. The failure path now
uses the same volatile-write `secure_wipe()` as the successful path. Return
values, ring contents and indices, and interrupt behavior are unchanged.
Invalid-input returns occur before any payload copy.

## Verification

- `python3 tests/test_spi_response_cleanup.py -v`: failed against the original
  builder because no wipe occurred on rejection; passed after the fix.
- The regression extracts the production builder, serialization helpers and
  wipe body, includes the production protocol header, and compiles with
  `-Wall -Wextra -Werror -fsanitize=undefined,bounds` and fatal sanitizer
  diagnostics. It checks full-ring rejection with payload lengths 0, 1,
  256 and the protocol maximum, unchanged rejected ring contents and indices,
  overrun accounting, no rejection interrupt, exact-fit wraparound success,
  full-frame zeroing, and invalid arguments. CRC and interrupt functions are
  stubs: this is a local buffer-lifetime test, not wire or hardware validation.
- `make -C tests run`: passed before and after the change; the new regression
  is registered in this target alongside the existing production-parser,
  driver-helper, Python-tool and devicetree regression tests.
- `make libapex`: passed.
- Cppcheck 2.17.1, command:
  `cppcheck --enable=warning,performance,portability --check-level=exhaustive --platform=unix32 --inline-suppr --error-exitcode=1 -Ifirmware/rp2350b/include firmware/rp2350b/src`
  passed before and after the fix with no diagnostics. The earlier default
  check level reported branch-analysis limits; exhaustive mode removed those
  limits. The Unix32 width approximation is not a Pico SDK target build.
- Linux v6.12 `scripts/checkpatch.pl --no-tree --file software/linux-drivers/src/apex_bridge.c`
  reported 772 errors and 1528 warnings in the unchanged kernel source.
  These remain unresolved; this focused firmware fix does not claim a clean
  kernel style scan or an exhaustive source audit.
- `git diff --check`: passed. Independent read-only review found no security
  or logic concerns in the change and regression.

## Remaining validation limits

`make check` failed because the AArch64 compiler, Arm bare-metal compiler,
CMake, Pico SDK and kernel build tree are absent. DTC is available (1.7.2).
Firmware cross-compilation, kernel module compilation, physical hardware,
multicore/interrupt timing, DMA ownership, and board electrical behavior were
not verified. No new hardware-dependent features were added. The change is
limited to response error-path cleanup; other source areas are not certified
by this review. No workflow files or infrastructure changes were made.
