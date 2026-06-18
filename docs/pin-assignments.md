# Pin Assignment Cross-Reference — GhostBlade

This document cross-references signal assignments across the three primary sources:
schematic netlist (`GhostBlade.mf`), device tree (`ghostblade-rk3576.dts`), and
firmware (`board_pins.h`).

When modifying any pin assignment, update **all three** sources to maintain consistency.

---

## 1. Inter-Processor Bridge (RK3576 ↔ RP2350B)

### SPI0 — Command/Response Bus (50 MHz, Mode 0)

| Signal | RK3576 GPIO | RP2350B Pin | Net Name | DTS Node | Firmware Macro |
|--------|-------------|-------------|-----------|----------|----------------|
| SPI_CLK | GPIO1_A2 | PIN_18 | NET_SPI_CLK | `spi0_pins` (CLK) | `PIN_SPI0_SCK` |
| SPI_MOSI | GPIO1_A0 | PIN_19 | NET_SPI_MOSI | `spi0_pins` (MOSI) | `PIN_SPI0_TX` |
| SPI_MISO | GPIO1_A1 | PIN_16 | NET_SPI_MISO | `spi0_pins` (MISO) | `PIN_SPI0_RX` |
| SPI_CS | GPIO1_A3 | PIN_17 | NET_SPI_CS | `spi0_pins` (CS) | `PIN_SPI0_CSN` |
| INT_REQ | GPIO1_B0 | PIN_20 | NET_INT_REQ | `bridge_gpio_pins` | `PIN_INT_REQ` |
| HOST_RDY | GPIO1_B1 | PIN_21 | NET_HOST_READY | `bridge_gpio_pins` | `PIN_HOST_RDY` |
| MCU_RESET | GPIO1_B2 | PIN_24 | NET_MCU_RESET | `bridge_gpio_pins` | `PIN_MCU_RUN` |

### I2C Bridge (RK3576 ↔ RP2350B)

| Signal | RK3576 Pin | RP2350B Pin | Net Name | DTS Node | Firmware Macro |
|--------|-----------|-------------|-----------|----------|----------------|
| I2C_SDA | I2C1_SDA | PIN_25 | NET_I2C1_SDA | `i2c1_pins` | — |
| I2C_SCL | I2C1_SCL | PIN_26 | NET_I2C1_SCL | `i2c1_pins` | — |

---

## 2. SDR Frontend (LMS7002M)

### SPI1 — SDR Control (50 MHz, Mode 0)

| Signal | RP2350B Pin | Net Name | Firmware Macro |
|--------|-------------|-----------|----------------|
| SDR_SPI_SCK | PIN_27 | NET_SDR_SPI_SCK | `PIN_SDR_SPI_SCK` |
| SDR_SPI_MOSI | PIN_28 | NET_SDR_SPI_MOSI | `PIN_SDR_SPI_TX` |
| SDR_SPI_MISO | PIN_29 | NET_SDR_SPI_MISO | `PIN_SDR_SPI_RX` |
| SDR_SPI_CSn | PIN_30 | NET_SDR_SPI_CS | `PIN_SDR_SPI_CSN` |
| SDR_RESET | PIN_31 | NET_SDR_RESET | `PIN_SDR_RESET` |
| SDR_GPIO0 (TX_EN) | PIN_32 | NET_SDR_GPIO0 | `PIN_SDR_GPIO0` |
| SDR_GPIO1 (RX_EN) | PIN_33 | NET_SDR_GPIO1 | `PIN_SDR_GPIO1` |
| SDR_LNA_EN | PIN_34 | NET_SDR_LNA_EN | `PIN_SDR_LNA_EN` |

### MIPI-CSI-2 — IQ Data Path (RK3576 ← LMS7002M)

| Signal | RK3576 Pin | Net Name | DTS Node |
|--------|-----------|-----------|----------|
| CSI_CLK_P/N | MIPI_CSI_CLK | NET_SDR_IQ_I | `mipi_csi_pins` |
| CSI_D0_P/N | MIPI_CSI_D0 | NET_SDR_IQ_Q | `mipi_csi_pins` |
| CSI_D1_P/N | MIPI_CSI_D1 | — | `mipi_csi_pins` |
| CSI_D2_P/N | MIPI_CSI_D2 | — | `mipi_csi_pins` |
| CSI_D3_P/N | MIPI_CSI_D3 | — | `mipi_csi_pins` |

---

## 3. Sub-GHz Radio (CC1101)

### SPI1 (shared bus) — CC1101 Control (10 MHz, Mode 0)

| Signal | RP2350B Pin | Net Name | Firmware Macro |
|--------|-------------|-----------|----------------|
| CC_SPI_SCK | PIN_8 | NET_CC_SPI_SCK | `PIN_CC_SPI_SCK` |
| CC_SPI_MOSI | PIN_9 | NET_CC_SPI_MOSI | `PIN_CC_SPI_TX` |
| CC_SPI_MISO | PIN_12 | NET_CC_SPI_MISO | `PIN_CC_SPI_RX` |
| CC_SPI_CSn | PIN_10 | NET_CC_SPI_CS | `PIN_CC_SPI_CSN` |
| CC_GDO0 | PIN_13 | NET_CC_GDO0 | `PIN_CC_GDO0` |
| CC_GDO2 | PIN_14 | NET_CC_GDO2 | `PIN_CC_GDO2` |

