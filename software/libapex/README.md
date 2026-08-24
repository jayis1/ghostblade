# libapex — Userspace C Library for GhostBlade

A C library and Python binding for interacting with the GhostBlade pentesting
device through the kernel SPI bridge driver.

## Building

### C Library

```bash
cd software/libapex
make
sudo make install
```

### Python Extension

```bash
cd software/libapex
python3 -m pip install .
# Or for development:
python3 setup.py build_ext --inplace
```

The compiled extension module is named `pyapex`. For compatibility with older
scripts, the package also installs a small `apex` shim that re-exports the same
API.

## C API Usage

```c
#include <libapex.h>

apex_handle_t dev = apex_open(NULL);  // Opens /dev/apex_bridge0
if (!dev) {
    fprintf(stderr, "Failed to open device\n");
    return 1;
}

// Tune SDR to 868 MHz, 20 MHz BW, 30 dB gain
apex_sdr_tune(dev, 868000000, 20000, 30.0f);

// Read telemetry
apex_telemetry_t telem;
apex_get_telemetry(dev, &telem);
printf("Battery: %u mV (%d%%)\n", telem.vbat_mv, apex_battery_percent(telem.vbat_mv));
printf("Temp: %.1f C\n", telem.temp_c_x10 / 10.0);
printf("SDR RSSI: %.1f dBm\n", telem.rssi_dbm_x10 / 10.0);

apex_close(dev);
```

## Python API Usage

```python
import pyapex

dev = pyapex.ApexBridge()  # Opens /dev/apex_bridge0

# Tune SDR
dev.sdr_tune(868_000_000, 20000, 30.0)

# Start streaming
dev.sdr_stream_start()

# Read telemetry
telem = dev.get_telemetry()
print(f"Battery: {telem['vbat_mv']} mV")
print(f"Temp: {telem['temp_c_x10'] / 10.0:.1f} C")

# CC1101 sub-GHz radio
dev.cc1101_set_channel(0)
dev.cc1101_set_power(0)  # 0 dBm

# NFC
dev.nfc_transact(cmd=0x26, flags=0x00, data=b'')

# Antenna selection
dev.ant_select(pyapex.ANT_SUBGHZ)

dev.close()
```

Compatibility import style:

```python
import apex

dev = apex.open()
print(dev.get_status())
dev.close()
```

## Linking

```bash
gcc -o my_app my_app.c -lapex
```

## License

GPL-2.0-or-later