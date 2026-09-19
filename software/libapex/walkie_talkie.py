"""
walkie_talkie.py — GhostBlade Walkie-Talkie / PTT Voice Module

Provides push-to-talk voice communication over four radio backends:
  - SDR (LMS7002M): any frequency 100 kHz–3.8 GHz via GNU Radio
    Modes: NFM narrowband, AM, DMR, P25, FreeDV HF digital voice
  - CC1101: sub-GHz OOK/FSK voice (300–928 MHz)
  - Wi-Fi 6E: VoIP over LAN/internet (Mumble protocol)
  - Bluetooth: SCO/HFP headset pairing

Architecture:
  - PTT assertion is sent to the RP2350B via the apex_bridge ioctl
    (SPI_CMD_AUDIO_PTT), which controls the ES8388 codec mute and
    the LMS7002M TX enable GPIO
  - Voice capture and encoding is handled by the RK3576 Linux ALSA
    subsystem + GNU Radio (for SDR) or platform audio stack (BT/Wi-Fi)
  - This module orchestrates the full pipeline end-to-end

Usage:
    from walkie_talkie import WalkieTalkie, PTTMode

    wt = WalkieTalkie()

    # SDR PTT — transmit on 446.0 MHz NFM (PMR446, Europe)
    wt.configure_sdr(freq_hz=446_006_250, mode='nfm', bandwidth_hz=12500)
    with wt.ptt(PTTMode.SDR):
        # Voice is captured from MEMS mic, encoded as NFM, and
        # transmitted via LMS7002M for the duration of this block
        input("Hold ENTER while transmitting...")

    # Sub-GHz PTT — 433.92 MHz OOK (ISM band)
    wt.configure_cc1101(freq_hz=433_920_000, modulation='ook')
    with wt.ptt(PTTMode.CC1101):
        input("Hold ENTER while transmitting...")

    # Wi-Fi VoIP — connect to a Mumble server
    wt.configure_voip(server='192.168.1.10', port=64738, channel='Field Ops')
    wt.connect_voip()
    with wt.ptt(PTTMode.WIFI):
        input("Hold ENTER while transmitting...")

    wt.close()
"""

from __future__ import annotations

import subprocess
import threading
import time
from contextlib import contextmanager
from enum import IntEnum
from typing import Optional

try:
    import pyapex as _pyapex_mod
    _HAS_APEX = True
except ImportError:
    _pyapex_mod = None  # type: ignore[assignment]
    _HAS_APEX = False

# ──────────────────────────────────────────────────────────────────────────────
# PTT mode constants (must match SPI_CMD_AUDIO_PTT payload byte 0)
# ──────────────────────────────────────────────────────────────────────────────

class PTTMode(IntEnum):
    OFF     = 0   # Not transmitting
    SDR     = 1   # LMS7002M: any freq, NFM/AM/DMR/P25/FreeDV
    CC1101  = 2   # CC1101 sub-GHz: OOK/FSK/GFSK
    WIFI    = 3   # Wi-Fi 6E VoIP (Mumble/SIP)
    BT      = 4   # Bluetooth SCO/HFP


# ──────────────────────────────────────────────────────────────────────────────
# SDR voice modes (GNU Radio flowgraph selector)
# ──────────────────────────────────────────────────────────────────────────────

SDR_MODES = {
    'nfm':    {'bandwidth_hz': 12_500,  'desc': 'Narrowband FM (voice, PMR/FRS/MURS)'},
    'wfm':    {'bandwidth_hz': 200_000, 'desc': 'Wideband FM (broadcast quality)'},
    'am':     {'bandwidth_hz': 10_000,  'desc': 'AM voice (aviation, shortwave)'},
    'usb':    {'bandwidth_hz': 3_000,   'desc': 'Upper sideband SSB (HF voice)'},
    'lsb':    {'bandwidth_hz': 3_000,   'desc': 'Lower sideband SSB (HF voice)'},
    'dmr':    {'bandwidth_hz': 12_500,  'desc': 'DMR digital voice (tier II)'},
    'p25':    {'bandwidth_hz': 12_500,  'desc': 'P25 Phase 1 digital voice'},
    'freedv': {'bandwidth_hz': 2_500,   'desc': 'FreeDV open-source HF digital voice'},
}

# Common walkie-talkie frequencies (Hz)
FREQ_PMR446_CH1    = 446_006_250   # Europe PMR446 channel 1
FREQ_FRS_CH1       = 462_562_500   # USA FRS channel 1
FREQ_MURS_CH1      = 151_820_000   # USA MURS channel 1
FREQ_GMRS_CH1      = 462_550_000   # USA GMRS channel 1
FREQ_MARINE_CH16   = 156_800_000   # Maritime distress / calling
FREQ_AVIATION_ATIS = 135_275_000   # Aviation ATIS example
FREQ_ISM_433       = 433_920_000   # ISM 433 MHz
FREQ_ISM_915       = 915_000_000   # ISM 915 MHz


