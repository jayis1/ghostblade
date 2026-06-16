# CMakeLists.txt — GhostBlade RP2350B Firmware Build
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: MIT
#
# Build with Pico SDK 2.0+:
#   mkdir build && cd build
#   cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_PLATFORM=rp2350
#   ninja -v
#
# Outputs:
#   ghostblade.uf2  — USB flashable (BOOTSEL)
#   ghostblade.hex   — Intel HEX for SWD/OpenOCD
#   ghostblade.bin   — Raw binary
#   ghostblade.elf   — ELF with debug symbols

cmake_minimum_required(VERSION 3.21)

# Pull in Pico SDK — required at configure time
include(pico_sdk_import.cmake)

project(ghostblade_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ── Pico SDK board configuration ──────────────────────────────────────────────
# We are a custom board, not a Pico board. Provide our own configuration.
# The board definition lives in include/ and the memmap in rp2350b_memmap.ld.

# Pico platform: rp2350 (RP2350B variant with 2 MB PSRAM)
set(PICO_PLATFORM "rp2350" CACHE STRING "Target platform" FORCE)

# UART for debug output (default 115200 baud on pins 0/1)
set(PICO_DEFAULT_UART 1)
set(PICO_DEFAULT_UART_BAUD_RATE 115200)

# Enable USB CDC as secondary stdio (in addition to UART)
set(PICO_STDIO_USB 1)
set(PICO_STDIO_UART 1)

# ── Firmware source files ─────────────────────────────────────────────────────
set(FW_SOURCES
    src/main.c
    src/rp2350b_init.c
    src/spi_protocol.c
    src/sdr_dma.c
    src/cc1101_init.c
    src/st25r3916_init.c
    src/battery_monitor.c
    src/watchdog.c
)

set(FW_HEADERS
    include/board_pins.h
    include/rp2350b_init.h
    include/spi_protocol.h
    include/sdr_dma.h
    include/cc1101_init.h
    include/st25r3916_init.h
    include/battery_monitor.h
    include/watchdog.h
)

# ── Create executable ─────────────────────────────────────────────────────────
pico_add_executable(ghostblade ${FW_SOURCES})

# Add include path for project headers
target_include_directories(ghostblade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(ghostblade PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion
    -fno-strict-aliasing
    -fstack-usage
    -Wstack-usage=4096
    $<$<CONFIG:Release>:-Os>
    $<$<CONFIG:Debug>:-Og -g3>
)

# ── Linker options ────────────────────────────────────────────────────────────
# Custom memory map for GhostBlade board (2 MB PSRAM, 520 KB SRAM)
target_link_options(ghostblade PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/rp2350b_memmap.ld
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(ghostblade PRIVATE
    pico_stdlib
    pico_multicore
    pico_mem_ops
    hardware_dma
    hardware_spi
    hardware_adc
    hardware_watchdog
    hardware_pio
    hardware_i2c
    hardware_flash
    hardware_irq
    hardware_gpio
    pico_bootrom
)

# ── Generate UF2, HEX, BIN, MAP outputs ───────────────────────────────────────
pico_add_extra_outputs(ghostblade)

# ── Generate link map file for debug ─────────────────────────────────────────
target_link_options(ghostblade PRIVATE -Wl,-Map=ghostblade.map)

# ── Custom target: flash via OpenOCD ──────────────────────────────────────────
add_custom_target(flash-openocd
    COMMAND openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg
        -c "program ${CMAKE_BINARY_DIR}/ghostblade.elf verify reset exit"
    DEPENDS ghostblade
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flashing ghostblade.elf via OpenOCD/SWD"
)

# ── Custom target: flash via picotool ─────────────────────────────────────────
add_custom_target(flash-picotool
    COMMAND picotool load ${CMAKE_BINARY_DIR}/ghostblade.uf2
    DEPENDS ghostblade
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flashing ghostblade.uf2 via picotool (BOOTSEL mode)"
)

# ── Custom target: size report ────────────────────────────────────────────────
add_custom_target(size
    COMMAND ${CMAKE_SIZE_UTIL} ${CMAKE_BINARY_DIR}/ghostblade.elf
    DEPENDS ghostblade
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Firmware size report"
)

# ── Print configuration summary ──────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════════════╗")
message(STATUS "║  GhostBlade RP2350B Firmware Build Configuration            ║")
message(STATUS "╚══════════════════════════════════════════════════════════════╝")
message(STATUS "  Platform:          ${PICO_PLATFORM}")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler:       ${CMAKE_C_COMPILER}")
message(STATUS "  Pico SDK path:    ${PICO_SDK_PATH}")
message(STATUS "  UART baud:        ${PICO_DEFAULT_UART_BAUD_RATE}")
message(STATUS "  Sources:          ${FW_SOURCES}")
message(STATUS "")