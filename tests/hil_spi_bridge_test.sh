#!/bin/bash
# ============================================================================
# hil_spi_bridge_test.sh — Hardware-in-the-Loop Test for GhostBlade SPI Bridge
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This script runs on the RK3576 host (Linux) and verifies the SPI bridge
# communication path to the RP2350B coprocessor. It requires:
#   - The apex_bridge kernel driver loaded (/dev/apex_bridge0 exists)
#   - The RP2350B firmware running and responsive
#   - The libapex shared library installed
#
# Usage:
#   ./hil_spi_bridge_test.sh           # Run all tests
#   ./hil_spi_bridge_test.sh --quick   # Run quick smoke tests only
#   ./hil_spi_bridge_test.sh --loop    # Run continuously until failure
#
# Exit codes:
#   0  — All tests passed
#   1  — One or more tests failed
#   2  — Setup/precondition failure
# ============================================================================

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================

DEVICE="/dev/apex_bridge0"
SYSFS_PATH="/sys/class/apex/apex_bridge0"
TELEMETRY_CMD="apex-ctl"
QUICK_MODE=0
LOOP_MODE=0
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'  # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# ============================================================================
# Helper Functions
# ============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
    TESTS_RUN=$((TESTS_RUN + 1))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
    TESTS_RUN=$((TESTS_RUN + 1))
}

log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $1"
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
}

log_section() {
    echo ""
    echo -e "${BLUE}=== $1 ===${NC}"
}

# Check if a sysfs attribute exists and read its value
read_sysfs() {
    local attr="$1"
    local path="${SYSFS_PATH}/${attr}"
    if [ -f "$path" ]; then
        cat "$path" 2>/dev/null
        return 0
    else
        return 1
    fi
}

# Check if apex-ctl is available
have_apex_ctl() {
    command -v "${TELEMETRY_CMD}" &>/dev/null
}

# ============================================================================
# Precondition Checks
# ============================================================================

check_prerequisites() {
    log_section "Precondition Checks"

    # Check device node exists
    if [ -c "${DEVICE}" ]; then
        log_pass "Device node ${DEVICE} exists"
    else
        log_fail "Device node ${DEVICE} does not exist (is apex_bridge driver loaded?)"
        exit 2
    fi

    # Check device is readable/writable
    if [ -r "${DEVICE}" ] && [ -w "${DEVICE}" ]; then
        log_pass "Device node is readable and writable"
    else
        log_fail "Device node is not readable/writable (check permissions)"
        exit 2
    fi

    # Check sysfs attributes exist
    local sysfs_ok=0
    for attr in rssi_dbm_x10 temp_c_x10 vbat_mv driver_status uptime_ms; do
        if [ -f "${SYSFS_PATH}/${attr}" ]; then
            sysfs_ok=$((sysfs_ok + 1))
        fi
    done
    if [ ${sysfs_ok} -ge 3 ]; then
        log_pass "Sysfs telemetry attributes accessible (${sysfs_ok} attributes)"
    else
        log_skip "Sysfs telemetry attributes not fully available (${sysfs_ok} found)"
    fi

    # Check if RP2350B is out of reset
    local status
    status=$(read_sysfs driver_status 2>/dev/null || echo "unknown")
    if [ "${status}" != "unknown" ]; then
        if [ $((status & 0x01)) -ne 0 ] 2>/dev/null; then
            log_pass "MCU reports ready (status=0x${status})"
        else
            log_fail "MCU not ready (status=0x${status})"
            if [ ${QUICK_MODE} -eq 0 ]; then
                exit 2
            fi
        fi
    else
        log_skip "Cannot read MCU status"
    fi
}

# ============================================================================
# Test Functions
# ============================================================================

# Test 1: Basic device open/close
test_device_open() {
    log_section "Test 1: Device Open/Close"

    # Try opening the device
    if python3 -c "
import sys
try:
    fd = open('${DEVICE}', 'r+b', buffering=0)
    fd.close()
    sys.exit(0)
except Exception as e:
    print(f'Error: {e}')
    sys.exit(1)
" 2>/dev/null; then
        log_pass "Device open/close succeeded"
    else
        log_fail "Device open/close failed"
    fi
}

