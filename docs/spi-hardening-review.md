# SPI hardening review

Author: jayis1

## Scope and changes

This is a focused defensive hardening pass, not a complete source audit or
hardware qualification. The production changes preserve existing wire formats.
The host-to-MCU trust boundary is covered by driver transfer-length validation,
frame validation, and firmware command-handler error recovery.

- Reject a successful SPI completion whose actual length differs from the
  requested transfer length. Previously the driver reported the requested length
  and could validate an unreceived allocation tail as frame data. Short transfers
  now enter the existing error-counter and error-flag path with `-EIO`.
- Reject nonzero frame reserved fields in the kernel, matching firmware header
  validation. Header length is checked before inspecting the field.
- Use size_t arithmetic for frame-size comparison, removing a signedness warning
  exposed by compiling the production validator with `-Werror`.
- Check the SDR DMA start result and disable RX/LNA and clear streaming state
  if it fails, allowing retry. The current DMA implementation returns success;
  this is defensive handling of its existing int return contract, verified by
  fault injection, not a claim of a presently reachable hardware failure path.

## Verification performed

- `make -C tests run`: passed before and after the changes.
- Kernel SPI host regression: compiles the extracted production transfer,
  builder, and validator functions with `-Wall -Wextra -Werror`, undefined-
  behavior and bounds sanitizers. Covers truncated headers/trailers, all four
  reserved bytes, short/zero-byte completion, recovery, buffer bounds and padding.
  The new reserved-field assertion failed before the production fix.
- Firmware stream host regression: compiles the extracted production command
  handler with the same warning and sanitizer settings. Covers empty payload,
  brownout refusal, injected startup failure, retry, repeated start and stop.
  The failure-state assertion failed before the production fix.
- Linux v6.12 checkpatch on the changed kernel patch, with `--no-tree
  --no-signoff`: zero errors and zero warnings.
- cppcheck 2.17.1 on `firmware/rp2350b/src`, with
  `--enable=warning,style,performance,portability --check-level=exhaustive
  --platform=unix32 --error-exitcode=1 --inline-suppr
  -Ifirmware/rp2350b/include`: exit 0, no diagnostics.
- Independent read-only diff review: no blocking logic or security findings.

## Unfinished work and verification limits

The host harness stubs kernel APIs and CRC calculations and the firmware harness
stubs DMA and GPIO operations. These establish local production-function behavior,
not a complete kernel module build, DMA resource cleanup, multicore synchronization,
real SPI timing, or electrical behavior. The Unix 32-bit cppcheck platform is a
width approximation, not a Pico SDK target build or complete ARM ABI model.

A native kernel build was attempted with `make -C software/linux-drivers` and
failed because `/lib/modules/7.0.14-25-pve/build` does not exist. An ARM bare-metal
compiler was not available on PATH. No firmware cross-build or hardware tests
were performed. No RF operations, flashing, or power-cycle tests were executed.

A broader initial cppcheck scan including
`devices/ghostwisp/firmware/rp2350b/src` returned nonzero, with existing style
findings: constant boot-phase return/error checks, const-qualification opportunities
in SPI/I2C helpers, and redundant length predicates in the SPI simulator. They
were not changed merely to make this focused pass appear repository-wide clean.
That scan used normal check depth and also reported branch-analysis limits.
The stronger clean result above applies only to core GhostBlade firmware.

Python tools and devicetree were inventoried and their existing host regressions
ran in the suite; they were not exhaustively audited. New protocol handlers,
watchdog policy, brownout thresholds, sleep/wake redesign, runtime power management,
DMA scatter-gather redesign, and broader interrupt/poll lifecycle changes require
separate design and target validation and were not implemented in this pass.
