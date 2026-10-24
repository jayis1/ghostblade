# ADC calibration hardening

Author: jayis1

## Corrected defects

- Battery correction multiplied signed 32-bit values before saturation. Valid
  input/coefficient field values could overflow and invert a safety-related
  battery reading. The multiply now uses signed 64-bit arithmetic.
- Raw 12-bit samples multiplied by the divider numerator exceeded 32 bits.
  Factory calibration also converted the overflowing numerator to signed before
  division. Factory calibration and self-test now share a 64-bit conversion.
- Factory calibration accepted unordered reference/measurement points and could
  divide by a computed gain of zero. It now rejects these conditions, gains
  outside the stored unsigned 16-bit range, and offsets outside the stored signed
  16-bit range before updating coefficients or programming flash. Invalid known
  references are rejected before any ADC access.

The wire protocol, coefficient record layout, RF settings, and board configuration
are unchanged.

## Verification

- A UBSan probe against the pre-change production source reproduced signed
  overflow in `adc_cal_apply_vbat`: `65535 * 98302`; exit status 1.
- `make -C tests clean all run`: exit status 0.
- Production-source regression test compiled with `-Wall -Wextra -Werror
  -std=c11 -g -O1 -fsanitize=undefined,address -fno-sanitize-recover=all`:
  exit status 0, no sanitizer diagnostics.
- The new test includes the actual firmware implementation, rather than copied
  arithmetic. It covers every unsigned 16-bit input for selected extreme gains
  and offsets, every 12-bit raw ADC value, valid coefficient calculations,
  rejection boundaries, unchanged outputs on calculation failure, and invalid
  reference rejection without hardware access.
- `python3 -m unittest discover -s devices/ghostwisp/tests -p 'test_*.py'`:
  72 tests passed.
- Cppcheck 2.17.1 over all `firmware/rp2350b/src` with
  `--check-level=exhaustive --enable=warning,style,performance,portability
  --error-exitcode=1 --inline-suppr --suppress=missingIncludeSystem
  -Ifirmware/rp2350b/include`: exit status 0 before and after these changes.
- `git diff --check`: passed.

## Remaining limitations

This is a bounded ADC fix, not a completed audit of every repository source.
No RP2350 cross-toolchain, Pico SDK target build, target kernel build, electrical
validation, flash persistence test, or hardware execution was performed.
Host tests do not establish multicore flash safety or analog accuracy.

Linux v6.12 checkpatch run with `--no-tree --file
software/linux-drivers/src/apex_bridge.c` returned 810 errors and 1618 warnings
in the unchanged driver. Its optional spelling/constant-structure word lists
were unavailable. These pre-existing findings remain unresolved; the request
for a warning-free kernel tree is not complete. Large driver reformatting and
unverified PM/DMA/power-state changes were deliberately not mixed into this fix.

The original `/root/ghostblade` checkout contained an existing uncommitted test
edit that blocked its pull. That checkout was preserved; this change was prepared
on a separate clean checkout of the remote default branch, `main`.
