# RP2350B Firmware Include - Board Pin Definitions
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: MIT

# SPI0 Slave (Bridge to RK3576)
PIN_SPI0_RX       = 16
PIN_SPI0_CSN      = 17
PIN_SPI0_SCK      = 18
PIN_SPI0_TX       = 19

# Interrupt and Control
PIN_INT_REQ       = 20
PIN_HOST_RDY       = 21
PIN_MCU_RUN       = 24

# PE42422 Antenna Switch
PIN_ANT_SEL0      = 2
PIN_ANT_SEL1      = 3

# SDR Control (LMS7002M)
PIN_SDR_SPI_SCK   = 27
PIN_SDR_SPI_TX    = 28
PIN_SDR_SPI_RX    = 29
PIN_SDR_SPI_CSN   = 30
PIN_SDR_RESET      = 31
PIN_SDR_GPIO0      = 32
PIN_SDR_GPIO1      = 33
PIN_SDR_LNA_EN    = 34

# CC1101 Sub-GHz
PIN_CC_SPI_SCK    = 8
PIN_CC_SPI_TX     = 9
PIN_CC_SPI_RX    = 12
PIN_CC_SPI_CSN   = 10
PIN_CC_GDO0      = 13
PIN_CC_GDO2      = 14

# ST25R3916 NFC
PIN_NFC_SPI_SCK  = 40
PIN_NFC_SPI_TX   = 41
PIN_NFC_SPI_RX   = 42
PIN_NFC_SPI_CSN  = 43
PIN_NFC_IRQ      = 44

# I2C (Secondary NFC Control)
PIN_I2C_SDA      = 46
PIN_I2C_SCL      = 47

# ADC
PIN_ADC_VBAT      = 0
PIN_ADC_TEMP       = 4

# UART0 Debug
PIN_UART_TX       = 0
PIN_UART_RX       = 1