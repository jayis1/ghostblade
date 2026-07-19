"""
pyapex.pyi — Type stubs for the pyapex Python extension module

Copyright (C) 2026 GhostBlade Project
SPDX-License-Identifier: GPL-2.0-or-later

This file provides type annotations and documentation for the pyapex
Python extension module, enabling IDE autocompletion, type checking,
and inline documentation for GhostBlade hardware control.

Usage:
    import pyapex
    dev = pyapex.ApexDevice('/dev/apex_bridge0')
    dev.sdr_tune(868e6, 20000, 30.0)
    telem = dev.get_telemetry()
    dev.close()
"""

from typing import Optional, Dict, Any

class ApexError(Exception):
    """Exception raised for GhostBlade device communication errors.

    Attributes:
        errno: Negative error code (APEX_ERR_* constant)
        msg: Human-readable error description
    """
    errno: int
    msg: str
    def __init__(self, errno: int, msg: str = "") -> None: ...

class ApexDevice:
    """GhostBlade hardware interface device.

    Opens a connection to the GhostBlade SPI bridge device and provides
    methods for SDR control, sub-GHz radio, NFC, and telemetry.

    The device supports exclusive access — only one process can open
    it at a time. Use close() to release the device when done.

    Example:
        dev = pyapex.ApexDevice('/dev/apex_bridge0')
        dev.sdr_tune(868e6, 20000, 30.0)
        dev.sdr_stream_start()
        iq_data = dev.sdr_read_iq(8192)
        dev.sdr_stream_stop()
        dev.close()

    Attributes:
        fd: Underlying file descriptor for the device node
    """

    fd: int

    def __init__(self, device_path: str = "/dev/apex_bridge0") -> None:
        """Open a connection to the GhostBlade device.

        Args:
            device_path: Path to the device node (default: /dev/apex_bridge0)

        Raises:
            ApexError: If the device cannot be opened
            FileNotFoundError: If the device path does not exist
            PermissionError: If the user lacks permissions for the device
        """
        ...

    def close(self) -> None:
        """Close the device connection and release resources.

        It is safe to call close() multiple times. After close(),
        no other methods should be called on this instance.
        """
        ...

    def sdr_tune(self, freq_hz: int, bw_khz: int, gain_db: float) -> None:
        """Tune the LMS7002M SDR to a frequency and bandwidth.

        Args:
            freq_hz: Center frequency in Hz (100000 = 100 kHz, 3800000000 = 3.8 GHz)
            bw_khz: Bandwidth in kHz (e.g., 20000 = 20 MHz)
            gain_db: LNA gain in dB (0.0 to 73.0)

        Raises:
            ApexError: If the SDR tune command fails
            ValueError: If frequency is out of range (100 kHz – 3.8 GHz)
        """
        ...

    def sdr_stream_start(self) -> None:
        """Start SDR IQ data streaming.

        After calling this, IQ samples can be read using sdr_read_iq().
        The SDR must be tuned (via sdr_tune()) before starting streaming.

        Raises:
            ApexError: If streaming is already active or cannot be started
        """
        ...

    def sdr_stream_stop(self) -> None:
        """Stop SDR IQ data streaming.

        Raises:
            ApexError: If streaming is not active or cannot be stopped
        """
        ...

    def sdr_read_iq(self, buf_len: int) -> bytes:
        """Read IQ samples from the SDR stream buffer.

        Each sample is 4 bytes: I(16-bit signed) + Q(16-bit signed),
        both in two's complement, little-endian.

        Args:
            buf_len: Maximum number of bytes to read. Should be a multiple of 4
                     for complete IQ samples. Common sizes: 4096, 8192, 32768.

        Returns:
            bytes: IQ sample data. Length may be less than buf_len if fewer
                   samples are available. Empty bytes if streaming is not active.

        Raises:
            ApexError: On communication error
        """
        ...

    def ant_select(self, ant: int) -> None:
        """Select the active antenna path.

        Args:
            ant: Antenna selection (0=MIMO_TX, 1=MIMO_RX, 2=SUBGHZ, 3=TERMINATED)

        Raises:
            ApexError: If the antenna selection command fails
            ValueError: If ant is not in range 0-3
        """
        ...

    def cc1101_write_regs(self, reg_addr: int, data: bytes) -> None:
        """Write consecutive CC1101 sub-GHz radio registers.

        Args:
            reg_addr: Starting register address (0x00-0x3D)
            data: Register data bytes to write

        Raises:
            ApexError: If the CC1101 configuration command fails
            ValueError: If reg_addr or data length is invalid
        """
        ...

    def cc1101_read_regs(self, reg_addr: int, reg_len: int) -> bytes:
        """Read consecutive CC1101 sub-GHz radio registers.

        Args:
            reg_addr: Starting register address (0x00-0x3D)
            reg_len: Number of registers to read (1-64)

        Returns:
            bytes: Register values read from the CC1101

        Raises:
            ApexError: If the CC1101 read command fails
            ValueError: If reg_addr or reg_len is invalid
        """
        ...

    def cc1101_set_channel(self, channel: int) -> None:
        """Set the CC1101 channel number.

        Args:
            channel: Channel number (0-255)

        Raises:
            ApexError: If the channel configuration fails
        """
        ...

    def cc1101_set_power(self, power_dbm: int) -> None:
        """Set the CC1101 TX output power.

        Args:
            power_dbm: TX power in dBm (-30 to +10)

        Raises:
            ApexError: If the power configuration fails
            ValueError: If power_dbm is out of range
        """
        ...

    def cc1101_set_band(self, band: int) -> None:
        """Switch the CC1101 to a different ISM band.

        Performs a full re-initialization: IDLE, flush FIFOs, write the
        configuration table for the selected band, calibrate, and verify.

        Args:
            band: Band identifier (0=433 MHz, 1=868 MHz, 2=915 MHz)

        Raises:
            ApexError: If the band switch fails
            ValueError: If band is not 0, 1, or 2
        """
        ...

    def nfc_transact(self, cmd: int, flags: int, data: bytes = b"") -> Dict[str, Any]:
        """Perform an NFC transaction.

        Args:
            cmd: NFC command byte (ISO 14443 A/B type)
            flags: Transaction flags
            data: TX data bytes (up to 256 bytes)

        Returns:
            Dict with keys:
                'data': bytes — response data from the NFC tag
                'data_len': int — length of response data

        Raises:
            ApexError: If the NFC transaction fails
            ValueError: If data exceeds 256 bytes
        """
        ...

    def nfc_poll(self, timeout_ms: int = 100) -> bool:
        """Poll for an NFC tag in the field.

        Activates the NFC carrier field, sends REQA, and waits for
        a tag response.

        Args:
            timeout_ms: Timeout in milliseconds (default: 100)

        Returns:
            True if a tag was detected, False if timeout elapsed

        Raises:
            ApexError: On communication error
        """
        ...

    def nfc_field_on(self) -> None:
        """Turn on the NFC 13.56 MHz carrier field.

        Raises:
            ApexError: If the NFC field cannot be activated
        """
        ...

    def nfc_field_off(self) -> None:
        """Turn off the NFC carrier field.

        Raises:
            ApexError: If the NFC field cannot be deactivated
        """
        ...

    def get_telemetry(self) -> Dict[str, Any]:
        """Read current telemetry data from the MCU.

        Returns:
            Dict with keys:
                'rssi_dbm_x10': int — SDR RSSI in dBm × 10
                'temp_c_x10': int — MCU die temperature in °C × 10
                'vbat_mv': int — Battery voltage in mV
                'cc1101_rssi_x10': int — CC1101 RSSI in dBm × 10
                'nfc_field_mv': int — NFC field strength in mV
                'flags': int — Status flags bitmask
                'uptime_ms': int — MCU uptime in milliseconds

        Raises:
            ApexError: If telemetry cannot be read
        """
        ...

    def get_status(self) -> Dict[str, Any]:
        """Get the driver and device status.

        Returns:
            Dict with keys:
                'driver_flags': int — Driver status flags
                'mcu_ready': bool — MCU has reported ready
                'mcu_in_reset': bool — MCU is held in reset
                'spi_error': bool — SPI communication error detected

        Raises:
            ApexError: If status cannot be read
        """
        ...

    def mcu_reset(self, assert_reset: bool) -> None:
        """Assert or deassert the MCU reset line.

        Args:
            assert_reset: True = hold MCU in reset, False = release reset

        Raises:
            ApexError: If the reset command fails
        """
        ...

    def soft_reset(self) -> None:
        """Trigger a soft reset of the MCU coprocessor.

        Sends a CMD_RESET_MCU command with the magic value 0x52534554 ("RSET").
        The MCU firmware validates the magic before triggering a watchdog reset.
        After a successful reset, the MCU reboots in ~200 ms.

        Raises:
            ApexError: If the reset command fails
        """
        ...

    def sg_start(self, buf_count: int = 4, buf_size: int = 65536,
                 timeout_ms: int = 1000, spi_speed_hz: int = 10000000,
                 continuous: bool = True) -> None:
        """Start the scatter-gather DMA engine for high-throughput IQ streaming.

        Args:
            buf_count: Number of DMA buffers (2-16, default: 4)
            buf_size: Size of each buffer in bytes (default: 65536)
            timeout_ms: DMA completion timeout in ms (default: 1000)
            spi_speed_hz: SPI clock speed in Hz (default: 10000000)
            continuous: If True, stream continuously; if False, single capture

        Raises:
            ApexError: If the SG engine cannot be started
            ValueError: If buf_count or buf_size is out of range
        """
        ...

    def sg_stop(self) -> None:
        """Stop the scatter-gather DMA engine.

        Raises:
            ApexError: If the SG engine cannot be stopped
        """
        ...

    def sg_get_status(self) -> Dict[str, Any]:
        """Get scatter-gather DMA engine status.

        Returns:
            Dict with keys:
                'state': str — Engine state ('idle', 'running', 'error')
                'buf_count': int — Number of buffers
                'buf_size': int — Size of each buffer
                'total_transferred': int — Total bytes transferred
                'overruns': int — Number of buffer overruns
                'errors': int — Number of errors
                'frames_rx': int — Number of frames received
                'frames_crc_err': int — Number of frames with CRC errors

        Raises:
            ApexError: If status cannot be read
        """
        ...

    def sg_mmap(self) -> memoryview:
        """Memory-map the scatter-gather DMA buffers for zero-copy IQ access.

        Returns:
            memoryview: Mapped buffer area for direct IQ sample access

        Raises:
            ApexError: If mmap fails
        """
        ...

    def sg_munmap(self) -> None:
        """Unmap the scatter-gather DMA buffers.

        Raises:
            ApexError: If munmap fails
        """
        ...