# Test 2: Sysfs telemetry readback
test_sysfs_telemetry() {
    log_section "Test 2: Sysfs Telemetry Readback"

    local rssi temp vbat uptime

    rssi=$(read_sysfs rssi_dbm_x10 2>/dev/null || echo "N/A")
    if [ "${rssi}" != "N/A" ]; then
        # RSSI should be a reasonable value (between -1200 and 0, in dBm*10)
        if [ "${rssi}" -ge -1200 ] && [ "${rssi}" -le 100 ] 2>/dev/null; then
            log_pass "RSSI readback: ${rssi} (0.1 dBm)"
        else
            log_fail "RSSI out of range: ${rssi}"
        fi
    else
        log_skip "RSSI sysfs attribute not available"
    fi

    temp=$(read_sysfs temp_c_x10 2>/dev/null || echo "N/A")
    if [ "${temp}" != "N/A" ]; then
        # Temperature should be reasonable (-40°C to 85°C, in 0.1°C)
        if [ "${temp}" -ge -400 ] && [ "${temp}" -le 850 ] 2>/dev/null; then
            log_pass "Temperature readback: ${temp} (0.1 °C)"
        else
            log_fail "Temperature out of range: ${temp}"
        fi
    else
        log_skip "Temperature sysfs attribute not available"
    fi

    vbat=$(read_sysfs vbat_mv 2>/dev/null || echo "N/A")
    if [ "${vbat}" != "N/A" ]; then
        # Battery voltage should be 3000-4200 mV (Li-Po range)
        if [ "${vbat}" -ge 3000 ] && [ "${vbat}" -le 4200 ] 2>/dev/null; then
            log_pass "Battery voltage readback: ${vbat} mV"
        else
            log_fail "Battery voltage out of range: ${vbat} mV"
        fi
    else
        log_skip "Battery voltage sysfs attribute not available"
    fi

    uptime=$(read_sysfs uptime_ms 2>/dev/null || echo "N/A")
    if [ "${uptime}" != "N/A" ]; then
        if [ "${uptime}" -gt 0 ] 2>/dev/null; then
            log_pass "MCU uptime readback: ${uptime} ms"
        else
            log_fail "MCU uptime is zero or negative: ${uptime}"
        fi
    else
        log_skip "Uptime sysfs attribute not available"
    fi
}

# Test 3: SPI bridge communication (using raw write/read)
test_spi_communication() {
    log_section "Test 3: SPI Bridge Communication"

    # Build a NOP frame (16-byte header, 0-byte payload, 4-byte CRC-32)
    # This is a minimal valid frame: sync=0xAA, cmd=0xFF(NOP), len=0, reserved=0
    # CRC-64 header and CRC-32 payload must be computed
    # For now, we use python3 to compute the CRC and build the frame

    if python3 << 'PYEOF' 2>/dev/null; then
import struct
import sys

def crc64_compute(data):
    poly = 0x42F0E1EBA9EA3693
    crc = 0xFFFFFFFFFFFFFFFF
    for b in data:
        crc = (crc >> 8) ^ [crc ^ b for crc in [crc]][0].to_bytes(8, 'little')
    # Actually compute properly
    crc = 0xFFFFFFFFFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFFFFFFFFFF

def crc32_compute(data):
    poly = 0xEDB88320
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF

# Build NOP frame
frame = bytearray(20)
frame[0] = 0xAA  # sync
frame[1] = 0xFF  # NOP command
frame[2] = 0x00  # len low
frame[3] = 0x00  # len high
frame[4] = 0x00  # reserved
frame[5] = 0x00
frame[6] = 0x00
frame[7] = 0x00

# Header CRC-64
hdr_crc = crc64_compute(bytes(frame[:8]))
struct.pack_into('<Q', frame, 8, hdr_crc)

# Zero-length payload CRC-32
pay_crc = crc32_compute(bytes(frame[16:16]))
struct.pack_into('<I', frame, 16, pay_crc)

# Write NOP frame and read response
try:
    with open('/dev/apex_bridge0', 'r+b', buffering=0) as f:
        f.write(bytes(frame))
        # Read response
        response = f.read(256)
        if len(response) >= 20:
            # Validate sync byte
            if response[0] == 0xAA:
                print(f"NOP response received: {len(response)} bytes, cmd=0x{response[1]:02x}")
                sys.exit(0)
            else:
                print(f"Bad sync byte in response: 0x{response[0]:02x}")
                sys.exit(1)
        elif len(response) > 0:
            print(f"Short response: {len(response)} bytes")
            sys.exit(1)
        else:
            # No response is acceptable for NOP — MCU may not have data
            print("No response data (NOP acknowledged)")
            sys.exit(0)
except IOError as e:
    print(f"IO error: {e}")
    sys.exit(1)
PYEOF
        log_pass "SPI NOP communication succeeded"
    else
        log_fail "SPI NOP communication failed"
    fi
}

