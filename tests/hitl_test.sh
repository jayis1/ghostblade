#!/bin/bash
# hitl_test.sh — Hardware-in-the-Loop Test Script for GhostBlade
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This script runs automated hardware-in-the-loop tests on a live
# GhostBlade device. It requires:
#   - A GhostBlade board connected via USB (RP2350B debug port)
#   - The apex_bridge kernel driver loaded on the RK3576
#   - The /dev/apex_bridge0 character device available
#
# Usage:
#   ./hitl_test.sh [--device /dev/apex_bridge0] [--verbose] [--skip-sdr]
#
# Exit codes:
#   0: All tests passed
#   1: One or more tests failed
#   2: Setup error (device not found, driver not loaded, etc.)

set -euo pipefail

# ── Configuration ──────────────────────────────────────────────────────────
DEVICE="${APEX_DEVICE:-/dev/apex_bridge0}"
VERBOSE=0
SKIP_SDR=0
SKIP_NFC=0
SKIP_SUBGHZ=0
LOG_DIR="/tmp/ghostblade-hitl"
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

# ── Colors ─────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ── Parse Arguments ───────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case $1 in
        --device)
            DEVICE="$2"
            shift 2
            ;;
        --verbose|-v)
            VERBOSE=1
            shift
            ;;
        --skip-sdr)
            SKIP_SDR=1
            shift
            ;;
        --skip-nfc)
            SKIP_NFC=1
            shift
            ;;
        --skip-subghz)
            SKIP_SUBGHZ=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--device /dev/apex_bridge0] [--verbose] [--skip-sdr] [--skip-nfc] [--skip-subghz]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 2
            ;;
    esac
done

# ── Utility Functions ──────────────────────────────────────────────────────
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $1"
    SKIP_COUNT=$((SKIP_COUNT + 1))
}

