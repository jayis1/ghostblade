# ============================================================================
# Makefile — GhostBlade Test Suite
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build targets:
#   test_spi_protocol     — Userspace SPI protocol unit tests
#   test_battery_monitor  — Battery monitor calculation unit tests
#   test_cc1101_config    — CC1101 register configuration unit tests
#   test_watchdog         — Watchdog timer unit tests
#   test_power_states     — Power state machine unit tests
#   test_libapex          — libapex userspace library unit tests
#   test_sdr_dma          — SDR DMA ring buffer logic unit tests
#   test_spi0_isr         — SPI0 ISR frame assembly state machine unit tests
#   test_apex_bridge      — Kernel module test harness (for target)
#   all                   — Build all userspace tests
#   clean                 — Remove build artifacts
#
# Usage:
#   make                       # Build all userspace tests
#   make test_spi_protocol     # Build SPI protocol tests only
#   make test_battery_monitor  # Build battery monitor tests only
#   make test_cc1101_config    # Build CC1101 config tests only
#   make test_sdr_dma          # Build SDR DMA ring buffer tests only
#   make test_spi0_isr         # Build SPI0 ISR frame assembly tests only
#   make run                   # Build and run all userspace tests
#   make check                 # Same as 'make run'
#
# For the kernel module test:
#   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
#   (See test_apex_bridge.c for details)
# ============================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -DNO_CMOCKA -g -O2
LDFLAGS =

# Userspace test targets
USERSPACE_TARGETS = test_spi_protocol test_battery_monitor test_cc1101_config test_watchdog test_power_states test_libapex test_sdr_dma test_spi0_isr test_st25r3916_init

# Kernel module sources (build separately on target)
KMOD_SOURCES = test_apex_bridge.c

.PHONY: all clean run check help

all: $(USERSPACE_TARGETS)

help:
	@echo "GhostBlade Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all                   - Build all userspace tests"
	@echo "  test_spi_protocol     - Build SPI protocol unit tests"
	@echo "  test_battery_monitor  - Build battery monitor unit tests"
	@echo "  test_cc1101_config    - Build CC1101 configuration unit tests"
	@echo "  test_watchdog         - Build watchdog timer unit tests"
	@echo "  test_power_states     - Build power state machine unit tests"
	@echo "  test_libapex          - Build libapex userspace library unit tests"
	@echo "  test_sdr_dma          - Build SDR DMA ring buffer unit tests"
	@echo "  test_spi0_isr         - Build SPI0 ISR frame assembly unit tests"
	@echo "  test_st25r3916_init   - Build ST25R3916 NFC controller init tests"
	@echo "  run / check           - Build and run all userspace tests"
	@echo "  clean                 - Remove build artifacts"
	@echo ""
	@echo "Kernel module test (build on target):"
	@echo "  make -C /lib/modules/\$$(uname -r)/build M=\$$(pwd) modules"

test_spi_protocol: test_spi_protocol.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_battery_monitor: test_battery_monitor.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lm

test_cc1101_config: test_cc1101_config.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_watchdog: test_watchdog.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_power_states: test_power_states.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_libapex: test_libapex.c
	$(CC) $(CFLAGS) -I$(CURDIR)/../software/libapex -o $@ $< $(LDFLAGS) -lm

test_sdr_dma: test_sdr_dma.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_spi0_isr: test_spi0_isr.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_st25r3916_init: test_st25r3916_init.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: $(USERSPACE_TARGETS)
	@echo "=== Running SPI Protocol Unit Tests ==="
	@./test_spi_protocol
	@echo ""
	@echo "=== Running Battery Monitor Unit Tests ==="
	@./test_battery_monitor
	@echo ""
	@echo "=== Running CC1101 Configuration Unit Tests ==="
	@./test_cc1101_config
	@echo ""
	@echo "=== Running Watchdog Timer Unit Tests ==="
	@./test_watchdog
	@echo ""
	@echo "=== Running Power State Machine Unit Tests ==="
	@./test_power_states
	@echo ""
	@echo "=== Running libapex Unit Tests ==="
	@./test_libapex
	@echo ""
	@echo "=== Running SDR DMA Ring Buffer Unit Tests ==="
	@./test_sdr_dma
	@echo ""
	@echo "=== Running SPI0 ISR Frame Assembly Unit Tests ==="
	@./test_spi0_isr
	@echo ""
	@echo "=== Running ST25R3916 NFC Controller Init Unit Tests ==="
	@./test_st25r3916_init
	@echo ""

check: run

clean:
	rm -f $(USERSPACE_TARGETS)
	rm -f *.o *.ko *.mod.c *.mod .*.cmd
	rm -rf .tmp_versions/ Module.symvers/ modules.order