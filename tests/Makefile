# ============================================================================
# Makefile — GhostBlade Test Suite
#
# Copyright (C) 2026 GhostBlade Project
# Author: jayis1
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build targets:
#   test_spi_protocol     — Userspace SPI protocol unit tests
#   test_battery_monitor  — Battery monitor calculation unit tests
#   test_cc1101_config    — CC1101 register configuration unit tests
#   test_watchdog         — Watchdog timer unit tests
#   test_power_states     — Power state machine unit tests
#   test_sleep_wake       — Sleep/wake state machine unit tests
#   test_libapex          — libapex userspace library unit tests
#   test_sdr_dma          — SDR DMA ring buffer logic unit tests
#   test_spi0_isr         — SPI0 ISR frame assembly state machine unit tests
#   test_apex_bridge      — Kernel module test harness (for target)
#   test_st25r3916_init   — ST25R3916 NFC controller init unit tests
#   test_adc_calibration  — ADC calibration unit tests
#   test_peripheral_power — Peripheral power sequencing unit tests
#   test_cc1101_lms7002m  — CC1101 and LMS7002M initialization unit tests
#   test_crc_validation  — SPI protocol CRC-64/CRC-32 validation tests
#   test_es8388_audio    — ES8388 audio codec driver and SPI command tests
#   test_walkie_talkie   — Walkie-talkie module PTT state machine and config tests (Python)
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
USERSPACE_TARGETS = test_spi_protocol test_battery_monitor test_cc1101_config test_watchdog test_power_states test_sleep_wake test_libapex test_libapex_api test_libapex_framing test_sdr_dma test_spi0_isr test_st25r3916_init test_adc_calibration test_adc_calibration_production test_peripheral_power test_cc1101_lms7002m test_crc_validation test_es8388_audio test_ghostwisp_boot test_ghostwisp_peripherals

# Kernel module sources (build separately on target)
KMOD_SOURCES = test_apex_bridge.c

.PHONY: all clean run check help

USERSPACE_TARGETS += test_ghostwisp_gpio_mmio
USERSPACE_TARGETS += test_temperature_production
USERSPACE_TARGETS += test_ghostwisp_i2c_addresses
USERSPACE_TARGETS += test_rp2350b_gpio_mmio

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
	@echo "  test_sleep_wake       - Build sleep/wake state machine unit tests"
	@echo "  test_libapex          - Build libapex userspace library unit tests"
	@echo "  test_libapex_framing  - Build libapex frame encoding/parsing unit tests"
	@echo "  test_sdr_dma          - Build SDR DMA ring buffer unit tests"
	@echo "  test_spi0_isr         - Build SPI0 ISR frame assembly unit tests"
	@echo "  test_st25r3916_init   - Build ST25R3916 NFC controller init tests"
	@echo "  test_adc_calibration  - Build ADC calibration unit tests"
	@echo "  test_peripheral_power - Build peripheral power sequencing unit tests"
	@echo "  test_cc1101_lms7002m - Build CC1101 and LMS7002M init tests"
	@echo "  test_crc_validation  - Build SPI protocol CRC validation tests"
	@echo "  test_es8388_audio    - Build ES8388 audio codec unit tests"
	@echo "  test_ghostwisp_boot  - Build GhostWisp boot sequence unit tests"
	@echo "  test_ghostwisp_peripherals - Build GhostWisp peripheral driver unit tests"
	@echo "  run / check           - Build and run all userspace tests"
	@echo "  clean                 - Remove build artifacts"
	@echo ""
	@echo "Kernel module test (build on target):"
	@echo "  make -C /lib/modules/\$$(uname -r)/build M=\$$(pwd) modules"

test_spi_protocol: test_spi_protocol.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_rp2350b_gpio_mmio: test_rp2350b_gpio_mmio.c ../firmware/rp2350b/src/rp2350b_gpio.c ../firmware/rp2350b/include/rp2350b_init.h
	$(CC) $(CFLAGS) -UNDEBUG -DRP2350B_GPIO_MMIO_TEST -I$(CURDIR)/../firmware/rp2350b/include -o $@ test_rp2350b_gpio_mmio.c ../firmware/rp2350b/src/rp2350b_gpio.c $(LDFLAGS)

test_battery_monitor: test_battery_monitor.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lm

test_cc1101_config: test_cc1101_config.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_watchdog: test_watchdog.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_power_states: test_power_states.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_sleep_wake: test_sleep_wake.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_libapex: test_libapex.c
	$(CC) $(CFLAGS) -I$(CURDIR)/../software/libapex -o $@ $< $(LDFLAGS) -lm

test_libapex_framing: test_libapex_framing.c
	$(CC) $(CFLAGS) -I$(CURDIR)/../software/libapex/include -o $@ $< $(LDFLAGS)

test_libapex_api: test_libapex_api.c ../software/libapex/src/libapex.c ../software/libapex/include/libapex.h
	$(CC) $(CFLAGS) -I$(CURDIR)/../software/libapex/include -o $@ test_libapex_api.c ../software/libapex/src/libapex.c -Wl,--wrap=ioctl $(LDFLAGS) -lm

test_sdr_dma: test_sdr_dma.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_spi0_isr: test_spi0_isr.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_st25r3916_init: test_st25r3916_init.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_adc_calibration: test_adc_calibration.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lm

test_adc_calibration_production: test_adc_calibration_production.c ../firmware/rp2350b/src/adc_calibration.c ../firmware/rp2350b/include/adc_calibration.h ../firmware/rp2350b/include/adc_temperature.h
	$(CC) $(CFLAGS) -I$(CURDIR)/../firmware/rp2350b/include -o $@ $< $(LDFLAGS)

