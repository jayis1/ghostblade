# ============================================================================
# Makefile — GhostBlade Project Top-Level Build
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Convenience targets for building sub-projects.
# ============================================================================

.PHONY: all firmware driver libapex tests dtb validate validate-dts validate-netlist docs-check check clean help reproducible

all: help

help:
	@echo "GhostBlade Project Build System"
	@echo "================================"
	@echo ""
	@echo "Targets:"
	@echo "  firmware         - Build RP2350B firmware (requires Pico SDK)"
	@echo "  driver           - Build Linux kernel SPI bridge driver"
	@echo "  libapex          - Build userspace C library (incremental)"
	@echo "  tests            - Build and run host-side unit tests"
	@echo "  dtb              - Compile device tree sources to DTB/DTBO"
	@echo "  validate         - Validate DTS syntax"
	@echo "  validate-dts     - Cross-reference DTS GPIOs with firmware and schematic"
	@echo "  validate-netlist - Cross-reference netlist, manifest, DTS, and firmware pins"
	@echo "  validate-netlist-strict - Also fail on missing exact manifest net names"
	@echo "  docs-check       - Verify internal markdown links"
	@echo "  reproducible     - Print suggested reproducible-build environment exports"
	@echo "  check            - Check toolchain availability"
	@echo "  clean            - Remove build artifacts"
	@echo ""
	@echo "Firmware example:"
	@echo "  make firmware PICO_SDK_PATH=/path/to/pico-sdk"
	@echo ""
	@echo "Driver example:"
	@echo "  make driver KDIR=/path/to/kernel/source ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-"

PICO_SDK_PATH ?= $(HOME)/pico-sdk
FW_BUILD_DIR  := firmware/rp2350b/build
KDIR ?= /lib/modules/$$(uname -r)/build

firmware:
	@echo "Building RP2350B firmware..."
	cmake -S firmware/rp2350b -B $(FW_BUILD_DIR) \
		-DPICO_SDK_PATH=$(PICO_SDK_PATH) \
		-DPICO_PLATFORM=rp2350
	cmake --build $(FW_BUILD_DIR)

driver:
	@echo "Building Linux kernel driver..."
	$(MAKE) -C software/linux-drivers KDIR=$(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)

libapex:
	@echo "Building libapex..."
	$(MAKE) -C software/libapex all

tests:
	@echo "Building and running unit tests..."
	$(MAKE) -C tests run

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

# Author: jayis1
.PHONY: validate-netlist-strict
validate-netlist-strict:
	python3 tools/validate_netlist.py --strict-net-names

docs-check:
	python3 tools/check_internal_links.py

reproducible:
	@echo "export SOURCE_DATE_EPOCH=$$(git log -1 --format=%ct)"
	@echo 'export KBUILD_BUILD_TIMESTAMP="$$(date -u -d @$$SOURCE_DATE_EPOCH "+%Y-%m-%d %H:%M:%S")"'
	@echo "export PYTHONHASHSEED=0"

clean:
	@echo "Cleaning all build artifacts..."
	rm -rf $(FW_BUILD_DIR)
	$(MAKE) -C software/linux-drivers clean || true
	$(MAKE) -C software/libapex clean
	$(MAKE) -C tests clean
	$(MAKE) -C software/dts clean
	@echo "Clean complete."

# Author: jayis1
# Keep the check's default separate from native driver builds (CROSS_COMPILE="").
CHECK_AARCH64_CC ?= $(if $(CROSS_COMPILE),$(CROSS_COMPILE)gcc,aarch64-linux-gnu-gcc)
CHECK_ARM_CC ?= arm-none-eabi-gcc
CHECK_DTC ?= dtc
CHECK_CMAKE ?= cmake

check:
	python3 tools/check_toolchain.py \
		--aarch64-cc "$(CHECK_AARCH64_CC)" --arm-cc "$(CHECK_ARM_CC)" \
		--dtc "$(CHECK_DTC)" --cmake "$(CHECK_CMAKE)" \
		--pico-sdk "$(PICO_SDK_PATH)" --kernel-dir "$(KDIR)"
