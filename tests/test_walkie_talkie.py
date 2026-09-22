"""
test_walkie_talkie.py — Unit Tests for the GhostBlade Walkie-Talkie Module

Copyright (C) 2026 GhostBlade Project
SPDX-License-Identifier: GPL-2.0-or-later

Tests for software/libapex/walkie_talkie.py, exercising:

  1.  PTTMode enum values (must match SPI_CMD_AUDIO_PTT payload byte 0)
  2.  SDR_MODES dict completeness and internal consistency
  3.  Common frequency constants within supported hardware range
  4.  configure_sdr() — valid modes accepted, invalid modes raise ValueError
  5.  configure_sdr() — bandwidth override respected
  6.  configure_cc1101() — frequency and modulation stored correctly
  7.  configure_voip() — server/port/channel stored correctly
  8.  set_volume() — clamped to [-96, 0]
  9.  set_mic_gain() — clamped to [0, 24]
  10. ptt_start() / ptt_stop() — state transitions in simulation mode
  11. ptt() context manager — start on enter, stop on exit (normal path)
  12. ptt() context manager — stop guaranteed on exception
  13. ptt_start() double-start raises RuntimeError
  14. ptt_stop() when idle is a no-op (does not raise)
  15. is_transmitting / active_mode properties
  16. close() stops PTT if active
  17. Context manager protocol (__enter__/__exit__)
  18. Frequency constants are distinct and non-zero

All tests run on the host without the apex_bridge device; walkie_talkie.py
gracefully degrades to simulation mode when pyapex is unavailable.

Build and run (host, no hardware required):

    cd /path/to/ghostblade
    python3 -m pytest tests/test_walkie_talkie.py -v

Or without pytest:

    python3 tests/test_walkie_talkie.py
"""

import sys
import os
import unittest
from unittest.mock import patch, MagicMock

# Make the libapex package importable from the repo root
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_REPO_ROOT, 'software', 'libapex'))

# Import the module under test (runs in simulation mode — no real apex device)
from walkie_talkie import WalkieTalkie, PTTMode, SDR_MODES  # noqa: E402
import walkie_talkie as _wt_module  # noqa: E402


# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

def make_wt(**kwargs) -> WalkieTalkie:
    """Return a WalkieTalkie in simulation mode (no real device)."""
    return WalkieTalkie('/dev/null-test', **kwargs)


# ─────────────────────────────────────────────────────────────────────────────
# 1. PTTMode enum values
# ─────────────────────────────────────────────────────────────────────────────

class TestPTTModeEnum(unittest.TestCase):
    """PTTMode constants must match SPI_CMD_AUDIO_PTT payload encoding.

    These values are embedded in the SPI wire protocol.  Any mismatch
    between this Python enum and the RP2350B firmware (es8388_ptt_mode_t)
    or the kernel driver (apex_bridge_regs.h) will silently misroute PTT
    commands to the wrong radio backend.
    """

    def test_off_is_zero(self):
        self.assertEqual(PTTMode.OFF, 0)

    def test_sdr_is_one(self):
        self.assertEqual(PTTMode.SDR, 1)

    def test_cc1101_is_two(self):
        self.assertEqual(PTTMode.CC1101, 2)

    def test_wifi_is_three(self):
        self.assertEqual(PTTMode.WIFI, 3)

    def test_bt_is_four(self):
        self.assertEqual(PTTMode.BT, 4)

    def test_five_modes_defined(self):
        self.assertEqual(len(PTTMode), 5)

    def test_all_values_are_ints(self):
        for mode in PTTMode:
            self.assertIsInstance(int(mode), int)


# ─────────────────────────────────────────────────────────────────────────────
# 2. SDR_MODES dict
# ─────────────────────────────────────────────────────────────────────────────

