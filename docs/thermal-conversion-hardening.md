# ADC temperature conversion hardening

Author: jayis1

The battery monitor and calibration self-test previously subtracted a temperature
delta in whole degrees from a reference expressed in tenths of a degree. Hot
readings were understated, preventing the intended 85 C warning and 105 C
shutdown thresholds from being reached for many samples.

Both paths now use `adc_temperature_dc10()` with the existing nominal sensor
model: 3.3 V ADC reference, 0.706 V at 27 C, and a 1.721 mV/C slope. The helper
widens the ADC multiplication before computing the signed temperature delta in
tenths of a degree. This corrects software units; it does not establish sensor
calibration, board wiring, ADC channel selection, or MMIO address correctness.

## Local verification

- The new production battery-monitor regression failed against the old code.
- `make -C tests run` passes, including all 4096 possible 12-bit samples compared
  with an independent floating-point reference, cached readings, thermal shutdown,
  and calibration self-test rejection of an implausible temperature.
- Production battery-monitor and calibration tests pass AddressSanitizer and
  UndefinedBehaviorSanitizer with nonrecovering diagnostics.
- `make -C software/libapex all` and `make -C software/dts all` pass.
- Cppcheck 2.17.1 with exhaustive branch checking and warning, performance, and
  portability diagnostics reports no findings for the main firmware source tree.
  This is not a target compilation and does not cover the separate GhostWisp tree.
- Linux v6.12 checkpatch on the unchanged kernel driver reports 810 errors and
  1618 warnings. These pre-existing style findings remain unresolved; this change
  does not reformat the driver without a target build environment.

The production tests map register backing at the existing ADC address using
Linux `MAP_FIXED_NOREPLACE`; they require a compatible Linux host and an available,
page-aligned mapping. They do not emulate ADC timing, write-one-to-clear semantics,
or electrical behavior, and do not access physical hardware.

## Remaining validation and scope

`make check` reports missing AArch64 and Arm bare-metal compilers, CMake, Pico SDK,
and a kernel build tree. Firmware and kernel target builds and physical thermal
validation could not run. No hardware-dependent DMA, power-management, watchdog,
or protocol features were added speculatively. This is a bounded thermal-safety
fix, not an exhaustive review or remediation of every source file or analyzer
finding in the project.
