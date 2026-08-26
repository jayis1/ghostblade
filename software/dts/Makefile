# ============================================================================
# Makefile — GhostBlade Device Tree Sources
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: GPL-2.0-or-later
# ============================================================================

DTC ?= dtc
CPP ?= cpp

CUR_DIR := $(CURDIR)
LOCAL_INCLUDE_DIR := $(CUR_DIR)/include
RK3576_DTSI ?=

# Set external include flags as needed, for example:
#   make DTS_INCLUDE_PATHS="-I/path/to/linux/include/dt-bindings -I/path/to/linux/arch/arm64/boot/dts/rockchip"
DTS_INCLUDE_PATHS ?=

CPP_DTS_FLAGS := -nostdinc -undef -x assembler-with-cpp \
	-I$(CUR_DIR) \
	-I$(LOCAL_INCLUDE_DIR) \
	$(if $(RK3576_DTSI),-I$(dir $(RK3576_DTSI)),) \
	$(DTS_INCLUDE_PATHS)

DTC_FLAGS = -Wno-unit_address_vs_reg \
	-Wno-unit_address_format \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-alias_paths \
	-Wno-graph_child_address \
	-Wno-simple_bus_reg \
	-Wno-unique_unit_address

BASE_DTS := $(if $(or $(wildcard $(CUR_DIR)/rk3576.dtsi),$(RK3576_DTSI)),ghostblade-rk3576.dts,)
DTO_SOURCES = ghostblade-options.dts \
	ghostblade-sdr-overlay.dts \
	ghostblade-cc1101-overlay.dts \
	ghostblade-nfc-overlay.dts \
	ghostblade-wifi-overlay.dts \
	ghostblade-sleep-overlay.dts \
	ghostblade-gps-overlay.dts

DTB_OUTPUTS = $(BASE_DTS:.dts=.dtb)
DTO_OUTPUTS = $(DTO_SOURCES:.dts=.dtbo)
ALL_OUTPUTS = $(DTB_OUTPUTS) $(DTO_OUTPUTS)
PREPROCESSED = $(BASE_DTS:.dts=.pp) $(DTO_SOURCES:.dts=.pp)

.PHONY: all validate clean help

all: $(ALL_OUTPUTS)
	@if [ -z "$(BASE_DTS)" ]; then \
		echo "Note: base DTB skipped (set RK3576_DTSI=/path/to/rk3576.dtsi to build ghostblade-rk3576.dtb)."; \
	fi

help:
	@echo "GhostBlade Device Tree Build"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Compile overlays and, when available, the base DTB"
	@echo "  validate   - Preprocess and validate the available DTS files"
	@echo "  clean      - Remove compiled outputs and preprocessed intermediates"
	@echo ""
	@echo "Variables:"
	@echo "  RK3576_DTSI       - Path to vendor rk3576.dtsi for base DTB builds"
	@echo "  DTS_INCLUDE_PATHS - Extra preprocessor include flags"
	@echo ""
	@echo "Examples:"
	@echo "  make validate"
	@echo "  make RK3576_DTSI=/path/to/rk3576.dtsi"
	@echo "  make DTS_INCLUDE_PATHS=\"-I/path/to/linux/include/dt-bindings\""

%.pp: %.dts
	$(CPP) $(CPP_DTS_FLAGS) $< > $@

%.dtb: %.pp
	$(DTC) $(DTC_FLAGS) -I dts -O dtb -o $@ $<

%.dtbo: %.pp
	$(DTC) $(DTC_FLAGS) -I dts -O dtb -@ -o $@ $<

validate: $(PREPROCESSED)
	@echo "Validating DTS files..."
	@if [ -z "$(BASE_DTS)" ]; then \
		echo "  Skipping ghostblade-rk3576.dts (set RK3576_DTSI to validate the base board DTS)."; \
	fi
	@for f in $(PREPROCESSED); do \
		echo "  Checking $$f..."; \
		$(DTC) $(DTC_FLAGS) -I dts -O dtb -o /dev/null $$f || exit 1; \
	done
	@echo "Available DTS files validated successfully."

clean:
	rm -f $(ALL_OUTPUTS) $(PREPROCESSED)
