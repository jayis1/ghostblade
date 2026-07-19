# ============================================================================
# Makefile — GhostBlade Project Top-Level Build
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Convenience targets for building sub-projects:
#   make firmware    — Build RP2350B firmware (requires Pico SDK)
#   make driver      — Build Linux kernel SPI bridge driver
#   make libapex     — Build userspace C library
#   make tests       — Build and run unit tests
#   make dtb         — Compile device tree sources
#   make validate    — Validate DTS files (syntax check)
#   make check       — Check toolchain availability
#   make clean       — Remove all build artifacts
#   make help        — Show available targets
# ============================================================================

.PHONY: all firmware driver libapex tests dtb validate validate-dts validate-netlist check clean help

all: help

help:
	@echo "GhostBlade Project Build System"
	@echo "================================"
	@echo ""
	@echo "Targets:"
	@echo "  firmware    — Build RP2350B firmware (requires Pico SDK)"
	@echo "  driver      — Build Linux kernel SPI bridge driver"
	@echo "  libapex     — Build userspace C library + Python bindings"
	@echo "  tests       — Build and run unit tests"
	@echo "  dtb         — Compile device tree sources to DTB/DTBO"
	@echo "  validate    — Validate DTS syntax"
	@echo "  validate-dts — Cross-reference DTS GPIOs with firmware and schematic"
	@echo "  validate-netlist — Cross-reference netlist, manifest, DTS, and firmware pins"
	@echo "  check       — Check toolchain availability"
	@echo "  clean       — Remove all build artifacts"
	@echo ""
	@echo "Firmware requires PICO_SDK_PATH:"
	@echo "  make firmware PICO_SDK_PATH=/path/to/pico-sdk"
	@echo ""
	@echo "Driver requires kernel source:"
	@echo "  make driver KDIR=/path/to/kernel/source"
	@echo ""
	@echo "DTS compilation requires dtc:"
	@echo "  make dtb DTS_INCLUDE_PATHS=\"-I\$$KERNEL_SRC/include/dt-bindings\""

# ── RP2350B Firmware ────────────────────────────────────────────────────────
PICO_SDK_PATH ?= $(HOME)/pico-sdk
FW_BUILD_DIR  := firmware/rp2350b/build

firmware:
	@echo "Building RP2350B firmware..."
	mkdir -p $(FW_BUILD_DIR)
	cd $(FW_BUILD_DIR) && cmake .. -DPICO_SDK_PATH=$(PICO_SDK_PATH) -DPICO_PLATFORM=rp2350
	$(MAKE) -C $(FW_BUILD_DIR)

# ── Linux Kernel Driver ─────────────────────────────────────────────────────
KDIR ?= /lib/modules/$$(shell uname -r)/build

driver:
	@echo "Building Linux kernel driver..."
	$(MAKE) -C software/linux-drivers KDIR=$(KDIR)

# ── Userspace Library ───────────────────────────────────────────────────────
libapex:
	@echo "Building libapex..."
	$(MAKE) -C software/libapex

# ── Unit Tests ──────────────────────────────────────────────────────────────
tests:
	@echo "Building and running unit tests..."
	$(MAKE) -C tests run

# ── Device Tree ──────────────────────────────────────────────────────────────
dtb:
	$(MAKE) -C software/dts all

validate:
	$(MAKE) -C software/dts validate

validate-dts:
	@echo "Running DTS cross-reference validation..."
	python3 tools/validate_dts.py

validate-netlist:
	@echo "Running netlist cross-reference validation..."
	python3 tools/validate_netlist.py

# ── Clean ────────────────────────────────────────────────────────────────────
clean:
	@echo "Cleaning all build artifacts..."
	rm -rf $(FW_BUILD_DIR)
	$(MAKE) -C software/linux-drivers clean
	$(MAKE) -C software/libapex clean
	$(MAKE) -C tests clean
	$(MAKE) -C software/dts clean
	@echo "Clean complete."

# ── Toolchain Check ──────────────────────────────────────────────────────────
check:
	@echo "GhostBlade Cross-Compilation Toolchain Check"
	@echo "=============================================="
	@echo ""
	@which $(CROSS_COMPILE)gcc >/dev/null 2>&1 && echo "✓ aarch64 GCC: $$($(CROSS_COMPILE)gcc --version | head -1)" || echo "✗ aarch64 GCC not found. Install: sudo apt install gcc-aarch64-linux-gnu"
	@which arm-none-eabi-gcc >/dev/null 2>&1 && echo "✓ ARM bare-metal GCC: $$(arm-none-eabi-gcc --version | head -1)" || echo "✗ ARM bare-metal GCC not found. Install: sudo apt install gcc-arm-none-eabi"
	@test -d $(PICO_SDK_PATH) && echo "✓ Pico SDK: $(PICO_SDK_PATH)" || echo "✗ Pico SDK not found at $(PICO_SDK_PATH)"
	@which dtc >/dev/null 2>&1 && echo "✓ DTC: $$(dtc --version | head -1)" || echo "✗ DTC not found. Install: sudo apt install device-tree-compiler"
	@test -d $(KDIR) && echo "✓ Kernel source: $(KDIR)" || echo "✗ Kernel source not found at $(KDIR)"
	@echo ""
	@echo "See software/toolchain.conf for cross-compilation environment setup."