class TestSDRModes(unittest.TestCase):
    """SDR_MODES must cover all documented voice modes and have sane bandwidths."""

    EXPECTED_MODES = {'nfm', 'wfm', 'am', 'usb', 'lsb', 'dmr', 'p25', 'freedv'}

    def test_all_expected_modes_present(self):
        for mode in self.EXPECTED_MODES:
            with self.subTest(mode=mode):
                self.assertIn(mode, SDR_MODES)

    def test_each_mode_has_bandwidth_hz(self):
        for name, spec in SDR_MODES.items():
            with self.subTest(mode=name):
                self.assertIn('bandwidth_hz', spec,
                              f"SDR mode '{name}' missing 'bandwidth_hz'")
                self.assertGreater(spec['bandwidth_hz'], 0)

    def test_each_mode_has_description(self):
        for name, spec in SDR_MODES.items():
            with self.subTest(mode=name):
                self.assertIn('desc', spec,
                              f"SDR mode '{name}' missing 'desc'")
                self.assertIsInstance(spec['desc'], str)
                self.assertGreater(len(spec['desc']), 0)

    def test_nfm_bandwidth_narrowband(self):
        # NFM voice must be ≤ 12.5 kHz (narrowband channel plan)
        self.assertLessEqual(SDR_MODES['nfm']['bandwidth_hz'], 12_500)

    def test_wfm_bandwidth_wideband(self):
        # WFM (broadcast) must be substantially wider than NFM
        self.assertGreater(SDR_MODES['wfm']['bandwidth_hz'],
                           SDR_MODES['nfm']['bandwidth_hz'])

    def test_bandwidths_are_positive_integers(self):
        for name, spec in SDR_MODES.items():
            with self.subTest(mode=name):
                self.assertIsInstance(spec['bandwidth_hz'], int)
                self.assertGreater(spec['bandwidth_hz'], 0)


# ─────────────────────────────────────────────────────────────────────────────
# 3. Common frequency constants
# ─────────────────────────────────────────────────────────────────────────────

class TestFrequencyConstants(unittest.TestCase):
    """Frequency constants should fall within LMS7002M or CC1101 range.

    LMS7002M supports 100 kHz – 3.8 GHz.
    CC1101 supports 300 – 928 MHz.
    All listed constants should be within LMS7002M range.
    """

    _LMS7_MIN_HZ = 100_000
    _LMS7_MAX_HZ = 3_800_000_000

    _CONSTANTS = [
        ('FREQ_PMR446_CH1',  _wt_module.FREQ_PMR446_CH1),
        ('FREQ_FRS_CH1',     _wt_module.FREQ_FRS_CH1),
        ('FREQ_MURS_CH1',    _wt_module.FREQ_MURS_CH1),
        ('FREQ_GMRS_CH1',    _wt_module.FREQ_GMRS_CH1),
        ('FREQ_MARINE_CH16', _wt_module.FREQ_MARINE_CH16),
        ('FREQ_AVIATION_ATIS', _wt_module.FREQ_AVIATION_ATIS),
        ('FREQ_ISM_433',     _wt_module.FREQ_ISM_433),
        ('FREQ_ISM_915',     _wt_module.FREQ_ISM_915),
    ]

    def test_all_constants_nonzero(self):
        for name, val in self._CONSTANTS:
            with self.subTest(constant=name):
                self.assertGreater(val, 0)

    def test_all_constants_within_lms7_range(self):
        for name, val in self._CONSTANTS:
            with self.subTest(constant=name):
                self.assertGreaterEqual(
                    val, self._LMS7_MIN_HZ,
                    f"{name}={val} Hz below LMS7002M minimum")
                self.assertLessEqual(
                    val, self._LMS7_MAX_HZ,
                    f"{name}={val} Hz above LMS7002M maximum")

    def test_pmr446_in_pmr_band(self):
        # PMR446 is 446.0–446.2 MHz
        self.assertGreaterEqual(_wt_module.FREQ_PMR446_CH1, 446_000_000)
        self.assertLessEqual(_wt_module.FREQ_PMR446_CH1,    446_200_000)

    def test_ism_433_in_ism_band(self):
        # 433 MHz ISM: 433.05–434.79 MHz
        self.assertGreaterEqual(_wt_module.FREQ_ISM_433, 433_050_000)
        self.assertLessEqual(_wt_module.FREQ_ISM_433,    434_790_000)

    def test_ism_915_in_ism_band(self):
        # 915 MHz ISM: 902–928 MHz (FCC Part 15)
        self.assertGreaterEqual(_wt_module.FREQ_ISM_915, 902_000_000)
        self.assertLessEqual(_wt_module.FREQ_ISM_915,    928_000_000)

    def test_all_constants_distinct(self):
        values = [val for _, val in self._CONSTANTS]
        self.assertEqual(len(values), len(set(values)),
                         "Two frequency constants share the same Hz value")


