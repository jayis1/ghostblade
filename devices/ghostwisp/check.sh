#!/usr/bin/env bash
# check.sh — GhostWisp automated checks helper
#
# Author: jayis1
# SPDX-License-Identifier: MIT
#
# Usage:
#   check.sh isolation  <build_dir>
#   check.sh headers    <include_dir>
#   check.sh cross      <fw_dir> <pico_sdk_path> <cross_build_dir>
#
# Invoked by: make check (devices/ghostwisp/Makefile)
#
# Exit 0 on pass, non-zero on fail.

set -euo pipefail

MODE="${1:-}"
PASS=0
FAIL=0

fail() {
    echo "FAIL: $*" >&2
    FAIL=$((FAIL + 1))
}

ok() {
    echo "  OK  $*"
    PASS=$((PASS + 1))
}

# ─────────────────────────────────────────────────────────────────────────────
# isolation: check that no GhostBlade-only symbols leaked into the test binary
# ─────────────────────────────────────────────────────────────────────────────
if [ "$MODE" = "isolation" ]; then
    BUILD_DIR="${2:?usage: check.sh isolation <build_dir>}"
    TEST_BIN="$BUILD_DIR/test_ghostwisp_boot"

    if [ ! -f "$TEST_BIN" ]; then
        fail "test binary not found: $TEST_BIN"
        exit 1
    fi

    # Symbols that must NOT appear in the GhostWisp boot test binary
    # (they are GhostBlade/RK3576-only). We check for non-test symbols only:
    # filter out any symbol that starts with 'test_' (those are test function names).
    FORBIDDEN_PATTERNS=(
        "rk3576"
        "rk35"
        "blade_boot"
        "blade_main"
        "rp2350b_memmap"
    )

    for pat in "${FORBIDDEN_PATTERNS[@]}"; do
        # Get symbols, skip test_ prefixed ones, check for forbidden pattern
        if nm "$TEST_BIN" 2>/dev/null \
               | grep -v ' t test_\| T test_\| W test_\| U test_' \
               | grep -qi "$pat"; then
            fail "forbidden symbol pattern '$pat' found in $TEST_BIN (non-test symbol)"
        else
            ok "no forbidden symbol '$pat' in test binary (non-test symbols)"
        fi
    done

    # The ghost_blade linker script must not be referenced
    if strings "$TEST_BIN" 2>/dev/null | grep -qi "rp2350b_memmap.ld"; then
        fail "GhostBlade linker script 'rp2350b_memmap.ld' referenced in test binary"
    else
        ok "GhostBlade linker script not referenced in test binary"
    fi

    if [ $FAIL -gt 0 ]; then
        echo "ISOLATION: $FAIL check(s) failed" >&2
        exit 1
    fi
    echo "ISOLATION: all $PASS check(s) passed"
    exit 0
fi

# ─────────────────────────────────────────────────────────────────────────────
# headers: verify GhostWisp headers do not include GhostBlade-only files
# ─────────────────────────────────────────────────────────────────────────────
if [ "$MODE" = "headers" ]; then
    INCLUDE_DIR="${2:?usage: check.sh headers <include_dir>}"

    for hdr in "$INCLUDE_DIR"/*.h; do
        [ -f "$hdr" ] || continue
        name=$(basename "$hdr")

        # Must not include GhostBlade coprocessor headers
        for forbidden in "rk3576" "blade_boot" "blade_spi" "rp2350b_memmap"; do
            if grep -i "^#include.*$forbidden" "$hdr"; then
                fail "$name includes forbidden GhostBlade header '$forbidden'"
            else
                ok "$name: no '$forbidden' include"
            fi
        done
    done

    if [ $FAIL -gt 0 ]; then
        echo "HEADERS: $FAIL check(s) failed" >&2
        exit 1
    fi
    echo "HEADERS: all $PASS check(s) passed"
    exit 0
fi

# ─────────────────────────────────────────────────────────────────────────────
# cross: attempt a Pico SDK cross-compile to verify the firmware still builds
# ─────────────────────────────────────────────────────────────────────────────
if [ "$MODE" = "cross" ]; then
    FW_DIR="${2:?usage: check.sh cross <fw_dir> <pico_sdk_path> <cross_build_dir>}"
    PICO_SDK_PATH="${3:?}"
    CROSS_BUILD_DIR="${4:?}"

    if ! command -v arm-none-eabi-gcc &>/dev/null; then
        echo "  SKIP cross-compile probe: arm-none-eabi-gcc not found"
        exit 0
    fi
    if ! command -v cmake &>/dev/null; then
        echo "  SKIP cross-compile probe: cmake not found"
        exit 0
    fi

    echo "  Cross-compiler: $(arm-none-eabi-gcc --version | head -1)"
    echo "  Pico SDK: $PICO_SDK_PATH"

    mkdir -p "$CROSS_BUILD_DIR"
    (
        cd "$CROSS_BUILD_DIR"
        cmake -S "$FW_DIR" \
              -DPICO_SDK_PATH="$PICO_SDK_PATH" \
              -DPICO_PLATFORM=rp2350 \
              -DCMAKE_BUILD_TYPE=Release \
              -G Ninja \
              -Wno-dev \
              > cmake.log 2>&1 || { echo "  cmake configure failed (see $CROSS_BUILD_DIR/cmake.log)"; exit 1; }
        cmake --build . --target ghostwisp 2>&1 | tail -5
    )

    ELF="$CROSS_BUILD_DIR/ghostwisp.elf"
    if [ -f "$ELF" ]; then
        ok "cross-compile produced $ELF"
        # Check entry point is in expected range (RP2350B flash: 0x10000000)
        ENTRY=$(arm-none-eabi-readelf -h "$ELF" 2>/dev/null | grep "Entry point" | awk '{print $4}')
        if [[ "$ENTRY" == 0x1* ]]; then
            ok "entry point $ENTRY is in QSPI flash region (0x10000000+)"
        else
            fail "unexpected entry point: $ENTRY (expected 0x10xxxxxx for RP2350B flash)"
        fi
    else
        fail "cross-compile did not produce ghostwisp.elf"
    fi

    if [ $FAIL -gt 0 ]; then
        echo "CROSS: $FAIL check(s) failed" >&2
        exit 1
    fi
    echo "CROSS: all $PASS check(s) passed"
    exit 0
fi

echo "Usage: check.sh <isolation|headers|cross> [args...]" >&2
exit 1