class WalkieTalkie:
    """
    GhostBlade push-to-talk walkie-talkie controller.

    Manages the full PTT pipeline:
      - RP2350B audio codec (ES8388) mute/unmute via apex_bridge ioctl
      - LMS7002M TX enable assertion (SDR mode)
      - GNU Radio flowgraph launch/teardown for SDR voice
      - Mumble client for Wi-Fi VoIP
    """

    def __init__(self, device: str = '/dev/apex_bridge0'):
        self._device = device
        self._apex: Optional[object] = None
        self._active_mode = PTTMode.OFF
        self._lock = threading.Lock()

        # SDR config
        self._sdr_freq_hz = FREQ_PMR446_CH1
        self._sdr_mode = 'nfm'
        self._sdr_bandwidth_hz = 12_500
        self._sdr_proc: Optional[subprocess.Popen] = None

        # CC1101 config
        self._cc1101_freq_hz = FREQ_ISM_433
        self._cc1101_modulation = 'ook'

        # VoIP config
        self._voip_server: Optional[str] = None
        self._voip_port = 64738
        self._voip_channel = 'Root'
        self._voip_proc: Optional[subprocess.Popen] = None

        if _HAS_APEX and _pyapex_mod is not None:
            try:
                self._apex = _pyapex_mod.ApexBridge(device)
            except OSError as e:
                print(f"[WalkieTalkie] Warning: could not open {device}: {e}")
                print("[WalkieTalkie] Running in simulation mode")

    # ─────────────────────────────────────────────────────────────────────
    # Configuration
    # ─────────────────────────────────────────────────────────────────────

    def configure_sdr(
        self,
        freq_hz: int = FREQ_PMR446_CH1,
        mode: str = 'nfm',
        bandwidth_hz: Optional[int] = None,
    ) -> None:
        """
        Configure SDR PTT parameters.

        Args:
            freq_hz:      Center frequency in Hz
            mode:         Voice mode — 'nfm', 'am', 'usb', 'lsb', 'dmr',
                          'p25', 'freedv', 'wfm'
            bandwidth_hz: Override default bandwidth for the chosen mode
        """
        if mode not in SDR_MODES:
            raise ValueError(f"Unknown SDR mode '{mode}'. Valid: {list(SDR_MODES)}")
        self._sdr_freq_hz = freq_hz
        self._sdr_mode = mode
        self._sdr_bandwidth_hz = bandwidth_hz or SDR_MODES[mode]['bandwidth_hz']
        print(f"[WalkieTalkie] SDR configured: {freq_hz/1e6:.4f} MHz "
              f"{mode.upper()} ({self._sdr_bandwidth_hz/1e3:.1f} kHz BW)")

    def configure_cc1101(
        self,
        freq_hz: int = FREQ_ISM_433,
        modulation: str = 'ook',
    ) -> None:
        """Configure CC1101 PTT parameters."""
        self._cc1101_freq_hz = freq_hz
        self._cc1101_modulation = modulation
        print(f"[WalkieTalkie] CC1101 configured: {freq_hz/1e6:.3f} MHz {modulation.upper()}")

    def configure_voip(
        self,
        server: str,
        port: int = 64738,
        channel: str = 'Root',
    ) -> None:
        """Configure Wi-Fi VoIP (Mumble) server."""
        self._voip_server = server
        self._voip_port = port
        self._voip_channel = channel
        print(f"[WalkieTalkie] VoIP configured: {server}:{port} channel='{channel}'")

    def set_volume(self, vol_db: int) -> None:
        """Set speaker volume (-96 to 0 dB)."""
        vol_db = max(-96, min(0, vol_db))
        self._apex_ioctl('audio_volume', vol_db)

    def set_mic_gain(self, gain_db: int) -> None:
        """Set MEMS microphone PGA gain (0–24 dB)."""
        gain_db = max(0, min(24, gain_db))
        self._apex_ioctl('audio_mic_gain', gain_db)

    # ─────────────────────────────────────────────────────────────────────
    # PTT control
    # ─────────────────────────────────────────────────────────────────────

    def ptt_start(self, mode: PTTMode) -> None:
        """
        Assert push-to-talk — begin voice transmission.

        For SDR mode, also tunes the LMS7002M and launches the
        GNU Radio NFM/AM/DMR/P25/FreeDV TX flowgraph.

        Prefer using the ``ptt()`` context manager over calling
        ptt_start/ptt_stop directly.
        """
        with self._lock:
            if self._active_mode != PTTMode.OFF:
                raise RuntimeError(
                    f"PTT already active in {self._active_mode.name} mode"
                )
            self._active_mode = mode

            if mode == PTTMode.SDR:
                self._sdr_ptt_start()
            elif mode == PTTMode.CC1101:
                self._cc1101_ptt_start()
            elif mode == PTTMode.WIFI:
                self._voip_ptt_start()
            elif mode == PTTMode.BT:
                self._bt_ptt_start()

            # Notify RP2350B: assert PTT, mute/unmute ES8388, gate TX
            self._apex_ptt(mode, active=True)
            print(f"[WalkieTalkie] PTT START — {mode.name}")

    def ptt_stop(self) -> None:
        """Release push-to-talk — return to receive mode."""
        with self._lock:
            if self._active_mode == PTTMode.OFF:
                return

            mode = self._active_mode

            # Notify RP2350B: release PTT, unmute ES8388, release TX
            self._apex_ptt(PTTMode.OFF, active=False)

            if mode == PTTMode.SDR:
                self._sdr_ptt_stop()
            elif mode == PTTMode.CC1101:
                self._cc1101_ptt_stop()
            elif mode == PTTMode.WIFI:
                self._voip_ptt_stop()
            elif mode == PTTMode.BT:
                self._bt_ptt_stop()

            self._active_mode = PTTMode.OFF
            print(f"[WalkieTalkie] PTT STOP — {mode.name} → RX")

    @contextmanager
    def ptt(self, mode: PTTMode):
        """
        Context manager for push-to-talk.

        Usage:
            with wt.ptt(PTTMode.SDR):
                time.sleep(5)  # transmit for 5 seconds
        """
        self.ptt_start(mode)
        try:
            yield
        finally:
            self.ptt_stop()

    @property
    def is_transmitting(self) -> bool:
        """True if PTT is currently active."""
        return self._active_mode != PTTMode.OFF

    @property
    def active_mode(self) -> PTTMode:
        """Currently active PTT mode."""
        return self._active_mode

    # ─────────────────────────────────────────────────────────────────────
    # Backend implementations
    # ─────────────────────────────────────────────────────────────────────

    def _sdr_ptt_start(self) -> None:
        """Tune LMS7002M and launch GNU Radio TX flowgraph."""
        if self._apex:
            # Tune LMS7002M to TX frequency
            self._apex.sdr_tune(
                self._sdr_freq_hz,
                self._sdr_bandwidth_hz // 1000,  # kHz
                300,  # gain 30.0 dB × 10
            )

        # Launch GNU Radio flowgraph for the selected voice mode.
        # The flowgraph reads from ALSA hw:0,0 (MEMS mic capture),
        # encodes to the selected mode, and feeds the IQ stream to
        # the LMS7002M via the MIPI-CSI-2 / SoapySDR sink.
        gr_script = f'/usr/share/ghostblade/flowgraphs/ptt_{self._sdr_mode}_tx.py'
        gr_args = [
            'python3', gr_script,
            '--freq', str(self._sdr_freq_hz),
            '--bw',   str(self._sdr_bandwidth_hz),
            '--device', 'driver=apex,type=sdr',
        ]
        try:
            self._sdr_proc = subprocess.Popen(gr_args)
            time.sleep(0.3)  # allow flowgraph to start
        except FileNotFoundError:
            print(f"[WalkieTalkie] GNU Radio flowgraph not found: {gr_script}")
            print("[WalkieTalkie] Install ghostblade-flowgraphs or build from source")

    def _sdr_ptt_stop(self) -> None:
        """Terminate GNU Radio TX flowgraph."""
        if self._sdr_proc and self._sdr_proc.poll() is None:
            self._sdr_proc.terminate()
            self._sdr_proc.wait(timeout=2)
        self._sdr_proc = None

    def _cc1101_ptt_start(self) -> None:
        """Configure CC1101 for voice TX via sub-GHz link."""
        if self._apex:
            # Set CC1101 to TX mode at configured frequency
            # CC1101 modulation register config handled by apex_bridge ioctl
            pass  # Handled by RP2350B firmware on PTT assertion

    def _cc1101_ptt_stop(self) -> None:
        pass

    def _voip_ptt_start(self) -> None:
        """Unmute the Mumble client mic channel."""
        if not self._voip_server:
            raise RuntimeError("VoIP not configured — call configure_voip() first")
        # Send unmute command to running mumble-cli instance
        # (Mumble must already be connected via connect_voip())
        self._run_mumble_cmd('self-mute', 'false')

    def _voip_ptt_stop(self) -> None:
        """Mute the Mumble client mic channel."""
        self._run_mumble_cmd('self-mute', 'true')

    def _bt_ptt_start(self) -> None:
        """Activate Bluetooth SCO link for voice TX."""
        subprocess.run(['bluetoothctl', 'connect-sco'], capture_output=True)

    def _bt_ptt_stop(self) -> None:
        pass

    def connect_voip(self) -> None:
        """
        Connect to the configured Mumble VoIP server.

        Launches mumble-cli in the background. The process persists
        until close() is called or the connection drops.
        """
        if not self._voip_server:
            raise RuntimeError("VoIP not configured — call configure_voip() first")
        cmd = [
            'mumble-cli',
            f'mumble://{self._voip_server}:{self._voip_port}/{self._voip_channel}',
            '--no-gui',
            '--audio-input', 'hw:0,0',
            '--audio-output', 'hw:0,1',
        ]
        self._voip_proc = subprocess.Popen(cmd)
        time.sleep(1)
        print(f"[WalkieTalkie] VoIP connected: {self._voip_server}:{self._voip_port} "
              f"channel='{self._voip_channel}'")

    def disconnect_voip(self) -> None:
        """Disconnect from VoIP server."""
        if self._voip_proc and self._voip_proc.poll() is None:
            self._voip_proc.terminate()
            self._voip_proc.wait(timeout=2)
        self._voip_proc = None

    def _run_mumble_cmd(self, *args: str) -> None:
        subprocess.run(['mumble-ctl', *args], capture_output=True)

    # ─────────────────────────────────────────────────────────────────────
    # apex_bridge ioctl helpers
    # ─────────────────────────────────────────────────────────────────────

    def _apex_ptt(self, mode: PTTMode, active: bool) -> None:
        """Send SPI_CMD_AUDIO_PTT to the RP2350B via apex_bridge."""
        if self._apex:
            try:
                self._apex.audio_ptt(int(mode), active)
            except AttributeError:
                pass
        else:
            print(f"[WalkieTalkie] [SIM] apex_ptt mode={mode.name} active={active}")

    def _apex_ioctl(self, cmd: str, value: int) -> None:
        if self._apex:
            try:
                getattr(self._apex, f'audio_{cmd}')(value)
            except AttributeError:
                pass
        else:
            print(f"[WalkieTalkie] [SIM] apex_{cmd}({value})")

    # ─────────────────────────────────────────────────────────────────────
    # Lifecycle
    # ─────────────────────────────────────────────────────────────────────

    def close(self) -> None:
        """Release all resources."""
        if self._active_mode != PTTMode.OFF:
            self.ptt_stop()
        self.disconnect_voip()
        self._sdr_ptt_stop()
        if self._apex:
            self._apex.close()
            self._apex = None

    def __enter__(self) -> 'WalkieTalkie':
        return self

    def __exit__(self, *_) -> None:
        self.close()