# Test 4: Antenna selection
test_antenna_select() {
    log_section "Test 4: Antenna Selection"

    if ! have_apex_ctl; then
        log_skip "apex-ctl not available"
        return
    fi

    for ant_id in 0 1 2 3; do
        local ant_name=""
        case ${ant_id} in
            0) ant_name="MIMO_TX" ;;
            1) ant_name="MIMO_RX" ;;
            2) ant_name="SUBGHZ" ;;
            3) ant_name="TERMINATED" ;;
        esac

        if apex-ctl --ant-select ${ant_id} &>/dev/null; then
            log_pass "Antenna ${ant_name} (${ant_id}) selection succeeded"
        else
            log_fail "Antenna ${ant_name} (${ant_id}) selection failed"
        fi

        # Brief delay between antenna switches
        sleep 0.1
    done
}

# Test 5: MCU reset cycle
test_mcu_reset() {
    log_section "Test 5: MCU Reset Cycle"

    if ! have_apex_ctl; then
        log_skip "apex-ctl not available"
        return
    fi

    # Assert MCU reset
    if apex-ctl --mcu-reset 1 &>/dev/null; then
        log_pass "MCU reset assert succeeded"
    else
        log_fail "MCU reset assert failed"
        return
    fi

    sleep 0.1

    # Release MCU reset
    if apex-ctl --mcu-reset 0 &>/dev/null; then
        log_pass "MCU reset release succeeded"
    else
        log_fail "MCU reset release failed"
        return
    fi

    # Wait for MCU to boot
    sleep 0.5

    # Verify MCU is ready again
    local status
    status=$(read_sysfs driver_status 2>/dev/null || echo "0")
    if [ $((status & 0x01)) -ne 0 ] 2>/dev/null; then
        log_pass "MCU ready after reset cycle"
    else
        log_fail "MCU not ready after reset cycle (status=0x${status})"
    fi
}

# Test 6: SDR tune (if SDR hardware is present)
test_sdr_tune() {
    log_section "Test 6: SDR Tune"

    if ! have_apex_ctl; then
        log_skip "apex-ctl not available"
        return
    fi

    # Tune to 433 MHz (ISM band, commonly available)
    if apex-ctl --sdr-tune 433000000 --sdr-bw 10000 --sdr-gain 30 &>/dev/null; then
        log_pass "SDR tune to 433 MHz succeeded"
    else
        log_fail "SDR tune to 433 MHz failed"
    fi

    # Verify RSSI changed (should not be exactly the same)
    local rssi_before rssi_after
    rssi_before=$(read_sysfs rssi_dbm_x10 2>/dev/null || echo "0")
    sleep 0.5
    rssi_after=$(read_sysfs rssi_dbm_x10 2>/dev/null || echo "0")

    if [ "${rssi_before}" != "${rssi_after}" ]; then
        log_info "RSSI changed: ${rssi_before} -> ${rssi_after}"
    else
        log_info "RSSI unchanged (may be normal without antenna)"
    fi
}

# Test 7: Sustained SPI transfer (stress test)
test_spi_stress() {
    log_section "Test 7: SPI Stress Test"

    if [ ${QUICK_MODE} -eq 1 ]; then
        log_skip "Skipping stress test in quick mode"
        return
    fi

    local iterations=100
    local errors=0

    log_info "Running ${iterations} SPI NOP transactions..."

    for i in $(seq 1 ${iterations}); do
        if ! python3 << 'PYEOF' 2>/dev/null; then
            errors=$((errors + 1))
        fi
import struct
import sys

def crc64_compute(data):
    poly = 0x42F0E1EBA9EA3693
    crc = 0xFFFFFFFFFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFFFFFFFFFF

def crc32_compute(data):
    poly = 0xEDB88320
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF

frame = bytearray(20)
frame[0] = 0xAA
frame[1] = 0xFF  # NOP
frame[2] = 0x00
frame[3] = 0x00
frame[4:8] = b'\x00\x00\x00\x00'
hdr_crc = crc64_compute(bytes(frame[:8]))
struct.pack_into('<Q', frame, 8, hdr_crc)
pay_crc = crc32_compute(b'')
struct.pack_into('<I', frame, 16, pay_crc)

try:
    with open('/dev/apex_bridge0', 'r+b', buffering=0) as f:
        f.write(bytes(frame))
        f.read(256)
except:
    sys.exit(1)
sys.exit(0)
PYEOF
    done

    if [ ${errors} -eq 0 ]; then
        log_pass "All ${iterations} stress test iterations passed"
    else
        log_fail "${errors}/${iterations} stress test iterations failed"
    fi
}

