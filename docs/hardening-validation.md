# Python SDR input hardening validation

Author: jayis1

## Change and failure mechanism

The production Python extension's `ApexBridge.sdr_tune()` previously accepted
NaN frequency and unchecked bandwidth/gain values. Converting NaN or an
unrepresentable gain to an integer has undefined behavior. Narrowing an
out-of-range bandwidth can wrap it to a different command value.

Validate all three inputs before conversion and before `ioctl()`, using the
existing firmware limits in `handle_cmd_sdr_tune()`:

- Frequency: 100000 through 3800000000 Hz, finite.
- Bandwidth: 1 through 61440 kHz.
- Gain: 0 through 73 dB, finite.

Invalid inputs raise `ValueError`; valid inputs retain the existing ioctl ABI.
No firmware, kernel driver, or RF protocol behavior is changed.

## Reproduction and checks

The regression tests compile the production extension through `setup.py` into
an isolated directory. They use `/dev/null`, never a radio device. Invalid
arguments must raise `ValueError`, not reach the device's rejected ioctl.
Valid endpoints and representative interior values must reach ioctl and receive
the expected `/dev/null` error. Tests include NaN, infinities, large values,
negative values, wraparound bandwidths, and the next representable double above
the maximum frequency and gain.

Results on the local host:

- `make -C tests run`: passed before changes and after changes.
- New regressions before the fix: two failing test methods, demonstrating the
  missing validation in the production extension.
- `python3 tests/test_pyapex_lifecycle.py -v`: all ten test methods passed after
  the fix (including seven existing lifecycle methods).
- `CFLAGS='-fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all'
  LDFLAGS='-fsanitize=undefined,float-cast-overflow'
  python3 tests/test_pyapex_lifecycle.py -v`: all ten passed.
- `cppcheck --check-level=exhaustive --enable=warning,performance,portability
  --std=c11 --inline-suppr --error-exitcode=1 -Ifirmware/rp2350b/include
  firmware/rp2350b/src`: passed with Cppcheck 2.17.1 across fourteen firmware
  translation units. This is source-level analysis, not target build validation.
- `make -C software/dts all validate`: passed for available overlays. The base
  board DTB is skipped because vendor `rk3576.dtsi` is unavailable.
- `git diff --check`: passed.
- Independent read-only review: no security concerns or logic errors found in
  the implementation and test diff.

## Incomplete validation and deferred work

Linux v6.12 `scripts/checkpatch.pl` was run with `--no-tree --file` on the
unchanged kernel driver and its register header. The driver reported 810 errors
and 1618 warnings; the header reported 1 error and 49 warnings. These are
pre-existing diagnostics, not resolved by this bounded userspace fix. A clean
kernel style result is not claimed.

The host lacks `arm-none-eabi-gcc`, `aarch64-linux-gnu-gcc`, CMake, the Pico SDK,
and a matching kernel build tree. Firmware and kernel target builds were not
performed. No board, DMA, interrupt timing, runtime power-management,
suspend/resume, brownout, or watchdog hardware validation was performed.

This change is not an exhaustive audit of every C, Python, or devicetree source.
Hardware-dependent feature additions and whole-driver warning cleanup remain
separate work requiring the appropriate build environment and hardware checks.
