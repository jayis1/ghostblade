<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade SPI Bridge Driver — sysfs Attributes

The `apex_bridge` kernel driver exposes telemetry, device status, and DMA
engine statistics through sysfs attributes under
`/sys/class/apex/apex_bridge0/`. This document describes each attribute,
its format, and valid ranges.

## Telemetry Attributes

These attributes are read from the RP2350B MCU via the SPI bridge protocol.
The MCU updates telemetry data every 1 second and sends it to the host
every 100 ms (when changes are detected).

### `rssi_dbm_x10`

SDR RSSI (Received Signal Strength Indicator) in dBm × 10.

- **Type:** Read-only integer
- **Range:** -1200 to 0 (−120.0 dBm to 0.0 dBm)
- **Unit:** 0.1 dBm
- **Source:** LMS7002M RSSI register, read by RP2350B firmware
- **Example:** `-740` means −74.0 dBm

### `temp_c_x10`

MCU die temperature in °C × 10.

- **Type:** Read-only integer
- **Range:** −400 to 850 (−40.0 °C to 85.0 °C)
- **Unit:** 0.1 °C
- **Source:** RP2350B internal temperature sensor (ADC channel 4)
- **Example:** `275` means 27.5 °C

### `vbat_mv`

Battery voltage in millivolts.

- **Type:** Read-only integer
- **Range:** 2800 to 4200 (typical Li-Po range)
- **Unit:** mV
- **Source:** RP2350B ADC channel 0, voltage-divided from VBAT
- **Example:** `3850` means 3.850 V

Use `apex_battery_percent()` from libapex to convert to an estimated
charge percentage:
- ≥ 4200 mV → 100%
- 3700–4200 mV → 50–100% (linear)
- 3300–3700 mV → 10–50%
- 3000–3300 mV → 0–10%
- ≤ 3000 mV → 0% (shutdown threshold)

### `cc1101_rssi_x10`

CC1101 sub-GHz radio RSSI in dBm × 10.

- **Type:** Read-only integer
- **Range:** -1200 to 0 (−120.0 dBm to 0.0 dBm)
- **Unit:** 0.1 dBm
- **Source:** CC1101 RSSI register, read by RP2350B via SPI1
- **Example:** `-550` means −55.0 dBm

### `nfc_field_mv`

NFC 13.56 MHz field strength in millivolts.

- **Type:** Read-only integer
- **Range:** 0 to 5000
- **Unit:** mV
- **Source:** ST25R3916 measured field strength register
- **Example:** `2500` means 2.5 V field

### `mcu_flags`

MCU status flags bitmap.

- **Type:** Read-only hexadecimal integer
- **Bit definitions:**

| Bit | Name              | Meaning                          |
|-----|-------------------|----------------------------------|
| 0   | SDR_RX_ACTIVE     | SDR receiver is active           |
| 1   | SDR_TX_ACTIVE     | SDR transmitter is active        |
| 2   | CC1101_RX         | CC1101 is receiving              |
| 3   | CC1101_TX         | CC1101 is transmitting           |
| 4   | NFC_ACTIVE        | NFC field is on                 |
| 5   | NFC_TAG_PRESENT   | An NFC tag is in the field       |
| 6   | OVERTEMP          | MCU temperature > 85 °C          |
| 7   | LOW_BATTERY       | Battery voltage < 3.3 V          |
| 8   | SPI_ERR           | SPI protocol error detected      |
| 9   | DMA_ERR           | DMA transfer error detected      |

- **Example:** `0x0025` means SDR_RX_ACTIVE, CC1101_RX, NFC_ACTIVE

### `uptime_ms`

MCU uptime in milliseconds since last reset.

- **Type:** Read-only integer
- **Range:** 0 to 4294967295 (wraps at ~49.7 days)
- **Unit:** ms
- **Source:** RP2350B hardware timer, incremented every 1 ms
- **Example:** `12345` means 12.345 seconds since boot

## Driver Status Attributes

### `driver_status`

Kernel driver status flags.

- **Type:** Read-only hexadecimal integer
- **Bit definitions:**

| Bit | Name          | Meaning                            |
|-----|---------------|------------------------------------|
| 0   | MCU_READY     | RP2350B has reported ready          |
| 1   | MCU_IN_RESET  | RP2350B is held in reset           |
| 2   | SPI_ERROR     | SPI communication error detected    |
| 3   | SG_RUNNING    | Scatter-gather DMA engine active   |
| 4   | SG_ERROR      | Scatter-gather DMA error           |

- **Example:** `0x01` means MCU is ready, no errors

### `spi_errors`

Count of SPI communication errors since driver load.

- **Type:** Read-only integer
- **Range:** 0 to 2³² − 1
- **Includes:** CRC mismatches, sync byte errors, frame length errors,
  SPI bus timeouts
- **Example:** `0` means no SPI errors

### `rx_fifo_count`

Number of bytes currently in the RX FIFO (kernel → userspace).

- **Type:** Read-only integer
- **Range:** 0 to 65536 (64 KiB FIFO)
- **Unit:** bytes
- **Example:** `4096` means 4 KiB of data pending in the RX FIFO

### `tx_fifo_count`

Number of bytes currently in the TX FIFO (userspace → kernel → MCU).

- **Type:** Read-only integer
- **Range:** 0 to 65536 (64 KiB FIFO)
- **Unit:** bytes
- **Example:** `0` means the TX FIFO is empty

## Scatter-Gather DMA Attributes

These attributes report the state and statistics of the DMA scatter-gather
engine used for high-throughput SDR IQ data streaming. The SG engine is
started via `APEX_IOC_SG_START` ioctl and stopped via `APEX_IOC_SG_STOP`.

### `sg_state`

Current state of the scatter-gather DMA engine.