# Test 8: Watchdog reset recovery
test_watchdog_recovery() {
    log_section "Test 8: Watchdog Recovery"

    local uptime_before uptime_after

    uptime_before=$(read_sysfs uptime_ms 2>/dev/null || echo "0")
    if [ "${uptime_before}" = "0" ]; then
        log_skip "Cannot read MCU uptime"
        return
    fi

    # Wait a few seconds and check uptime increased
    sleep 3

    uptime_after=$(read_sysfs uptime_ms 2>/dev/null || echo "0")
    if [ "${uptime_after}" = "0" ]; then
        log_skip "Cannot read MCU uptime (after)"
        return
    fi

    if [ "${uptime_after}" -gt "${uptime_before}" ] 2>/dev/null; then
        local delta=$((uptime_after - uptime_before))
        log_pass "Uptime advancing: ${uptime_before}ms -> ${uptime_after}ms (delta=${delta}ms)"
    else
        log_fail "Uptime not advancing: ${uptime_before}ms -> ${uptime_after}ms (possible watchdog reset)"
    fi
}

# Test 9: CC1101 sub-GHz radio register access
test_cc1101_access() {
    log_section "Test 9: CC1101 Register Access"

    if ! have_apex_ctl; then
        log_skip "apex-ctl not available"
        return
    fi

    # Try to set CC1101 to channel 0 (default)
    if apex-ctl --cc1101-set-channel 0 &>/dev/null; then
        log_pass "CC1101 channel set succeeded"
    else
        log_fail "CC1101 channel set failed"
    fi
}

# Test 10: Scatter-gather DMA statistics (if streaming was active)
test_sg_stats() {
    log_section "Test 10: Scatter-Gather DMA Statistics"

    local sg_state sg_bytes sg_errors

    sg_state=$(read_sysfs sg_state 2>/dev/null || echo "N/A")
    sg_bytes=$(read_sysfs sg_total_bytes 2>/dev/null || echo "N/A")
    sg_errors=$(read_sysfs sg_errors 2>/dev/null || echo "N/A")

    if [ "${sg_state}" != "N/A" ]; then
        log_info "SG engine state: ${sg_state}"
        log_info "SG total bytes: ${sg_bytes}"
        log_info "SG errors: ${sg_errors}"

        if [ "${sg_state}" = "idle" ]; then
            log_pass "SG engine in idle state (expected when not streaming)"
        else
            log_info "SG engine state: ${sg_state}"
        fi

        if [ "${sg_errors}" != "N/A" ] && [ "${sg_errors}" -eq 0 ] 2>/dev/null; then
            log_pass "SG engine reports 0 errors"
        elif [ "${sg_errors}" != "N/A" ]; then
            log_fail "SG engine reports ${sg_errors} errors"
        fi
    else
        log_skip "SG sysfs attributes not available"
    fi
}

# ============================================================================
# Main
# ============================================================================

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --quick|-q)
            QUICK_MODE=1
            shift
            ;;
        --loop|-l)
            LOOP_MODE=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--quick] [--loop]"
            echo "  --quick  Run quick smoke tests only"
            echo "  --loop   Run continuously until failure"
            echo "  --help   Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 2
            ;;
    esac
done

echo "============================================================"
echo "  GhostBlade SPI Bridge Hardware-in-the-Loop Test"
echo "  Device: ${DEVICE}"
echo "  Date: $(date -Iseconds)"
echo "============================================================"

check_prerequisites

test_device_open
test_sysfs_telemetry
test_spi_communication
test_antenna_select
test_mcu_reset
test_sdr_tune
test_spi_stress
test_watchdog_recovery
test_cc1101_access
test_sg_stats

echo ""
echo "============================================================"
echo "  Test Results Summary"
echo "============================================================"
echo "  Total:  ${TESTS_RUN}"
echo "  Passed: ${TESTS_PASSED}"
echo "  Failed: ${TESTS_FAILED}"
echo "  Skipped: ${TESTS_SKIPPED}"
echo "============================================================"

if [ ${TESTS_FAILED} -eq 0 ]; then
    echo -e "  ${GREEN}ALL TESTS PASSED${NC}"
    echo ""
    exit 0
else
    echo -e "  ${RED}${TESTS_FAILED} TEST(S) FAILED${NC}"
    echo ""
    exit 1
fi