---

## 4. NFC Controller (ST25R3916)

### SPI2 — NFC Control (10 MHz, Mode 0)

| Signal | RP2350B Pin | Net Name | Firmware Macro |
|--------|-------------|-----------|----------------|
| NFC_SPI_SCK | PIN_40 | NET_NFC_SPI_SCK | `PIN_NFC_SPI_SCK` |
| NFC_SPI_MOSI | PIN_41 | NET_NFC_SPI_MOSI | `PIN_NFC_SPI_TX` |
| NFC_SPI_MISO | PIN_42 | NET_NFC_SPI_MISO | `PIN_NFC_SPI_RX` |
| NFC_SPI_CSn | PIN_43 | NET_NFC_SPI_CS | `PIN_NFC_SPI_CSN` |
| NFC_IRQ | PIN_44 | NET_NFC_IRQ | `PIN_NFC_IRQ` |

### I2C1 — NFC Secondary Control (400 kHz)

| Signal | RP2350B Pin | Net Name | Firmware Macro |
|--------|-------------|-----------|----------------|
| I2C_SDA | PIN_46 | NET_NFC_I2C_SDA | `PIN_I2C_SDA` |
| I2C_SCL | PIN_47 | NET_NFC_I2C_SCL | `PIN_I2C_SCL` |

---

## 5. Antenna Switch (PE42422)

| Signal | RP2350B Pin | Net Name | Firmware Macro | Selection |
|--------|-------------|-----------|----------------|-----------|
| ANT_SEL0 | PIN_2 | NET_ANT_SEL0 | `PIN_ANT_SEL0` | V1 bit 0 |
| ANT_SEL1 | PIN_3 | NET_ANT_SEL1 | `PIN_ANT_SEL1` | V2 bit 1 |

**Antenna path selection:**

| ANT_SEL1 | ANT_SEL0 | Path | Connected To |
|----------|----------|------|-------------|
| 0 | 0 | RF1 | SMA_ANT0 (MIMO TX) |
| 0 | 1 | RF2 | SMA_ANT1 (MIMO RX) |
| 1 | 0 | RF3 | u.FL (Sub-GHz via CC1101) |
| 1 | 1 | RF4 | 50Ω terminated |

---

## 6. ADC Channels (RP2350B)

| Signal | RP2350B Pin | Firmware Macro | Description |
|--------|-------------|----------------|-------------|
| ADC_VBAT | PIN_0 (ADC0) | `PIN_ADC_VBAT` | Battery voltage (divider) |
| ADC_TEMP | PIN_4 (ADC4) | `PIN_ADC_TEMP` | Internal die temperature |

---

## 7. Debug UART (RP2350B)

| Signal | RP2350B Pin | Firmware Macro | Description |
|--------|-------------|----------------|-------------|
| UART_TX | PIN_0 | `PIN_UART_TX` | Debug console TX (115200 8N1) |
| UART_RX | PIN_1 | `PIN_UART_RX` | Debug console RX (115200 8N1) |

> **Note:** PIN_0 is shared between `ADC_VBAT` (ADC function) and `UART_TX` (UART function).
> The RP2350B pin function multiplexing resolves this: ADC0 on GPIO0 uses a different
> function than UART0 TX. Verify pin muxing in `rp2350b_init.c` if both are enabled.

---

## 8. RK3576 Peripherals (DTS Only)

| Peripheral | DTS Node | Pins | Purpose |
|-----------|----------|------|---------|
| UART0 | `&uart0` | UART0_TX, UART0_RX | Debug console |
| eMMC 5.1 | `&sdhci` | EMMC_* | 32 GB boot storage |
| SD card | `&sdmmc0` | SDMMC0_* | μSD card slot |
| PCIe Gen3 x2 | `&pcie` | PCIE_* | M.2 NVMe |
| MIPI-DSI | `&dsi` | DSI_* | 6.4" IPS display |
| USB-C 3.2 | `&usb` | USB_CC* | Host/device + DP alt |
| I2C1 | `&i2c1` | I2C1_SDA, I2C1_SCL | RP2350B / NFC |
| GPIO keys | `&gpio_keys` | GPIO1_B4, GPIO1_B5 | BOOTSEL, USER |
| LEDs | `&gpio-leds` | GPIO1_B6, GPIO1_B7, GPIO1_C0 | STATUS, ACTIVITY, SDR_RX |
| Wi-Fi | `&sdio` | SDIO_* | MT7922 SDIO |

---

## Cross-Reference Checklist

When adding or modifying a signal, update these sources:

- [ ] **Schematic netlist** (`hardware/kicad/ghostblade.net`)
- [ ] **Manifest** (`GhostBlade.mf` — `[Schematic.Netlist.*]` sections)
- [ ] **Device tree** (`software/dts/ghostblade-rk3576.dts` or overlay)
- [ ] **Firmware pin definitions** (`firmware/rp2350b/include/board_pins.h`)
- [ ] **Kernel driver** (`software/linux-drivers/include/apex_bridge_regs.h` if RK3576 GPIO)
- [ ] **This document** (`docs/pin-assignments.md`)

---

*Last updated: 2026-06-18. Generated from GhostBlade.mf, board_pins.h, and ghostblade-rk3576.dts.*