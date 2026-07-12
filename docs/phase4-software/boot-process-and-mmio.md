<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# PHASE 4: Foundational Software Stack & Implementation

**Device:** GhostBlade  
**Codename:** Project NullSpectre  
**Date:** 2026-06-14  
**Revision:** 1.0  

---

## 1. Multi-Stage Boot Process

### 1.1 Stage 1: Internal ROM Bootloader

The RK3576 contains a mask ROM bootloader (24KB) that executes at address 0x00000000 upon power-on reset. This stage:

1. **Initializes minimal clock tree:** Sets ARM PLL to 600 MHz for A53 boot core (core 0), disables all other cores
2. **Loads SPL from eMMC boot0 partition:** Reads 256KB from eMMC boot partition 0 at offset 0x0000
3. **Verifies RSA-2048 signature:** Checks the SPL image against a hash stored in eFUSE. If verification fails, falls back to USB OTG recovery mode (FEL mode)
4. **Jumps to SPL entry point:** Transfers execution to 0x00000000 in SRAM (0x00080000 physical)

The ROM cannot be modified. It is hardcoded in silicon.

### 1.2 Stage 2: U-Boot SPL (Secondary Program Loader)

The SPL is a minimal U-Boot build (~192KB) that runs from internal SRAM. Its responsibilities:

```
U-Boot SPL Entry (0x00080000)
    │
    ├── 1. Initialize UART0 for debug console (115200 8N1)
    │
    ├── 2. Configure PMIC (RK817):
    │      - Enable BUCK1 (0.9V) → VDD_CORE
    │      - Enable BUCK2 (1.8V) → VDD_LOGIC
    │      - Enable BUCK3 (1.1V) → VDD_DDR
    │      - Enable BUCK4 (3.3V) → VDD_3V3
    │      - Enable LDO1 (1.2V) → VDD_1V2_SDR
    │      - Enable LDO2 (1.8V) → VDD_1V8_SDR
    │
    ├── 3. Initialize LPDDR5:
    │      - Configure DDR PHY training mode
    │      - Run write leveling (CA bus delay calibration)
    │      - Run read leveling (DQ/DQS delay calibration)
    │      - Run DRAM retention test (8 passes)
    │      - Set LPDDR5 to 3200 MT/s mode
    │
    ├── 4. Initialize eMMC:
    │      - Set eMMC to HS400 Enhanced Strobe mode
    │      - Read boot1 partition: full U-Boot image (2MB)
    │      - Verify SHA-256 hash of full U-Boot
    │
    ├── 5. Load full U-Boot:
    │      - Copy U-Boot to LPDDR5 at 0x00200000
    │      - Copy device tree blob (DTB) to 0x08300000
    │
    └── 6. Jump to U-Boot proper
           - Pass DTB address in X0 register
           - Jump to 0x00200000
```

#### SPL LPDDR5 Initialization Sequence

The LPDDR5 training sequence is critical. The RK3576 DDR controller requires precise timing calibration:

```c
/* Pseudocode for SPL DDR training — actual implementation in rk3576_ddr_init.c */

int ddr5_training(void) {
    /* Step 1: Assert DRAM reset */
    mmio_write32(DDR_CTRL_BASE + DDR_SWCTL, 0x0);
    mdelay(10);
    mmio_write32(DDR_CTRL_BASE + DDR_SWCTL, DDR_SWCTL_RELEASE);
    mdelay(10);

    /* Step 2: Issue MRW (Mode Register Write) sequence */
    /* MR12 = 0x34 (read DBI on, CRC on) */
    ddr5_mrw(0, 12, 0x34);
    /* MR13 = 0x03 (read PBRST, write PBRST) */
    ddr5_mrw(0, 13, 0x03);
    /* MR1 = 0x05 (BL=16, CRC, read DBI) */
    ddr5_mrw(0, 1, 0x05);
    /* MR2 = 0x13 (3200 MT/s, nWR=26) */
    ddr5_mrw(0, 2, 0x13);
    /* MR3 = 0x04 (tRCD=18, tRP=18) */
    ddr5_mrw(0, 3, 0x04);

    /* Step 3: Write leveling */
    ddr5_write_leveling();

    /* Step 4: Read leveling (gate training) */
    ddr5_read_leveling();

    /* Step 5: Run 8-pass memory test */
    for (int pass = 0; pass < 8; pass++) {
        if (ddr5_memory_test() != 0) {
            printf("DDR5 training FAIL on pass %d\n", pass);
            return -1;
        }
    }

    /* Step 6: Enable MRW monitoring */
    mmio_write32(DDR_CTRL_BASE + DDR_MRCTRL0, DDR_MRCTRL0_EN);

    return 0;
}
```

