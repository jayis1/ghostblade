# ============================================================================
# Makefile — GhostBlade Device Tree Sources
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build targets:
#   all        — Compile all DTS files to DTB/DTBO
#   validate   — Validate DTS syntax without producing output
#   clean      — Remove compiled DTB/DTBO files
#
# Usage:
#   make                           # Build all DTB/DTBO files
#   make CROSS_COMPILE=aarch64-linux-gnu-  # Cross-compile
#   make validate                  # Validate syntax only
#   make clean                     # Remove outputs
#
# Prerequisites:
#   device-tree-compiler (dtc) must be installed.
#   For full DTS validation with includes, you need the kernel
#   DTS include paths (set DTS_INCLUDE_PATHS).
# ============================================================================

DTC     ?= dtc
CROSS_COMPILE ?=

# DTS source files
DTS_SOURCES = ghostblade-rk3576.dts

# Overlay sources (compiled to .dtbo)
DTO_SOURCES = ghostblade-options.dts \
              ghostblade-sdr-overlay.dts \
              ghostblade-cc1101-overlay.dts \
              ghostblade-nfc-overlay.dts \
              ghostblade-wifi-overlay.dts \
              ghostblade-sleep-overlay.dts

# Output files
DTB_OUTPUTS = $(DTS_SOURCES:.dts=.dtb)
DTO_OUTPUTS = $(DTO_SOURCES:.dts=.dtbo)
ALL_OUTPUTS = $(DTB_OUTPUTS) $(DTO_OUTPUTS)

# DTS include paths (for resolving #include directives)
# Set this to your kernel source DTS include directory for full validation:
#   make DTS_INCLUDE_PATHS="-I/path/to/linux/include/dt-bindings -I/path/to/linux/arch/arm64/boot/dts/rockchip"
DTS_INCLUDE_PATHS ?=

# Common dtc flags
DTC_FLAGS = -Wno-unit_address_vs_reg \
            -Wno-unit_address_format \
            -Wno-avoid_unnecessary_addr_size \
            -Wno-alias_paths \
            -Wno-graph_child_address \
            -Wno-simple_bus_reg \
            -Wno-unique_unit_address \
            $(DTS_INCLUDE_PATHS)

.PHONY: all validate clean help

all: $(ALL_OUTPUTS)

help:
	@echo "GhostBlade Device Tree Build"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Compile all DTS files to DTB/DTBO"
	@echo "  validate   - Validate DTS syntax without producing output"
	@echo "  clean      - Remove compiled DTB/DTBO files"
	@echo ""
	@echo "Variables:"
	@echo "  DTC                - Device tree compiler (default: dtc)"
	@echo "  DTS_INCLUDE_PATHS  - Extra include paths for dtc"
	@echo ""
	@echo "Examples:"
	@echo "  make                              # Build all DTB/DTBO"
	@echo "  make validate                     # Check syntax only"
	@echo "  make DTS_INCLUDE_PATHS=\"-I\$$KERNEL_SRC/include/dt-bindings\""

# Compile base DTS to DTB
%.dtb: %.dts
	$(DTC) $(DTC_FLAGS) -I dts -O dtb -o $@ $<

# Compile overlay DTS to DTBO
%.dtbo: %.dts
	$(DTC) $(DTC_FLAGS) -I dts -O dtb -@ -o $@ $<

# Validate DTS files without producing output
validate:
	@echo "Validating DTS files..."
	@for f in $(DTS_SOURCES) $(DTO_SOURCES); do \
		echo "  Checking $$f..."; \
		$(DTC) $(DTC_FLAGS) -I dts -O dtb -o /dev/null $$f || exit 1; \
	done
	@echo "All DTS files validated successfully."

clean:
	rm -f $(ALL_OUTPUTS)