class ApexDeviceContext:
    """Context manager wrapper for ApexDevice.

    Automatically opens and closes the device connection.

    Example:
        with pyapex.ApexDeviceContext('/dev/apex_bridge0') as dev:
            dev.sdr_tune(868e6, 20000, 30.0)
            telem = dev.get_telemetry()
    """

    def __init__(self, device_path: str = "/dev/apex_bridge0") -> None: ...
    def __enter__(self) -> ApexDevice: ...
    def __exit__(self, exc_type: type, exc_val: Exception, exc_tb: object) -> None: ...


# Module-level convenience functions

def open_device(device_path: str = "/dev/apex_bridge0") -> ApexDevice:
    """Open a GhostBlade device connection.

    Convenience wrapper around ApexDevice constructor.

    Args:
        device_path: Path to the device node

    Returns:
        ApexDevice instance

    Raises:
        ApexError: If the device cannot be opened
    """
    ...

def battery_percent(vbat_mv: int) -> int:
    """Estimate battery charge percentage from voltage.

    Args:
        vbat_mv: Battery voltage in mV (typically from get_telemetry())

    Returns:
        Estimated charge percentage (0-100)
    """
    ...

def is_low_battery(telem: Dict[str, Any]) -> bool:
    """Check if battery voltage is below the low threshold (3.3V).

    Args:
        telem: Telemetry dictionary from get_telemetry()

    Returns:
        True if battery is below 3.3V
    """
    ...

def is_overtemp(telem: Dict[str, Any]) -> bool:
    """Check if MCU die temperature exceeds 85.0°C.

    Args:
        telem: Telemetry dictionary from get_telemetry()

    Returns:
        True if temperature exceeds 85.0°C
    """
    ...