# ──────────────────────────────────────────────────────────────────────────────
# CLI helper
# ──────────────────────────────────────────────────────────────────────────────

def _cli() -> None:
    """Simple interactive PTT CLI for field use."""
    import argparse

    parser = argparse.ArgumentParser(description='GhostBlade Walkie-Talkie')
    parser.add_argument('--mode', choices=['sdr', 'cc1101', 'wifi', 'bt'],
                        default='sdr', help='PTT mode (default: sdr)')
    parser.add_argument('--freq', type=float, default=446.00625,
                        help='Frequency in MHz (SDR/CC1101 mode)')
    parser.add_argument('--voice', choices=list(SDR_MODES), default='nfm',
                        help='SDR voice encoding (default: nfm)')
    parser.add_argument('--server', help='VoIP server address (Wi-Fi mode)')
    parser.add_argument('--device', default='/dev/apex_bridge0')
    args = parser.parse_args()

    mode_map = {'sdr': PTTMode.SDR, 'cc1101': PTTMode.CC1101,
                'wifi': PTTMode.WIFI, 'bt': PTTMode.BT}
    ptt_mode = mode_map[args.mode]

    with WalkieTalkie(args.device) as wt:
        if ptt_mode == PTTMode.SDR:
            wt.configure_sdr(freq_hz=int(args.freq * 1e6), mode=args.voice)
        elif ptt_mode == PTTMode.CC1101:
            wt.configure_cc1101(freq_hz=int(args.freq * 1e6))
        elif ptt_mode == PTTMode.WIFI:
            if not args.server:
                parser.error("--server required for Wi-Fi mode")
            wt.configure_voip(server=args.server)
            wt.connect_voip()

        print(f"\nGhostBlade Walkie-Talkie — {args.mode.upper()} mode")
        print(f"Frequency: {args.freq:.4f} MHz")
        print("Hold SPACE (or ENTER) to transmit, Ctrl+C to quit\n")

        try:
            while True:
                input("Press ENTER to start TX...")
                with wt.ptt(ptt_mode):
                    input("Transmitting — press ENTER to stop TX")
        except KeyboardInterrupt:
            print("\nExiting.")


if __name__ == '__main__':
    _cli()
