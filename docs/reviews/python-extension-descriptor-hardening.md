# Python extension descriptor hardening

Author: jayis1

## Scope and verified defects

This pass starts at main commit `0c8a1ce5b3132c93a18f6fc0fe01e54af79d729a`.
The existing checkout contained an unrelated uncommitted test edit, so work
was performed in a separate clean checkout on main without task branches.

The source inventory includes RP2350B firmware, the Linux bridge driver,
libapex and its Python extension, Python tools, devicetree overlays, and
GhostWisp protocol/peripheral code. This is a bounded hardening pass, not
an exhaustive security audit of every source path.

The production `pyapex.ApexBridge` extension had these ownership defects:

- Generic allocation initialized `fd` to zero. Constructor argument parsing
  failure then destroyed the object and closed stdin, although no device
  descriptor had been acquired.
- Repeated explicit `__init__` calls overwrote the existing descriptor and
  path, leaking both. A failed reopen also lost the existing device handle.
- Examples documented `close()`, but the extension provided no such method.

The allocator now establishes `fd = -1`. Initialization acquires replacement
resources before releasing the previous device. Destructor and explicit,
idempotent `close()` share one cleanup routine; the stored path is wiped
before freeing. A closed object's `fileno()` returns -1; initialization can
open it again. An actually acquired descriptor zero remains valid and is
closed normally. This does not change the device ioctl or wire ABI.

## Regression evidence

`tests/test_pyapex_lifecycle.py` builds the actual extension using its
production setup script into a temporary directory. It requires a C compiler,
CPython development headers, and setuptools; on Debian install `python3-dev`
and `python3-setuptools`. Tests run in isolated subprocesses with pipe-backed
stdin and `/dev/null`, never requiring the bridge device. Linux `/proc/self/fd`
is used to detect descriptor leaks.

All seven tests failed against the unchanged baseline, then passed after the
fix. They cover constructor failure, directly allocated uninitialized objects,
100 repeated initializations, failed reopen, idempotent close with descriptor
reuse, ownership of fd zero, and reopening a closed object. The test is included
in `make -C tests run`. No hardware behavior is inferred from these tests.

## Validation performed

- GCC 14.2.0: baseline and final `make -C tests run` passed. This includes
  existing C tests, walkie-talkie Python tests, devicetree build regressions,
  and the new extension lifecycle tests in the final run.
- `make -C software/libapex CFLAGS='-Wall -Wextra -Wpedantic -Werror -std=c11 -O2'`
  built static and shared libraries successfully.
- `python3 -m unittest discover -s devices/ghostwisp/tests -v` passed 72 tests.
- Cppcheck 2.17.1: the initial firmware scan passed but reported branch-analysis
  limits. Repeating with exhaustive checking passed without reported findings:

      cppcheck --enable=warning,performance,portability \
        --check-level=exhaustive --std=c11 --inline-suppr --error-exitcode=1 \
        -Ifirmware/rp2350b/include firmware/rp2350b/src

  All 14 firmware C files were checked. This is static analysis without the
  target SDK/build configuration, not proof of target correctness.
- Linux v6.12 `scripts/checkpatch.pl`, with its spelling and const-struct
  support files, was run on the existing kernel source:

      perl checkpatch.pl --no-tree --file software/linux-drivers/src/apex_bridge.c

  It exited 1 with 810 errors and 1618 warnings across 2695 lines. Findings
  include space indentation and comment/style issues. The driver was not
  changed; this pass does not claim to resolve all existing findings.
- `git diff --check` passed. An independent read-only review found no blocking
  security or logic issues in the lifecycle changes.

## Remaining work and limitations

A target firmware toolchain, Pico SDK, matching kernel build tree, and physical
hardware were not available for full firmware/module builds or device tests.
Full-source manual audit and elimination of all kernel checkpatch findings
remain outstanding. New protocol handlers, runtime power management, DMA,
watchdog/brownout behavior, and sleep/wake changes were not introduced without
hardware-specific validation. Existing tests may simulate hardware or duplicate
logic; their results must not be presented as physical-device verification.

No automation workflow files were added or modified. No other repository was
changed, and the pre-existing dirty checkout was left untouched.