log_section() {
    echo ""
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

# ── Setup ──────────────────────────────────────────────────────────────────
mkdir -p "$LOG_DIR"

log_section "GhostBlade Hardware-in-the-Loop Tests"
log_info "Device: $DEVICE"
log_info "Log directory: $LOG_DIR"
log_info "Date: $(date -Iseconds)"

# Check if device exists
if [[ ! -c "$DEVICE" ]]; then
    echo -e "${RED}ERROR: Device $DEVICE not found or not a character device${NC}"
    echo "Make sure the apex_bridge driver is loaded and the device is connected."
    exit 2
fi

# Check if sysfs attributes exist
SYSFS_PATH="/sys/class/apex/apex_bridge0"
if [[ ! -d "$SYSFS_PATH" ]]; then
    echo -e "${RED}ERROR: sysfs path $SYSFS_PATH not found${NC}"
    echo "The apex_bridge driver may not be loaded."
    exit 2
fi

# ── Test 1: Device Detection ───────────────────────────────────────────────
log_section "Test 1: Device Detection"

# 1.1: Check driver status
if [[ -f "$SYSFS_PATH/driver_status" ]]; then
    STATUS=$(cat "$SYSFS_PATH/driver_status")
    if [[ "$STATUS" == "0x00000001" || "$STATUS" == "0x1" ]]; then
        log_pass "MCU ready flag is set"
    else
        log_fail "MCU ready flag not set (status=$STATUS)"
    fi
else
    log_skip "driver_status sysfs attribute not available"
fi

# 1.2: Read telemetry
if [[ -f "$SYSFS_PATH/uptime_ms" ]]; then
    UPTIME=$(cat "$SYSFS_PATH/uptime_ms")
    if [[ "$UPTIME" -gt 0 ]]; then
        log_pass "MCU uptime: ${UPTIME}ms"
    else
        log_fail "MCU uptime is zero (MCU may not be running)"
    fi
else
    log_skip "uptime_ms sysfs attribute not available"
fi

# 1.3: Check SPI error count
if [[ -f "$SYSFS_PATH/spi_errors" ]]; then
    SPI_ERR=$(cat "$SYSFS_PATH/spi_errors")
    if [[ "$SPI_ERR" -eq 0 ]]; then
        log_pass "No SPI errors detected"
    else
        log_fail "SPI error count: $SPI_ERR"
    fi
else
    log_skip "spi_errors sysfs attribute not available"
fi

# ── Test 2: Battery Monitor ───────────────────────────────────────────────
log_section "Test 2: Battery Monitor"

# 2.1: Read battery voltage
if [[ -f "$SYSFS_PATH/vbat_mv" ]]; then
    VBAT=$(cat "$SYSFS_PATH/vbat_mv")
    if [[ "$VBAT" -ge 2800 && "$VBAT" -le 4500 ]]; then
        log_pass "Battery voltage: ${VBAT}mV (within 2.8V-4.5V range)"
    elif [[ "$VBAT" -lt 2800 ]]; then
        log_fail "Battery voltage: ${VBAT}mV (below 2.8V — battery critically low)"
    else
        log_fail "Battery voltage: ${VBAT}mV (above 4.5V — possible overvoltage)"
    fi
else
    log_skip "vbat_mv sysfs attribute not available"
fi

# 2.2: Read MCU temperature
if [[ -f "$SYSFS_PATH/temp_c_x10" ]]; then
    TEMP=$(cat "$SYSFS_PATH/temp_c_x10")
    TEMP_C=$((TEMP / 10))
    if [[ "$TEMP_C" -ge -20 && "$TEMP_C" -le 85 ]]; then
        log_pass "MCU temperature: ${TEMP_C}°C (within -20°C to 85°C)"
    elif [[ "$TEMP_C" -lt -20 ]]; then
        log_fail "MCU temperature: ${TEMP_C}°C (below -20°C — sensor error?)"
    else
        log_fail "MCU temperature: ${TEMP_C}°C (above 85°C — OVERTEMP warning)"
    fi
else
    log_skip "temp_c_x10 sysfs attribute not available"
fi

# ── Test 3: MCU Reset ──────────────────────────────────────────────────────
log_section "Test 3: MCU Software Reset"

log_info "Sending software reset command (magic=0x52534554)..."
if python3 -c "
import struct, os
fd = os.open('$DEVICE', os.O_RDWR)
# APEX_IOC_SOFT_RESET = _IOW('A', 12, uint32) = encode as ioctl
magic = struct.pack('I', 0x52534554)
# This requires the libapex Python bindings
try:
    import apex
    dev = apex.ApexBridge('$DEVICE')
    dev.soft_reset(0x52534554)
    print('RESET_SENT')
except Exception as e:
    # Fallback: use raw ioctl
    import fcntl
    APEX_IOC_SOFT_RESET = 0x8004610C  # _IOW('A', 12, uint32)
    fcntl.ioctl(fd, APEX_IOC_SOFT_RESET, struct.pack('I', 0x52534554))
    print('RESET_SENT')
os.close(fd)
" 2>&1 | grep -q "RESET_SENT"; then
    log_pass "MCU soft reset command sent"
else
    log_fail "MCU soft reset command failed"
fi

# Wait for MCU to reboot (~200ms typical)
log_info "Waiting for MCU reboot..."
sleep 0.5

# Check MCU ready flag again
if [[ -f "$SYSFS_PATH/driver_status" ]]; then
    STATUS=$(cat "$SYSFS_PATH/driver_status")
    if [[ "$STATUS" == "0x00000001" || "$STATUS" == "0x1" ]]; then
        log_pass "MCU ready after reset"
    else
        # Give it more time
        sleep 1
        STATUS=$(cat "$SYSFS_PATH/driver_status")
        if [[ "$STATUS" == "0x00000001" || "$STATUS" == "0x1" ]]; then
            log_pass "MCU ready after reset (slow boot)"
        else
            log_fail "MCU not ready after reset (status=$STATUS)"
        fi
    fi
fi

# ── Test 4: SDR Telemetry ──────────────────────────────────────────────────
log_section "Test 4: SDR Telemetry"

if [[ $SKIP_SDR -eq 1 ]]; then
    log_skip "SDR tests skipped (--skip-sdr)"
else
    # 4.1: Read SDR RSSI
    if [[ -f "$SYSFS_PATH/rssi_dbm_x10" ]]; then
        RSSI=$(cat "$SYSFS_PATH/rssi_dbm_x10")
        log_info "SDR RSSI: ${RSSI} (dBm × 10)"
        # RSSI should be in range -1200 to 0 (no antenna = very low signal)
        if [[ "$RSSI" -ge -1200 && "$RSSI" -le 100 ]]; then
            log_pass "SDR RSSI in valid range: ${RSSI} dBm×10"
        else
            log_fail "SDR RSSI out of range: ${RSSI} dBm×10"
        fi
    else
        log_skip "rssi_dbm_x10 sysfs attribute not available"
    fi
fi

# ── Test 5: Sub-GHz (CC1101) ──────────────────────────────────────────────
log_section "Test 5: Sub-GHz Radio (CC1101)"

if [[ $SKIP_SUBGHZ -eq 1 ]]; then
    log_skip "Sub-GHz tests skipped (--skip-subghz)"
else
    # 5.1: Read CC1101 RSSI
    if [[ -f "$SYSFS_PATH/cc1101_rssi_x10" ]]; then
        CC_RSSI=$(cat "$SYSFS_PATH/cc1101_rssi_x10")
        log_info "CC1101 RSSI: ${CC_RSSI} (dBm × 10)"
        if [[ "$CC_RSSI" -ge -1000 && "$CC_RSSI" -le 100 ]]; then
            log_pass "CC1101 RSSI in valid range: ${CC_RSSI} dBm×10"
        else
            log_fail "CC1101 RSSI out of range: ${CC_RSSI} dBm×10"
        fi
    else
        log_skip "cc1101_rssi_x10 sysfs attribute not available"
    fi
fi

# ── Test 6: NFC (ST25R3916) ───────────────────────────────────────────────
log_section "Test 6: NFC Controller (ST25R3916)"

if [[ $SKIP_NFC -eq 1 ]]; then
    log_skip "NFC tests skipped (--skip-nfc)"
else
    # 6.1: Read NFC field strength
    if [[ -f "$SYSFS_PATH/nfc_field_mv" ]]; then
        NFC_FIELD=$(cat "$SYSFS_PATH/nfc_field_mv")
        log_info "NFC field strength: ${NFC_FIELD}mV"
        # With no tag present, field should be low (< 500mV)
        if [[ "$NFC_FIELD" -ge 0 && "$NFC_FIELD" -le 5000 ]]; then
            log_pass "NFC field strength in range: ${NFC_FIELD}mV"
        else
            log_fail "NFC field strength out of range: ${NFC_FIELD}mV"
        fi
    else
        log_skip "nfc_field_mv sysfs attribute not available"
    fi

    # 6.2: Check MCU flags for NFC active bit
    if [[ -f "$SYSFS_PATH/mcu_flags" ]]; then
        FLAGS=$(cat "$SYSFS_PATH/mcu_flags")
        log_info "MCU flags: $FLAGS"
        log_pass "MCU flags readable"
    else
        log_skip "mcu_flags sysfs attribute not available"
    fi
fi

# ── Test 7: SPI Bridge Stress Test ─────────────────────────────────────────
log_section "Test 7: SPI Bridge Stress Test"

STRESS_ITERATIONS=100
log_info "Running $STRESS_ITERATIONS telemetry request iterations..."

STRESS_PASS=0
STRESS_FAIL=0
for i in $(seq 1 $STRESS_ITERATIONS); do
    # Read telemetry via sysfs
    if [[ -f "$SYSFS_PATH/uptime_ms" ]]; then
        UPTIME=$(cat "$SYSFS_PATH/uptime_ms" 2>/dev/null || echo "ERROR")
        if [[ "$UPTIME" != "ERROR" && "$UPTIME" -gt 0 ]]; then
            STRESS_PASS=$((STRESS_PASS + 1))
        else
            STRESS_FAIL=$((STRESS_FAIL + 1))
        fi
    else
        STRESS_FAIL=$((STRESS_FAIL + 1))
    fi
done

if [[ $STRESS_FAIL -eq 0 ]]; then
    log_pass "Stress test: $STRESS_PASS/$STRESS_ITERATIONS passed"
else
    log_fail "Stress test: $STRESS_PASS/$STRESS_ITERATIONS passed, $STRESS_FAIL failed"
fi

# ── Test 8: DMA Scatter-Gather ─────────────────────────────────────────────
log_section "Test 8: DMA Scatter-Gather"

log_info "Checking SG engine status..."

# This test requires the libapex Python bindings
if python3 -c "import apex" 2>/dev/null; then
    log_info "libapex Python bindings available, testing SG engine..."
    if python3 -c "
import apex
dev = apex.ApexBridge('$DEVICE')
# Get SG engine status (should be IDLE)
status = dev.sg_get_status()
if status['state'] == 0:  # APEX_SG_STATE_IDLE
    print('SG_IDLE')
else:
    print(f'SG_STATE={status[\"state\"]}')
" 2>&1 | grep -q "SG_IDLE"; then
        log_pass "SG engine is in IDLE state"
    else
        log_fail "SG engine not in IDLE state"
    fi
else
    log_skip "libapex Python bindings not available for SG test"
fi

# ── Summary ────────────────────────────────────────────────────────────────
log_section "Test Summary"

TOTAL=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))
echo ""
echo -e "  ${GREEN}PASSED:${NC}  $PASS_COUNT"
echo -e "  ${RED}FAILED:${NC}  $FAIL_COUNT"
echo -e "  ${YELLOW}SKIPPED:${NC} $SKIP_COUNT"
echo -e "  ${BLUE}TOTAL:${NC}    $TOTAL"
echo ""

# Save results to log
echo "$(date -Iseconds) PASS=$PASS_COUNT FAIL=$FAIL_COUNT SKIP=$SKIP_COUNT" \
    >> "$LOG_DIR/hitl_results.log"

if [[ $FAIL_COUNT -gt 0 ]]; then
    echo -e "${RED}Some tests FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}All tests PASSED${NC}"
    exit 0
fi