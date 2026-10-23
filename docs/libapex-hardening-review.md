# libapex input-boundary hardening

Author: jayis1

## Corrected defects

- `apex_strerror(INT_MIN)` negated its argument before checking its range.
  Signed negation overflow is undefined behavior. Validate the supported error
  domain before negating; unknown values still return `Unknown error`.
- `apex_sdr_tune()` accepted NaN because neither ordered rejection comparison
  was true. Converting NaN to the unsigned wire gain is undefined behavior.
  Require both inclusive range comparisons to succeed before conversion.
  Existing finite gain limits and encoding remain unchanged.
- `apex_get_firmware_version()` accepted a buffer too short for the complete
  string and its terminating null, then returned success after truncation.
  Require `sizeof("GhostBlade v1.0.0")` bytes and record insufficient capacity
  in the handle's last-error state.

## Regression coverage

`tests/test_libapex_api.c` links the production library implementation rather
than copying validation helpers. It opens `/dev/null` for a real handle and
wraps only ioctl, checking invalid gains never issue a device command and
valid gains retain their wire encoding. It checks extreme integer errors,
nonfinite and boundary gains, short and exact-sized version buffers, output
canaries, and last-error state.

Run with `make -C tests test_libapex_api` and `tests/test_libapex_api`, or as
part of `make -C tests run`. The target depends on the production source and
header so implementation changes rebuild the regression binary.

The baseline production-linked test reproduced the signed negation overflow
under UBSan. After the fixes, the full host suite, the library build with
`-Wall -Wextra -Wpedantic -Werror`, and the regression executable with
`-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all`
passed.

## Review and validation limits

This is a bounded userspace hardening change, not a completed security audit
of all firmware, kernel, Python, and devicetree sources.

Cppcheck 2.17.1 completed on all 14 top-level RP2350B firmware translation
units with `--enable=warning,style,performance,portability
--check-level=exhaustive --inline-suppr --error-exitcode=1
-Ifirmware/rp2350b/include` and reported no diagnostics. The changed production
library and its regression source also passed the same enabled checks with
the library include directory. This standalone analysis is not a target
compiler build and does not establish hardware correctness.

Linux v6.12 checkpatch, run with `--no-tree --file` on
`software/linux-drivers/src/apex_bridge.c`, reported 810 errors and 1,618
warnings in the unchanged baseline driver. Those diagnostics remain open;
this patch does not claim to satisfy the all-warning cleanup request.

The environment had neither an ARM cross compiler nor a build tree for the
running kernel. No firmware or kernel module target build, device execution,
or electrical/timing validation was performed. Driver, devicetree, watchdog,
brownout, sleep/wake, and DMA behavior were not changed. Hardware-dependent
feature additions require their own specifications and target verification.