# ─────────────────────────────────────────────────────────────────────────────
# 4 & 5. configure_sdr()
# ─────────────────────────────────────────────────────────────────────────────

class TestConfigureSDR(unittest.TestCase):

    def setUp(self):
        self.wt = make_wt()

    def tearDown(self):
        self.wt.close()

    def test_valid_mode_stored(self):
        self.wt.configure_sdr(freq_hz=446_006_250, mode='nfm')
        self.assertEqual(self.wt._sdr_mode, 'nfm')

    def test_valid_frequency_stored(self):
        self.wt.configure_sdr(freq_hz=915_000_000, mode='am')
        self.assertEqual(self.wt._sdr_freq_hz, 915_000_000)

    def test_all_valid_modes_accepted(self):
        for mode in SDR_MODES:
            with self.subTest(mode=mode):
                # Should not raise
                self.wt.configure_sdr(mode=mode)

    def test_invalid_mode_raises_value_error(self):
        with self.assertRaises(ValueError):
            self.wt.configure_sdr(mode='fm_stereo_invalid')

    def test_invalid_mode_error_message_names_mode(self):
        try:
            self.wt.configure_sdr(mode='bogus_mode')
            self.fail("Expected ValueError")
        except ValueError as exc:
            self.assertIn('bogus_mode', str(exc))

    def test_default_bandwidth_from_mode(self):
        self.wt.configure_sdr(mode='nfm')
        self.assertEqual(self.wt._sdr_bandwidth_hz, SDR_MODES['nfm']['bandwidth_hz'])

    def test_bandwidth_override_respected(self):
        self.wt.configure_sdr(mode='nfm', bandwidth_hz=6_250)
        self.assertEqual(self.wt._sdr_bandwidth_hz, 6_250)

    def test_bandwidth_override_does_not_mutate_sdr_modes(self):
        original = SDR_MODES['nfm']['bandwidth_hz']
        self.wt.configure_sdr(mode='nfm', bandwidth_hz=6_250)
        self.assertEqual(SDR_MODES['nfm']['bandwidth_hz'], original)


# ─────────────────────────────────────────────────────────────────────────────
# 6. configure_cc1101()
# ─────────────────────────────────────────────────────────────────────────────

class TestConfigureCC1101(unittest.TestCase):

    def setUp(self):
        self.wt = make_wt()

    def tearDown(self):
        self.wt.close()

    def test_frequency_stored(self):
        self.wt.configure_cc1101(freq_hz=868_000_000)
        self.assertEqual(self.wt._cc1101_freq_hz, 868_000_000)

    def test_modulation_stored(self):
        self.wt.configure_cc1101(modulation='fsk')
        self.assertEqual(self.wt._cc1101_modulation, 'fsk')

    def test_ook_modulation(self):
        self.wt.configure_cc1101(modulation='ook')
        self.assertEqual(self.wt._cc1101_modulation, 'ook')

    def test_default_frequency_is_ism433(self):
        self.wt.configure_cc1101()
        self.assertEqual(self.wt._cc1101_freq_hz, _wt_module.FREQ_ISM_433)


# ─────────────────────────────────────────────────────────────────────────────
# 7. configure_voip()
# ─────────────────────────────────────────────────────────────────────────────

