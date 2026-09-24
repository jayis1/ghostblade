# GhostWisp host-side build and test Makefile
#
# Author: jayis1
# SPDX-License-Identifier: MIT
#
# Targets:
#   all            Build host tests (default)
#   test           Build and run host boot tests
#   check          Run all automated checks (tests + isolation + build probe)
#   clean          Remove all build outputs
#   help           Show this help
#
# Usage:
#   make test               — build and run host tests
#   make check              — full automated check suite
#   PICO_SDK_PATH=... make check   — also attempt cross-compile probe
#
# Requirements:
#   - gcc (host compiler for tests)
#   - ARM cross-compiler (optional, for target build probe):
#       arm-none-eabi-gcc or arm-none-eabi-gcc-12
#       cmake, ninja

.PHONY: all test check clean help

# ── Paths ────────────────────────────────────────────────────────────────────

REPO_ROOT     := $(shell git -C $(dir $(realpath $(lastword $(MAKEFILE_LIST)))) rev-parse --show-toplevel 2>/dev/null || echo ../..)
GHOSTWISP_DIR := $(REPO_ROOT)/devices/ghostwisp
FW_DIR        := $(GHOSTWISP_DIR)/firmware/rp2350b
TESTS_DIR     := $(GHOSTWISP_DIR)/tests
BUILD_DIR     := $(GHOSTWISP_DIR)/build

# Host compiler
CC      := gcc
CFLAGS  := -DBOOT_HOST_TEST \
            -I$(FW_DIR)/include \
            -Wall -Wextra -Wpedantic -Wconversion \
            -std=c11 \
            -g

TEST_BIN := $(BUILD_DIR)/test_ghostwisp_boot
TEST_SRC := $(TESTS_DIR)/test_ghostwisp_boot.c

# Optional cross-compiler for target probe
CROSS_CC     ?= arm-none-eabi-gcc
CMAKE        ?= cmake
NINJA        ?= ninja
PICO_SDK_PATH ?=

# ── Targets ──────────────────────────────────────────────────────────────────

all: $(TEST_BIN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(TEST_BIN): $(TEST_SRC) $(FW_DIR)/src/ghostwisp_boot.c $(FW_DIR)/include/ghostwisp_boot.h $(FW_DIR)/include/ghostwisp_pins.h | $(BUILD_DIR)
	@echo "[CC] Building host test..."
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_BIN)
	@echo "[OK] $(TEST_BIN) built"

test: $(TEST_BIN)
	@echo "[RUN] Running GhostWisp boot host tests..."
	$(TEST_BIN)

check: $(TEST_BIN)
	@echo "========================================================"
	@echo "  GhostWisp boot automated check suite"
	@echo "========================================================"
	@echo ""
	@echo "--- Step 1: Host boot tests ---"
	$(TEST_BIN)
	@echo ""
	@echo "--- Step 2: GhostBlade isolation check ---"
	@$(GHOSTWISP_DIR)/check.sh isolation $(BUILD_DIR)
	@echo ""
	@echo "--- Step 3: Header sanity (no GhostBlade includes) ---"
	@$(GHOSTWISP_DIR)/check.sh headers $(FW_DIR)/include
	@echo ""
	@if [ -n "$(PICO_SDK_PATH)" ]; then \
	    echo "--- Step 4: Cross-compile probe ---"; \
	    $(GHOSTWISP_DIR)/check.sh cross $(FW_DIR) $(PICO_SDK_PATH) $(BUILD_DIR)/cross; \
	else \
	    echo "--- Step 4: Cross-compile probe (SKIPPED — PICO_SDK_PATH not set) ---"; \
	    echo "  Set PICO_SDK_PATH=/path/to/pico-sdk to enable target build probe."; \
	fi
	@echo ""
	@echo "========================================================"
	@echo "  All automated checks PASSED"
	@echo "========================================================"

clean:
	@echo "[CLEAN] Removing $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)

help:
	@echo "GhostWisp boot automated checks"
	@echo ""
	@echo "Targets:"
	@echo "  all           Build host test binary (default)"
	@echo "  test          Build + run host boot tests"
	@echo "  check         Full automated check suite"
	@echo "  clean         Remove build directory"
	@echo ""
	@echo "Variables:"
	@echo "  CC=...              Override host C compiler (default: gcc)"
	@echo "  PICO_SDK_PATH=...   Enable cross-compile probe"
	@echo "  CROSS_CC=...        Override ARM cross-compiler"
	@echo ""
	@echo "Physical-board (HIL) requirements:"
	@echo "  The following checks are NOT automated here and require"
	@echo "  real GhostWisp hardware + SWD/UART access:"
	@echo "    - Actual clock frequencies (150/48/133 MHz)"
	@echo "    - Watchdog fires after 5 s under hardware conditions"
	@echo "    - UART output on UART1 GPIO 35/36 at 115200"
	@echo "    - USB CDC enumeration on a real host"
	@echo "    - POST peripheral SPI reads (CC1101, ST25R3916, QSPI)"
	@echo "    - Brownout detection under actual voltage drop"
	@echo "    - Signature verification with a real signed image"
	@echo "    - Radio disable pin measured high during boot"
	@echo "    - End-to-end boot time < 2 s"
