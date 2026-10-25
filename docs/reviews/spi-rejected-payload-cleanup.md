# Rejected SPI payload cleanup

Author: jayis1

## Defect and fix

The RP2350B parser wiped accepted command payloads in `dispatch_frame()`,
but a complete frame with a bad payload CRC returned to idle without
clearing the assembled payload. Rejected NFC/configuration bytes therefore
remained in the parser's SRAM buffer until overwritten or reinitialized.
This is a data-retention defect, not evidence of an externally exploitable
memory disclosure.

`spi_protocol_process()` now uses the existing volatile `secure_wipe()` on
the rejected payload before resetting to idle. The length was already checked
against `SPI_MAX_PAYLOAD` during header processing. CRC accounting and wire
behavior are unchanged. Incomplete frames remain buffered for continuation;
this change does not introduce a timeout or alter multicore synchronization.

## Verification

- The new `tests/test_spi_rejected_payload.py` compiled successfully against
  the original parser, then failed its zeroed-payload assertion before the fix.
  It passes after the fix with GCC warnings treated as errors and undefined
  behavior/bounds sanitizers enabled.
- The test extracts production parser, payload validator, CRC32, and wipe
  function bodies. Header validation, barriers, and successful dispatch are
  explicitly stubbed. Cases cover empty, one-byte, 256-byte, and maximum-size
  rejected payloads, split CRC trailers, recovery on a subsequent valid frame,
  and back-to-back invalid/valid frames. It establishes software cleanup and
  parser recovery, not hardware behavior or complete framing correctness.
- `make -C tests run` passed before and after the change. The new test is
  included in that target.
- Cppcheck 2.17.1 passed on all 15 main firmware C sources using
  `--enable=warning,style,performance,portability --check-level=exhaustive
  --platform=unix32 --error-exitcode=1 --inline-suppr
  -Ifirmware/rp2350b/include firmware/rp2350b/src`.
  This width approximation without the SDK is not a target build.
- `git diff --check` passed. Independent read-only review found no security
  or logic objections to this change.

## Remaining coverage and blockers

This is a focused security improvement, not an exhaustive source audit or
completion of every maintenance target. No protocol, DMA, power-management,
watchdog, brownout, or sleep/wake feature was invented without specifications
and hardware validation. No RF or invasive hardware tests were performed.

The requested full-file kernel checkpatch scan still reports pre-existing
style defects in `apex_bridge.c`: 772 errors and 1528 warnings. The driver was
not reformatted in this patch. A separate exhaustive Cppcheck scan of the
seven GhostWisp firmware sources also reports pre-existing redundant-condition
and const-qualification style findings. Those are not suppressed or presented
as passing checks.

`make check` fails because the AArch64 compiler, Arm bare-metal compiler,
CMake, Pico SDK, and matching kernel build tree are unavailable. DTC 1.7.2
is present. Firmware and kernel target builds and physical board tests remain
unverified. Host regressions do not replace those checks.