class TestConfigureVoIP(unittest.TestCase):

    def setUp(self):
        self.wt = make_wt()

    def tearDown(self):
        self.wt.close()

    def test_server_stored(self):
        self.wt.configure_voip(server='192.168.1.10')
        self.assertEqual(self.wt._voip_server, '192.168.1.10')

    def test_port_stored(self):
        self.wt.configure_voip(server='10.0.0.1', port=64740)
        self.assertEqual(self.wt._voip_port, 64740)

    def test_channel_stored(self):
        self.wt.configure_voip(server='10.0.0.1', channel='Alpha')
        self.assertEqual(self.wt._voip_channel, 'Alpha')

    def test_default_port_is_mumble(self):
        self.wt.configure_voip(server='10.0.0.1')
        self.assertEqual(self.wt._voip_port, 64738)

    def test_default_channel_root(self):
        self.wt.configure_voip(server='10.0.0.1')
        self.assertEqual(self.wt._voip_channel, 'Root')


# ─────────────────────────────────────────────────────────────────────────────
# 8. set_volume() clamping
# ─────────────────────────────────────────────────────────────────────────────

class TestSetVolume(unittest.TestCase):
    """set_volume() must clamp its argument to [-96, 0] before forwarding."""

    def setUp(self):
        self.wt = make_wt()
        # Capture what value was passed to _apex_ioctl
        self._calls = []
        self.wt._apex_ioctl = lambda cmd, val: self._calls.append((cmd, val))

    def tearDown(self):
        self.wt.close()

    def test_zero_db_passes_through(self):
        self.wt.set_volume(0)
        self.assertEqual(self._calls[-1], ('audio_volume', 0))

    def test_negative_96_passes_through(self):
        self.wt.set_volume(-96)
        self.assertEqual(self._calls[-1], ('audio_volume', -96))

    def test_positive_clamped_to_zero(self):
        self.wt.set_volume(10)
        self.assertEqual(self._calls[-1], ('audio_volume', 0))

    def test_below_min_clamped_to_minus_96(self):
        self.wt.set_volume(-200)
        self.assertEqual(self._calls[-1], ('audio_volume', -96))

    def test_midrange_value_passes_through(self):
        self.wt.set_volume(-40)
        self.assertEqual(self._calls[-1], ('audio_volume', -40))


# ─────────────────────────────────────────────────────────────────────────────
# 9. set_mic_gain() clamping
# ─────────────────────────────────────────────────────────────────────────────

class TestSetMicGain(unittest.TestCase):
    """set_mic_gain() must clamp its argument to [0, 24] before forwarding."""

    def setUp(self):
        self.wt = make_wt()
        self._calls = []
        self.wt._apex_ioctl = lambda cmd, val: self._calls.append((cmd, val))

    def tearDown(self):
        self.wt.close()

    def test_zero_passes_through(self):
        self.wt.set_mic_gain(0)
        self.assertEqual(self._calls[-1], ('audio_mic_gain', 0))

    def test_max_passes_through(self):
        self.wt.set_mic_gain(24)
        self.assertEqual(self._calls[-1], ('audio_mic_gain', 24))

    def test_above_max_clamped_to_24(self):
        self.wt.set_mic_gain(30)
        self.assertEqual(self._calls[-1], ('audio_mic_gain', 24))

    def test_negative_clamped_to_zero(self):
        self.wt.set_mic_gain(-5)
        self.assertEqual(self._calls[-1], ('audio_mic_gain', 0))

    def test_midrange_passes_through(self):
        self.wt.set_mic_gain(12)
        self.assertEqual(self._calls[-1], ('audio_mic_gain', 12))


# ─────────────────────────────────────────────────────────────────────────────
# 10–14. PTT state machine in simulation mode
# ─────────────────────────────────────────────────────────────────────────────

