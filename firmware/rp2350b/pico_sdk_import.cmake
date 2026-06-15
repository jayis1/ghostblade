# pico_sdk_import.cmake — Import the Raspberry Pi Pico SDK
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: MIT
#
# This file is a minimal shim that locates the Pico SDK. In a typical
# Pico SDK setup, this file is either:
#   1. Copied from $PICO_SDK_PATH/external/pico_sdk_import.cmake, or
#   2. Included via -DPICO_SDK_PATH= on the cmake command line.
#
# The CI workflow uses option 2: cmake .. -DPICO_SDK_PATH=/opt/pico-sdk
# For local development, set PICO_SDK_PATH in your environment or pass
# it to cmake.

if (NOT PICO_SDK_PATH)
    # Try environment variable
    if (DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
        message(STATUS "PICO_SDK_PATH set from environment: ${PICO_SDK_PATH}")
    else()
        # Try common locations
        if (EXISTS "/opt/pico-sdk")
            set(PICO_SDK_PATH "/opt/pico-sdk")
        elseif (EXISTS "${CMAKE_SOURCE_DIR}/../pico-sdk")
            set(PICO_SDK_PATH "${CMAKE_SOURCE_DIR}/../pico-sdk")
        elseif (EXISTS "${CMAKE_SOURCE_DIR}/pico-sdk")
            set(PICO_SDK_PATH "${CMAKE_SOURCE_DIR}/pico-sdk")
        else()
            message(FATAL_ERROR
                "PICO_SDK_PATH not set. Please set it to the root of the Pico SDK:\n"
                "  cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..\n"
                "Or set the PICO_SDK_PATH environment variable."
            )
        endif()
    endif()
endif()

# Validate the Pico SDK path
if (NOT EXISTS "${PICO_SDK_PATH}/pico_sdk.cmake")
    message(FATAL_ERROR
        "Pico SDK not found at ${PICO_SDK_PATH}\n"
        "Expected ${PICO_SDK_PATH}/pico_sdk.cmake to exist.\n"
        "Set PICO_SDK_PATH to the root of the Pico SDK checkout."
    )
endif()

# Include the Pico SDK
include(${PICO_SDK_PATH}/pico_sdk.cmake)