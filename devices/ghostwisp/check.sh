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

    for tool in nm strings grep; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            fail "required isolation tool not found: $tool"
            exit 1
        fi
    done

    # Author: jayis1 — failed inspection is not evidence of isolation.
    # Capture once so tool errors cannot be mistaken for absent patterns.
    if ! SYMBOLS=$(nm "$TEST_BIN"); then
        fail "nm could not inspect $TEST_BIN"
        exit 1
    fi
    if [ -z "$SYMBOLS" ]; then
        fail "nm returned no symbols; isolation requires an unstripped test binary"
        exit 1
    fi
    if ! BINARY_STRINGS=$(strings "$TEST_BIN"); then
        fail "strings could not inspect $TEST_BIN"
        exit 1
    fi

    # Filtering every symbol is valid (grep status 1); execution errors are not.
    if FILTERED_SYMBOLS=$(grep -v ' t test_\| T test_\| W test_\| U test_' <<< "$SYMBOLS"); then
        :
    else
        STATUS=$?
        if [ "$STATUS" -ne 1 ]; then
            fail "grep could not filter the symbol table"
            exit 1
        fi
    fi

    for pat in "${FORBIDDEN_PATTERNS[@]}"; do
        # Consume all input: grep -q may cause upstream SIGPIPE under pipefail.
        if grep -i "$pat" <<< "$FILTERED_SYMBOLS" >/dev/null; then
            fail "forbidden symbol pattern '$pat' found in $TEST_BIN (non-test symbol)"
        else
            STATUS=$?
            if [ "$STATUS" -ne 1 ]; then
                fail "grep could not inspect symbol pattern '$pat'"
                exit 1
            fi
            ok "no forbidden symbol '$pat' in test binary (non-test symbols)"
        fi
    done

    # The ghost_blade linker script must not be referenced
    if grep -i "rp2350b_memmap.ld" <<< "$BINARY_STRINGS" >/dev/null; then
        fail "GhostBlade linker script 'rp2350b_memmap.ld' referenced in test binary"
    else
        STATUS=$?
        if [ "$STATUS" -ne 1 ]; then
            fail "grep could not inspect linker script references"
            exit 1
        fi
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

    # Author: jayis1 — no inspection is not evidence of header isolation.
    if [ ! -d "$INCLUDE_DIR" ]; then
        fail "header directory not found: $INCLUDE_DIR"
        exit 1
    fi
    if ! command -v grep >/dev/null 2>&1; then
        fail "required header inspection tool not found: grep"
        exit 1
    fi
    shopt -s nullglob globstar
    HEADERS=("$INCLUDE_DIR"/**/*.h)
    if [ ${#HEADERS[@]} -eq 0 ]; then
        fail "no headers found in $INCLUDE_DIR"
        exit 1
    fi

    for hdr in "${HEADERS[@]}"; do
        if [ ! -f "$hdr" ] || [ ! -r "$hdr" ]; then
            fail "header is not a readable regular file: $hdr"
            exit 1
        fi
        name=${hdr#"$INCLUDE_DIR"/}

        # Inspect nested board headers too; preserve the forbidden-name rules.
        # This is a source-text gate, not a C preprocessor/dependency analysis.
        for forbidden in "rk3576" "blade_boot" "blade_spi" "rp2350b_memmap"; do
            if grep -i "^[[:space:]]*#[[:space:]]*include[[:space:]]*.*$forbidden" "$hdr"; then
                fail "$name includes forbidden GhostBlade header '$forbidden'"
            else
                STATUS=$?
                if [ "$STATUS" -ne 1 ]; then
                    fail "grep could not inspect $name for '$forbidden'"
                    exit 1
                fi
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

    # Author: jayis1 — this mode is explicitly requested, so missing tools
    # must fail rather than turn an unperformed target build into a pass.
    # Ninja is selected below; readelf is required for output inspection.
    for tool in arm-none-eabi-gcc cmake ninja arm-none-eabi-readelf; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            fail "required cross-compile tool not found: $tool"
            exit 1
        fi
    done

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
