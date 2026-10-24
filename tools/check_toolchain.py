#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
# Author: jayis1
"""Read-only prerequisite probes; not a firmware or kernel build validation."""

import argparse
from pathlib import Path
import subprocess


def probe(label, command, expected_prefix=None):
    """Execute one bounded, shell-free probe and report its actual result."""
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, errors="replace", timeout=10, check=False
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL {label}: {exc}")
        return False
    output = result.stdout.strip()
    if result.returncode != 0 or not output:
        print(f"FAIL {label}: probe failed (exit {result.returncode})")
        return False
    if expected_prefix and not output.startswith(expected_prefix):
        print(f"FAIL {label}: target {output!r}; expected {expected_prefix}*")
        return False
    print(f"OK   {label}: {output.splitlines()[0]}")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aarch64-cc", default="aarch64-linux-gnu-gcc")
    parser.add_argument("--arm-cc", default="arm-none-eabi-gcc")
    parser.add_argument("--dtc", default="dtc")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--pico-sdk", type=Path, default=Path.home() / "pico-sdk")
    parser.add_argument("--kernel-dir", type=Path, required=True)
    args = parser.parse_args()

    print("GhostBlade cross-build prerequisite probes")
    # Do not short-circuit: show every missing prerequisite in one invocation.
    checks = [
        probe("AArch64 compiler", [args.aarch64_cc, "-dumpmachine"], "aarch64-"),
        probe("Arm bare-metal compiler", [args.arm_cc, "-dumpmachine"], "arm-none-eabi"),
        probe("DTC", [args.dtc, "--version"]),
        probe("CMake", [args.cmake, "--version"]),
    ]
    for label, marker in (
        ("Pico SDK", args.pico_sdk / "pico_sdk_init.cmake"),
        ("kernel build tree", args.kernel_dir / "Makefile"),
    ):
        found = marker.is_file()
        print(f"{'OK  ' if found else 'FAIL'} {label}: {marker}")
        checks.append(found)
    print("These probes do not verify SDK submodules, kernel configuration, or builds.")
    return 0 if all(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