test_temperature_production: test_temperature_production.c ../firmware/rp2350b/src/battery_monitor.c ../firmware/rp2350b/include/battery_monitor.h ../firmware/rp2350b/include/adc_temperature.h
	$(CC) $(CFLAGS) -I$(CURDIR)/../firmware/rp2350b/include -o $@ $< $(LDFLAGS) -lm

test_peripheral_power: test_peripheral_power.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_cc1101_lms7002m: test_cc1101_lms7002m.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_crc_validation: test_crc_validation.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_es8388_audio: test_es8388_audio.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_ghostwisp_gpio_mmio: test_ghostwisp_gpio_mmio.c ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_gpio.c ../devices/ghostwisp/firmware/rp2350b/include/ghostwisp_gpio.h ../devices/ghostwisp/firmware/rp2350b/include/ghostwisp_pins.h
	$(CC) $(CFLAGS) -UNDEBUG -UGHOSTWISP_GPIO_MMIO_TEST -UDRIVER_HOST_TEST -DGHOSTWISP_GPIO_MMIO_TEST -I$(CURDIR)/../devices/ghostwisp/firmware/rp2350b/include -o $@ test_ghostwisp_gpio_mmio.c ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_gpio.c $(LDFLAGS)

test_ghostwisp_i2c_addresses: test_ghostwisp_i2c_addresses.c ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_i2c.c ../devices/ghostwisp/firmware/rp2350b/include/ghostwisp_i2c.h
	$(CC) $(CFLAGS) -UNDEBUG -DDRIVER_HOST_TEST -I$(CURDIR)/../devices/ghostwisp/firmware/rp2350b/include -o $@ test_ghostwisp_i2c_addresses.c ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_i2c.c $(LDFLAGS)

test_ghostwisp_boot: test_ghostwisp_boot.c
	$(CC) $(CFLAGS) -DBOOT_HOST_TEST -I$(CURDIR)/../devices/ghostwisp/firmware/rp2350b/include -o $@ $< ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_boot.c $(LDFLAGS)

test_ghostwisp_peripherals: test_ghostwisp_peripherals.c
	$(CC) $(CFLAGS) -DDRIVER_HOST_TEST -I$(CURDIR)/../devices/ghostwisp/firmware/rp2350b/include -o $@ $< \
		../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_gpio.c \
		../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_i2c.c \
		../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_spi.c \
		../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_uart.c \
		$(LDFLAGS)

.PHONY: test_walkie_talkie
test_walkie_talkie:
	@echo "=== Running Walkie-Talkie Python Unit Tests ==="
	@python3 test_walkie_talkie.py

run: $(USERSPACE_TARGETS)
	@./test_rp2350b_gpio_mmio
	@./test_ghostwisp_i2c_addresses
	@./test_temperature_production
	@./test_ghostwisp_gpio_mmio
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
	@echo "=== Running Sleep/Wake State Machine Unit Tests ==="
	@./test_sleep_wake
	@echo ""
	@echo "=== Running libapex Unit Tests ==="
	@./test_libapex
	@echo ""
	@echo "=== Running libapex Production API Regression Tests ==="
	@./test_libapex_api
	@echo ""
	@echo "=== Running libapex Frame Encoding/Parsing Unit Tests ==="
	@./test_libapex_framing
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
	@echo "=== Running ADC Calibration Unit Tests ==="
	@./test_adc_calibration
	@./test_adc_calibration_production
	@echo ""
	@echo "=== Running Peripheral Power Sequencing Unit Tests ==="
	@./test_peripheral_power
	@echo ""
	@echo "=== Running CC1101 and LMS7002M Initialization Unit Tests ==="
	@./test_cc1101_lms7002m
	@echo ""
	@echo "=== Running CRC Validation Unit Tests ==="
	@./test_crc_validation
	@echo ""
	@echo "=== Running ES8388 Audio Codec Unit Tests ==="
	@./test_es8388_audio
	@echo ""
	@echo "=== Running GhostWisp Boot Sequence Unit Tests ==="
	@./test_ghostwisp_boot
	@echo ""
	@echo "=== Running GhostWisp Peripheral Driver Unit Tests ==="
	@./test_ghostwisp_peripherals
	@echo ""
	@echo "=== Running Walkie-Talkie Python Unit Tests ==="
	@python3 test_walkie_talkie.py
	@echo ""
	@echo "=== Running Netlist Name Coverage Regression Tests ==="
	@python3 test_netlist_validation.py -v
	@echo ""
	@echo "=== Running Fabrication Export Regression Tests ==="
	@python3 test_generate_gerbers.py -v
	@echo ""
	@echo "=== Running Documentation Link Regression Tests ==="
	@python3 test_doc_links.py -v
	@echo ""
	@echo "=== Running Cross-Build Prerequisite Regression Tests ==="
	@python3 test_toolchain_check.py -v
	@echo ""
	@echo "=== Running Device Tree Build Regression Tests ==="
	@python3 test_dts_build.py -v
	@echo ""
	@echo "=== Running Production Python Extension Lifecycle Tests ==="
	@python3 test_pyapex_lifecycle.py -v
	@echo ""

check: run

clean:
	rm -f $(USERSPACE_TARGETS)
	rm -f *.o *.ko *.mod.c *.mod .*.cmd
	rm -rf .tmp_versions/ Module.symvers/ modules.order