class TestPTTStateMachine(unittest.TestCase):
    """Test PTT state transitions without real hardware.

    In simulation mode (no apex_bridge device), _apex is None and
    _apex_ptt / _apex_ioctl print to stdout instead of issuing ioctls.
    We patch out subprocess.Popen to prevent GNU Radio / mumble launches.
    """

    def setUp(self):
        self.wt = make_wt()

    def tearDown(self):
        # Ensure PTT is stopped even if a test leaves it active
        try:
            if self.wt.is_transmitting:
                self.wt.ptt_stop()
        except Exception:
            pass
        self.wt.close()

    # 10 — basic start/stop
    def test_initial_state_is_off(self):
        self.assertFalse(self.wt.is_transmitting)
        self.assertEqual(self.wt.active_mode, PTTMode.OFF)

    def test_ptt_start_sets_active_mode(self):
        with patch('subprocess.Popen'):
            self.wt.ptt_start(PTTMode.SDR)
        self.assertTrue(self.wt.is_transmitting)
        self.assertEqual(self.wt.active_mode, PTTMode.SDR)

    def test_ptt_stop_clears_active_mode(self):
        with patch('subprocess.Popen'):
            self.wt.ptt_start(PTTMode.SDR)
            self.wt.ptt_stop()
        self.assertFalse(self.wt.is_transmitting)
        self.assertEqual(self.wt.active_mode, PTTMode.OFF)

    def test_ptt_start_cc1101(self):
        self.wt.ptt_start(PTTMode.CC1101)
        self.assertEqual(self.wt.active_mode, PTTMode.CC1101)
        self.wt.ptt_stop()

    def test_ptt_start_bt(self):
        with patch('subprocess.run'):
            self.wt.ptt_start(PTTMode.BT)
        self.assertEqual(self.wt.active_mode, PTTMode.BT)
        self.wt.ptt_stop()

    # 13 — double start raises
    def test_double_ptt_start_raises_runtime_error(self):
        with patch('subprocess.Popen'):
            self.wt.ptt_start(PTTMode.SDR)
        try:
            with self.assertRaises(RuntimeError):
                self.wt.ptt_start(PTTMode.CC1101)
        finally:
            self.wt.ptt_stop()

    def test_double_ptt_start_error_message_includes_mode(self):
        with patch('subprocess.Popen'):
            self.wt.ptt_start(PTTMode.SDR)
        try:
            with self.assertRaises(RuntimeError) as ctx:
                self.wt.ptt_start(PTTMode.CC1101)
            self.assertIn('SDR', str(ctx.exception))
        finally:
            self.wt.ptt_stop()

    # 14 — stop when idle is a no-op
    def test_ptt_stop_when_idle_does_not_raise(self):
        # Should be a safe no-op
        self.wt.ptt_stop()
        self.assertFalse(self.wt.is_transmitting)

    def test_ptt_stop_twice_does_not_raise(self):
        with patch('subprocess.Popen'):
            self.wt.ptt_start(PTTMode.SDR)
            self.wt.ptt_stop()
        self.wt.ptt_stop()  # second stop — must not raise

    # WiFi start without configure raises RuntimeError
    def test_ptt_start_wifi_without_configure_raises(self):
        # _active_mode is set before _voip_ptt_start raises, so
        # tearDown's ptt_stop() will try to call mumble-ctl; patch it out.
        with patch('subprocess.run'), patch('subprocess.Popen'):
            with self.assertRaises(RuntimeError):
                self.wt.ptt_start(PTTMode.WIFI)
            # Manually reset state so tearDown does not call mumble-ctl
            self.wt._active_mode = PTTMode.OFF


# ─────────────────────────────────────────────────────────────────────────────
# 11 & 12. ptt() context manager
# ─────────────────────────────────────────────────────────────────────────────

class TestPTTContextManager(unittest.TestCase):

    def setUp(self):
        self.wt = make_wt()

    def tearDown(self):
        try:
            if self.wt.is_transmitting:
                self.wt.ptt_stop()
        except Exception:
            pass
        self.wt.close()

    def test_context_manager_starts_ptt(self):
        with patch('subprocess.Popen'):
            with self.wt.ptt(PTTMode.SDR):
                self.assertTrue(self.wt.is_transmitting)
                self.assertEqual(self.wt.active_mode, PTTMode.SDR)

    def test_context_manager_stops_ptt_on_exit(self):
        with patch('subprocess.Popen'):
            with self.wt.ptt(PTTMode.SDR):
                pass
        self.assertFalse(self.wt.is_transmitting)

    def test_context_manager_stops_ptt_on_exception(self):
        """PTT must be released even when the body raises."""
        with patch('subprocess.Popen'):
            try:
                with self.wt.ptt(PTTMode.SDR):
                    raise RuntimeError("simulated body failure")
            except RuntimeError:
                pass
        self.assertFalse(self.wt.is_transmitting)

    def test_context_manager_cc1101(self):
        with self.wt.ptt(PTTMode.CC1101):
            self.assertEqual(self.wt.active_mode, PTTMode.CC1101)
        self.assertFalse(self.wt.is_transmitting)