### 1.3 Stage 3: U-Boot Proper

Full U-Boot (2MB) runs from LPDDR5 and provides:

- **eMMC boot:** Reads Linux kernel Image.gz and initramfs from eMMC rootfs partition
- **USB recovery:** If VOL+ is held during boot, enters USB OTG recovery mode for flashing
- **Network boot:** If μSD card contains a boot.scr, loads kernel from network (TFTP)
- **Device tree fixups:** Modifies the DTB at runtime based on board revision and SKU
- **RP2350B release:** De-asserts MCU_RESET GPIO, allowing the RP2350B to boot from its flash

### 1.4 Stage 4: Linux Kernel

The Linux kernel (arm64, defconfig: `apex_one_defconfig`) initializes:

1. **Root filesystem:** eMMC ext4 rootfs with overlayfs for persistent customization
2. **Device drivers:** 
   - `apex_bridge` — SPI character device driver for RK3576 ↔ RP2350B communication
   - `apex_sdr` — V4L2 subdevice driver for LMS7002M (repurposes MIPI-CSI-2 framework)
   - `apex_nfc` — NFC subsystem driver for ST25R3916
   - `mt7922` — MediaTek mt76 driver (Wi-Fi 6E monitor mode + packet injection)
   - `cc1101` — SPI subdevice driver for CC1101 sub-GHz radio
3. **DMA ring buffers:** Two pre-allocated 16MB DMA-coherent buffers in LPDDR5 for SDR IQ capture (ping-pong)
4. **NPU runtime:** Rockchip RKNPU driver loads model via mmap()

### 1.5 Stage 5: Userspace

systemd boot targets:
- `multi-user.target` (default)
- `apex-sdr.target` (activates SDR services)
- `apex-pentest.target` (activates full pentest suite)

Key services:
- `apex-bridge.service` — Opens `/dev/apex_bridge0` and manages SPI protocol
- `apex-sdr.service` — Configures LMS7002M via MCU, manages DMA buffers
- `apex-nfc.service` — NFC polling daemon
- `NetworkManager` — Manages Wi-Fi 6E interfaces, monitor mode
- `apex-web-ui.service` — Local web interface on port 443 (self-signed TLS)

---

## 2. Memory-Mapped I/O (MMIO) Register Definitions

### 2.1 RK3576 SPI0 Controller Registers (apex_bridge)

The SPI0 controller on the RK3576 is memory-mapped at base address 0xFE610000 (based on RK3576 TRM).

