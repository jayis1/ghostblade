# ============================================================================
# Makefile — GhostBlade Test Suite
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build targets:
#   test_spi_protocol  — Userspace SPI protocol unit tests
#   test_apex_bridge   — Kernel module test harness (for target)
#   all                — Build all userspace tests
#   clean              — Remove build artifacts
#
# Usage:
#   make                    # Build all userspace tests
#   make test_spi_protocol  # Build SPI protocol tests only
#   make run                # Build and run userspace tests
#   make check              # Same as 'make run'
#
# For the kernel module test:
#   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
#   (See test_apex_bridge.c for details)
# ============================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -DNO_CMOCKA -g -O2
LDFLAGS =

# Userspace test targets
USERSPACE_TARGETS = test_spi_protocol

# Kernel module sources (build separately on target)
KMOD_SOURCES = test_apex_bridge.c

.PHONY: all clean run check help

all: $(USERSPACE_TARGETS)

help:
	@echo "GhostBlade Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all                - Build all userspace tests"
	@echo "  test_spi_protocol  - Build SPI protocol unit tests"
	@echo "  run / check        - Build and run userspace tests"
	@echo "  clean              - Remove build artifacts"
	@echo ""
	@echo "Kernel module test (build on target):"
	@echo "  make -C /lib/modules/\$$(uname -r)/build M=\$$(pwd) modules"

test_spi_protocol: test_spi_protocol.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: test_spi_protocol
	@echo "=== Running SPI Protocol Unit Tests ==="
	@./test_spi_protocol
	@echo ""

check: run

clean:
	rm -f $(USERSPACE_TARGETS)
	rm -f *.o *.ko *.mod.c *.mod .*.cmd
	rm -rf .tmp_versions/ Module.symvers/ modules.order