# ─────────────────────────────────────────────────────────────────────────────
# 15. Properties
# ─────────────────────────────────────────────────────────────────────────────

class TestProperties(unittest.TestCase):

    def setUp(self):
        self.wt = make_wt()

    def tearDown(self):
        try:
            if self.wt.is_transmitting:
                self.wt.ptt_stop()
        except Exception:
            pass
        self.wt.close()

    def test_is_transmitting_false_initially(self):
        self.assertFalse(self.wt.is_transmitting)

    def test_is_transmitting_true_while_ptt_active(self):
        with patch('subprocess.Popen'):
            self.wt.ptt_start(PTTMode.SDR)
        self.assertTrue(self.wt.is_transmitting)
        self.wt.ptt_stop()

    def test_active_mode_off_initially(self):
        self.assertEqual(self.wt.active_mode, PTTMode.OFF)

    def test_active_mode_reflects_ptt_state(self):
        self.wt.ptt_start(PTTMode.CC1101)
        self.assertEqual(self.wt.active_mode, PTTMode.CC1101)
        self.wt.ptt_stop()
        self.assertEqual(self.wt.active_mode, PTTMode.OFF)


# ─────────────────────────────────────────────────────────────────────────────
# 16. close() lifecycle
# ─────────────────────────────────────────────────────────────────────────────

class TestClose(unittest.TestCase):

    def test_close_stops_active_ptt(self):
        wt = make_wt()
        with patch('subprocess.Popen'):
            wt.ptt_start(PTTMode.SDR)
        self.assertTrue(wt.is_transmitting)
        wt.close()
        self.assertFalse(wt.is_transmitting)

    def test_close_when_idle_does_not_raise(self):
        wt = make_wt()
        wt.close()  # Must not raise

    def test_double_close_does_not_raise(self):
        wt = make_wt()
        wt.close()
        wt.close()


# ─────────────────────────────────────────────────────────────────────────────
# 17. Context manager protocol (__enter__ / __exit__)
# ─────────────────────────────────────────────────────────────────────────────

class TestContextManagerProtocol(unittest.TestCase):

    def test_enter_returns_self(self):
        wt = make_wt()
        with wt as wt2:
            self.assertIs(wt, wt2)

    def test_exit_calls_close(self):
        wt = make_wt()
        closed = []
        original_close = wt.close
        wt.close = lambda: closed.append(True) or original_close()
        with wt:
            pass
        self.assertTrue(closed, "close() was not called on __exit__")

    def test_exit_called_on_exception(self):
        wt = make_wt()
        closed = []
        original_close = wt.close
        wt.close = lambda: closed.append(True) or original_close()
        try:
            with wt:
                raise ValueError("test exception")
        except ValueError:
            pass
        self.assertTrue(closed, "close() not called when exception escaped")


# ─────────────────────────────────────────────────────────────────────────────
# Simulation-mode helpers
# ─────────────────────────────────────────────────────────────────────────────

class TestSimulationMode(unittest.TestCase):
    """WalkieTalkie must initialise cleanly when pyapex is absent."""

    def test_apex_none_when_device_not_found(self):
        # /dev/null-test does not exist and pyapex is not installed in this
        # environment, so _apex should be None.
        wt = make_wt()
        try:
            self.assertIsNone(wt._apex)
        finally:
            wt.close()

    def test_ptt_commands_do_not_raise_without_apex(self):
        wt = make_wt()
        try:
            # These call _apex_ioctl which must be safe with _apex=None
            wt.set_volume(-20)
            wt.set_mic_gain(12)
        finally:
            wt.close()


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    loader = unittest.TestLoader()
    suite  = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