```c
/* firmware/rp2350b/include/apex_bridge_regs.h — or software/linux-drivers/include/ */

#ifndef APEX_BRIDGE_REGS_H
#define APEX_BRIDGE_REGS_H

#include <linux/types.h>
#include <linux/io.h>

/*
 * RK3576 SPI0 Controller Register Map
 * Base address: 0xFE610000
 * Reference: Rockchip RK3576 TRM, Section 28 - SPI Controller
 */

#define RK3576_SPI0_BASE        0xFE610000

/* Control Register 0 (CR0) - Offset 0x0000 */
#define SPI_CR0                  0x0000
#define SPI_CR0_CLK_DIV_MASK     GENMASK(7, 0)
#define SPI_CR0_CLK_DIV_SHIFT    0
#define SPI_CR0_SPH              BIT(8)    /* Clock Phase (CPHA) */
#define SPI_CR0_SPO              BIT(9)    /* Clock Polarity (CPOL) */
#define SPI_CR0_CSM_KEEP         BIT(10)   /* CS keep after transfer */
#define SPI_CR0_CSM_1            BIT(11)   /* CS minimum inactive time: 1 SPI clk */
#define SPI_CR0_CSM_SHIFT        11
#define SPI_CR0_CSM_MASK         GENMASK(12, 11)
#define SPI_CR0_DFS_8BIT         (0x0 << 14) /* Data frame size: 8 bit */
#define SPI_CR0_DFS_16BIT        (0x1 << 14) /* Data frame size: 16 bit */
#define SPI_CR0_DFS_32BIT        (0x2 << 14) /* Data frame size: 32 bit */
#define SPI_CR0_DFS_MASK         GENMASK(15, 14)
#define SPI_CR0_BHT_8BIT         BIT(16)   /* Byte-hold time for 8-bit frames */
#define SPI_CR0_HT_SHIFT         16
#define SPI_CR0_XFM_TX           (0x0 << 18) /* Transfer mode: TX only */
#define SPI_CR0_XFM_RX           (0x1 << 18) /* Transfer mode: RX only */
#define SPI_CR0_XFM_TXRX         (0x2 << 18) /* Transfer mode: TX+RX (duplex) */
#define SPI_CR0_XFM_MASK         GENMASK(19, 18)
#define SPI_CR0_OPM_MASTER       BIT(20)   /* Master mode */
#define SPI_CR0_OPM_SLAVE        (0 << 20) /* Slave mode */
#define SPI_CR0_MBM_DMA          BIT(21)   /* DMA-based multi-bit mode */
#define SPI_CR0_RSD_1CLK         (0x0 << 22) /* Read stall delay: 1 SPI clk */
#define SPI_CR0_RSD_MASK         GENMASK(23, 22)
#define SPI_CR0_SSD_HALF         (0x0 << 24) /* SS polarity: active low */
#define SPI_CR0_SSD_HIGH         (0x1 << 24) /* SS polarity: active high */
#define SPI_CR0_SSD_MASK         BIT(24)
#define SPI_CR0_SSE_ENABLE       BIT(25)   /* SPI enable */
#define SPI_CR0_BEN_LSB          (0 << 26) /* Bit order: LSB first */
#define SPI_CR0_BEN_MSB          BIT(26)   /* Bit order: MSB first */

/* Control Register 1 (CR1) - Offset 0x0004 */
#define SPI_CR1                  0x0004
#define SPI_CR1_NDF_MASK         GENMASK(15, 0) /* Number of data frames */
#define SPI_CR1_NDF_SHIFT        0

/* Slave Enable Register (SER) - Offset 0x0008 */
#define SPI_SER                  0x0008
#define SPI_SER_CS0              BIT(0)    /* Enable CS0 */
#define SPI_SER_CS1              BIT(1)    /* Enable CS1 */

/* Status Register (SR) - Offset 0x000C */
#define SPI_SR                   0x000C
#define SPI_SR_DCOL              BIT(0)    /* Data collision error */
#define SPI_SR_TXE               BIT(1)    /* Transmission error */
#define SPI_SR_RXO               BIT(2)    /* Receive overrun */
#define SPI_SR_TXO               BIT(3)    /* Transmit overrun */
#define SPI_SR_BUSY              BIT(4)    /* SPI busy */
#define SPI_SR_TFE               BIT(5)    /* TX FIFO empty */
#define SPI_SR_TFNF              BIT(6)    /* TX FIFO not full */
#define SPI_SR_RFNE              BIT(7)    /* RX FIFO not empty */
#define SPI_SR_RFF               BIT(8)    /* RX FIFO full */

/* Interrupt Mask Register (IMR) - Offset 0x0010 */
#define SPI_IMR                  0x0010
#define SPI_IMR_TXEIM            BIT(0)    /* TX empty interrupt mask */
#define SPI_IMR_TXOIM            BIT(1)    /* TX overrun interrupt mask */
#define SPI_IMR_RXUIM            BIT(2)    /* RX underrun interrupt mask */
#define SPI_IMR_RXOIM            BIT(3)    /* RX overrun interrupt mask */
#define SPI_IMR_MIM              BIT(4)    /* Multi-master contention mask */
#define SPI_IMR_RIM              BIT(5)    /* RX FIFO full interrupt mask */

/* Interrupt Status Register (ISR) - Offset 0x0014 */
#define SPI_ISR                  0x0014
#define SPI_ISR_TXEIS            BIT(0)    /* TX empty interrupt status */
#define SPI_ISR_TXOIS            BIT(1)    /* TX overrun interrupt status */
#define SPI_ISR_RXUIS            BIT(2)    /* RX underrun interrupt status */
#define SPI_ISR_RXOIS            BIT(3)    /* RX overrun interrupt status */
#define SPI_ISR_MIS              BIT(4)    /* Multi-master contention status */
#define SPI_ISR_RIS              BIT(5)    /* RX FIFO full interrupt status */

/* Data Register (DR) - Offset 0x0060 */
#define SPI_DR                   0x0060
#define SPI_DR_DATA_MASK         GENMASK(31, 0) /* Data frame */

/* TX FIFO Level Register (TXFLR) - Offset 0x0074 */
#define SPI_TXFLR                0x0074
#define SPI_TXFLR_MASK           GENMASK(7, 0)

/* RX FIFO Level Register (RXFLR) - Offset 0x0078 */
#define SPI_RXFLR                0x0078
#define SPI_RXFLR_MASK           GENMASK(7, 0)

/* TX FIFO Write Level (TXFWCR) - Offset 0x007C */
#define SPI_TXFWCR               0x007C

/* RX DMA Register (RXDMA) - Offset 0x0100 */
#define SPI_RXDMA                0x0100
#define SPI_RXDMA_EN             BIT(0)    /* RX DMA enable */
#define SPI_RXDMA_DIS            (0 << 0)  /* RX DMA disable */

/* TX DMA Register (TXDMA) - Offset 0x0104 */
#define SPI_TXDMA                0x0104
#define SPI_TXDMA_EN             BIT(0)    /* TX DMA enable */
#define SPI_TXDMA_DIS            (0 << 0)  /* TX DMA disable */

/* DMA Receive Data Address (DMARDLR) - Offset 0x0118 */
#define SPI_DMARDLR              0x0118

/* DMA Transmit Data Address (DMATDLR) - Offset 0x011C */
#define SPI_DMATDLR              0x011C

/*
 * GPIO Register Definitions for SPI Bridge
 * RK3576 GPIO1 Bank Base: 0xFD510000
 */
#define RK3576_GPIO1_BASE        0xFD510000

/* GPIO1 Port A registers */
#define GPIO1_SWPORTA_DR         0x0000    /* Data register */
#define GPIO1_SWPORTA_DDR        0x0004    /* Data direction register */

/* GPIO1 Port B registers */
#define GPIO1_SWPORTB_DR         0x000C
#define GPIO1_SWPORTB_DDR        0x0010

/* GPIO1 interrupt registers */
#define GPIO1_INTEN               0x0030    /* Interrupt enable */
#define GPIO1_INTMASK             0x0034    /* Interrupt mask */
#define GPIO1_INTTYPE             0x0038    /* Interrupt type (edge/level) */
#define GPIO1_INT_POLARITY        0x003C    /* Interrupt polarity */
#define GPIO1_INT_STATUS          0x0040    /* Interrupt status */
#define GPIO1_INT_RAW_STATUS      0x0044    /* Raw interrupt status */
#define GPIO1_DEBOUNCE            0x0048    /* Debounce enable */
#define GPIO1_EOI                 0x004C    /* End of interrupt */
#define GPIO1_PORTA_EOI           0x0050    /* Port A end of interrupt */

/*
 * Specific GPIO pins for the SPI bridge
 */
#define APEX_SPI0_CS_GPIO        1  /* GPIO1_A3 — SPI0 chip select */
#define APEX_SPI0_SCK_GPIO       2  /* GPIO1_A2 — SPI0 clock */
#define APEX_SPI0_MOSI_GPIO      0  /* GPIO1_A0 — SPI0 MOSI */
#define APEX_SPI0_MISO_GPIO      1  /* GPIO1_A1 — SPI0 MISO */
#define APEX_INT_REQ_GPIO        8  /* GPIO1_B0 — MCU interrupt request */
#define APEX_HOST_READY_GPIO     9  /* GPIO1_B1 — Host ready signal */
#define APEX_MCU_RESET_GPIO     10  /* GPIO1_B2 — MCU reset (active-low) */

/*
 * GPIO bit positions within GPIO1 bank
 * Port A: bits [15:0] = A0-A15
 * Port B: bits [15:0] = B0-B15
 */
#define APEX_INT_REQ_BIT          BIT(8)    /* GPIO1_B0 */
#define APEX_HOST_READY_BIT       BIT(9)    /* GPIO1_B1 */
#define APEX_MCU_RESET_BIT       BIT(10)    /* GPIO1_B2 */

/*
 * SPI Bridge Protocol Constants
 */
#define APEX_SPI_SYNC_BYTE        0xAA
#define APEX_SPI_HDR_SIZE         16
#define APEX_SPI_MAX_PAYLOAD      4092
#define APEX_SPI_FRAME_SIZE_MAX   (APEX_SPI_HDR_SIZE + APEX_SPI_MAX_PAYLOAD + 4) /* + CRC32 */

/* Command opcodes */
#define APEX_CMD_NOP              0xFF
#define APEX_CMD_SDR_TUNE         0x01
#define APEX_CMD_SDR_STREAM       0x02
#define APEX_CMD_ANT_SELECT       0x03
#define APEX_CMD_CC1101_CFG       0x04
#define APEX_CMD_NFC_TRANSACT     0x05
#define APEX_CMD_TELEMETRY        0x81
#define APEX_CMD_SDR_IQ_CHUNK     0x82

/* Telemetry payload structure (16 bytes) */
struct apex_telemetry {
    __le16 rssi_dbm_x10;      /* SDR RSSI in dBm × 10 */
    __le16 temp_c_x10;         /* MCU temperature in °C × 10 */
    __le16 vbat_mv;            /* Battery voltage in mV */
    __le16 cc1101_rssi_dbm_x10;/* CC1101 RSSI in dBm × 10 */
    __le16 nfc_field_mv;       /* NFC field strength in mV */
    __le16 flags;              /* Status flags bitmap */
    __le32 uptime_ms;          /* MCU uptime in milliseconds */
};

/* Telemetry flags */
#define APEX_FLAG_SDR_RX_ACTIVE   BIT(0)
#define APEX_FLAG_SDR_TX_ACTIVE   BIT(1)
#define APEX_FLAG_CC1101_RX       BIT(2)
#define APEX_FLAG_CC1101_TX       BIT(3)
#define APEX_FLAG_NFC_ACTIVE      BIT(4)
#define APEX_FLAG_NFC_TAG_PRESENT BIT(5)
#define APEX_FLAG_OVERTEMP        BIT(6)
#define APEX_FLAG_LOW_BATTERY     BIT(7)
#define APEX_FLAG_SPI_ERR         BIT(8)
#define APEX_FLAG_DMA_ERR         BIT(9)

/* SDR tune command payload (8 bytes) */
struct apex_sdr_tune_cmd {
    __le32 freq_hz;            /* Tuning frequency in Hz */
    __le16 bw_hz;              /* Bandwidth in Hz (×1000, e.g., 20000 = 20 MHz) */
    __le16 gain_db_x10;        /* LNA gain in dB × 10 */
};

#endif /* APEX_BRIDGE_REGS_H */