- **Type:** Read-only string
- **Values:** `idle`, `running`, `error`
- **Example:** `idle` means the SG engine is not streaming

### `sg_total_bytes`

Total bytes transferred by the SG engine since the last `SG_START`.

- **Type:** Read-only integer
- **Range:** 0 to 2⁶⁴ − 1
- **Unit:** bytes
- **Example:** `8388608` means 8 MiB transferred

### `sg_overruns`

Number of DMA buffer overrun events since `SG_START`.

- **Type:** Read-only integer
- **Range:** 0 to 2³² − 1
- **Meaning:** An overrun occurs when the DMA engine fills a buffer faster
  than userspace can consume it. The oldest buffer data is lost.
- **Example:** `0` means no overruns

### `sg_errors`

Number of transfer errors (CRC failures, SPI timeouts) since `SG_START`.

- **Type:** Read-only integer
- **Range:** 0 to 2³² − 1
- **Example:** `0` means no errors

### `sg_frames_rx`

Number of valid SPI protocol frames received by the SG engine.

- **Type:** Read-only integer
- **Range:** 0 to 2³² − 1
- **Example:** `1024` means 1024 IQ data frames received

### `sg_buf_count`

Number of scatter-gather buffers currently allocated.

- **Type:** Read-only integer
- **Range:** 0 to 64
- **Example:** `8` means 8 DMA buffers are allocated

### `sg_buf_size`

Size of each scatter-gather buffer in bytes.

- **Type:** Read-only integer
- **Range:** 4096 to 262144 (4 KiB to 256 KiB)
- **Unit:** bytes
- **Example:** `32768` means each buffer is 32 KiB

## Usage Examples

### Reading telemetry from shell

```bash
# Read battery voltage
cat /sys/class/apex/apex_bridge0/vbat_mv
# Output: 3850

# Read MCU temperature
cat /sys/class/apex/apex_bridge0/temp_c_x10
# Output: 275

# Calculate temperature in °C
temp_x10=$(cat /sys/class/apex/apex_bridge0/temp_c_x10)
echo "Temperature: $(echo "scale=1; $temp_x10 / 10" | bc) °C"

# Read SDR RSSI
cat /sys/class/apex/apex_bridge0/rssi_dbm_x10
# Output: -740

# Read MCU flags (hex)
cat /sys/class/apex/apex_bridge0/mcu_flags
# Output: 0x0025

# Read SG DMA engine state
cat /sys/class/apex/apex_bridge0/sg_state
# Output: idle

# Read SG transfer statistics
cat /sys/class/apex/apex_bridge0/sg_total_bytes
cat /sys/class/apex/apex_bridge0/sg_frames_rx
cat /sys/class/apex/apex_bridge0/sg_overruns
```

### Monitoring telemetry with a script

```bash
#!/bin/bash
# Monitor GhostBlade telemetry in real time
DEVICE="/sys/class/apex/apex_bridge0"

while true; do
    vbat=$(cat ${DEVICE}/vbat_mv 2>/dev/null || echo "N/A")
    temp=$(cat ${DEVICE}/temp_c_x10 2>/dev/null || echo "N/A")
    rssi=$(cat ${DEVICE}/rssi_dbm_x10 2>/dev/null || echo "N/A")
    flags=$(cat ${DEVICE}/mcu_flags 2>/dev/null || echo "N/A")
    uptime=$(cat ${DEVICE}/uptime_ms 2>/dev/null || echo "N/A")

    echo "$(date +%H:%M:%S) | VBAT=${vbat}mV | TEMP=${temp} | RSSI=${rssi} | FLAGS=${flags} | UP=${uptime}ms"
    sleep 1
done
```

### Using libapex from C

```c
#include <libapex.h>
#include <stdio.h>

int main(void) {
    apex_handle_t dev = apex_open(NULL);
    if (!dev) {
        fprintf(stderr, "Failed to open device\n");
        return 1;
    }

    apex_telemetry_t telem;
    if (apex_get_telemetry(dev, &telem) == APEX_OK) {
        printf("Battery: %u mV (%u%%)\n",
               telem.vbat_mv, apex_battery_percent(telem.vbat_mv));
        printf("Temp: %.1f C\n", telem.temp_c_x10 / 10.0);
        printf("RSSI: %.1f dBm\n", telem.rssi_dbm_x10 / 10.0);
        printf("Uptime: %u ms\n", telem.uptime_ms);
    }

    apex_close(dev);
    return 0;
}
```

### Using pyapex from Python

```python
import pyapex

dev = pyapex.open()
telem = dev.get_telemetry()

print(f"Battery: {telem['vbat_mv']} mV")
print(f"Temp: {telem['temp_c_x10'] / 10.0:.1f} C")
print(f"RSSI: {telem['rssi_dbm_x10'] / 10.0:.1f} dBm")
print(f"Uptime: {telem['uptime_ms']} ms")

dev.close()
```

## Troubleshooting

| Symptom | Likely Cause | Check |
|---------|-------------|-------|
| All telemetry reads return 0 | MCU not responding | `driver_status` bit 0 should be set |
| `rssi_dbm_x10` always 0 | SDR not initialized | Send SDR_TUNE command via ioctl |
| `vbat_mv` returns 3000 | Low battery / ADC error | Check battery connection |
| `mcu_flags` bit 6 set | Overtemperature (>85°C) | Reduce workload, check cooling |
| `mcu_flags` bit 7 set | Low battery (<3.3V) | Charge battery immediately |
| `mcu_flags` bit 8 set | SPI error | Check `spi_errors` count |
| `sg_state` is `error` | DMA transfer failure | Check `sg_errors` and `sg_overruns` |
| `spi_errors` increasing | Bad SPI connection | Check SPI0 wiring, reduce clock speed |