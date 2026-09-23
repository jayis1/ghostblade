# CMakeLists.txt — GhostWisp RP2350B Firmware Build
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: MIT
#
# GhostWisp (Project Little Spectre) is a standalone RP2350B device.
# Unlike GhostBlade's coprocessor firmware (firmware/rp2350b/), this is
# the sole processor — there is no RK3576 host bridge.
#
# Build with Pico SDK 2.0+:
#   mkdir build && cd build
#   cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_PLATFORM=rp2350
#   ninja -v
#
# Outputs:
#   ghostwisp.uf2  — USB flashable (BOOTSEL)
#   ghostwisp.hex  — Intel HEX for SWD/OpenOCD
#   ghostwisp.bin  — Raw binary
#   ghostwisp.elf  — ELF with debug symbols

cmake_minimum_required(VERSION 3.21)

# Pull in Pico SDK — required at configure time
include(pico_sdk_import.cmake)

project(ghostwisp_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ── Pico SDK board configuration ──────────────────────────────────────────────
# GhostWisp is a custom board, not a Pico board.
# The board definition lives in include/ and the pin map in ghostwisp_pins.h.

set(PICO_PLATFORM "rp2350" CACHE STRING "Target platform" FORCE)

# UART for debug output (default 115200 baud on expansion header pins)
set(PICO_DEFAULT_UART 1)
set(PICO_DEFAULT_UART_BAUD_RATE 115200)

# Enable USB CDC as secondary stdio (companion link + debug)
set(PICO_STDIO_USB 1)
set(PICO_STDIO_UART 1)

# ── Firmware source files ─────────────────────────────────────────────────────
set(FW_SOURCES
    src/ghostwisp_boot.c
)

set(FW_HEADERS
    include/ghostwisp_pins.h
    include/ghostwisp_boot.h
)

# ── Create executable ─────────────────────────────────────────────────────────
pico_add_executable(ghostwisp ${FW_SOURCES})

# Add include path for project headers
target_include_directories(ghostwisp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(ghostwisp PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion
    -fno-strict-aliasing
    -fstack-usage
    -Wstack-usage=4096
    $<$<CONFIG:Release>:-Os>
    $<$<CONFIG:Debug>:-Og -g3>
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(ghostwisp PRIVATE
    pico_stdlib
    hardware_watchdog
    hardware_gpio
    hardware_spi
    hardware_i2c
    hardware_adc
    hardware_flash
    hardware_irq
    pico_bootrom
)

# ── Generate UF2, HEX, BIN, MAP outputs ───────────────────────────────────────
pico_add_extra_outputs(ghostwisp)

# ── Generate link map file for debug ─────────────────────────────────────────
target_link_options(ghostwisp PRIVATE -Wl,-Map=ghostwisp.map)

# ── Custom target: flash via OpenOCD ──────────────────────────────────────────
add_custom_target(flash-openocd
    COMMAND openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg
        -c "program ${CMAKE_BINARY_DIR}/ghostwisp.elf verify reset exit"
    DEPENDS ghostwisp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flashing ghostwisp.elf via OpenOCD/SWD"
)

# ── Custom target: flash via picotool ─────────────────────────────────────────
add_custom_target(flash-picotool
    COMMAND picotool load ${CMAKE_BINARY_DIR}/ghostwisp.uf2
    DEPENDS ghostwisp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flashing ghostwisp.uf2 via picotool (BOOTSEL mode)"
)

# ── Custom target: size report ────────────────────────────────────────────────
add_custom_target(size
    COMMAND ${CMAKE_SIZE_UTIL} ${CMAKE_BINARY_DIR}/ghostwisp.elf
    DEPENDS ghostwisp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Firmware size report"
)

# ── Print configuration summary ──────────────────────────────────────────────
message(STATUS "")
message(STATUS "==========================================================")
message(STATUS "  GhostWisp RP2350B Firmware Build Configuration")
message(STATUS "  Project Little Spectre — jayis1")
message(STATUS "==========================================================")
message(STATUS "  Platform:          ${PICO_PLATFORM}")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler:        ${CMAKE_C_COMPILER}")
message(STATUS "  Pico SDK path:     ${PICO_SDK_PATH}")
message(STATUS "  UART baud:         ${PICO_DEFAULT_UART_BAUD_RATE}")
message(STATUS "  Sources:           ${FW_SOURCES}")
message(STATUS "")
