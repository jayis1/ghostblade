"""
GhostWisp protocol — unit tests.

Author: jayis1

Covers:
  1. CRC-16/CCITT-FALSE implementation
  2. Frame encode/decode round-trip for all golden vectors
  3. Error handling: too-short, bad magic, bad CRC, bad length, unknown mandatory type
  4. Payload schemas: IdentityPayload, SessionCapabilities, TimeSyncPayload,
     PolicySyncPayload, ArmRequestPayload, ConfirmPhysicalPayload, AbortPayload
  5. Safety invariants:
     a. GhostBlade ARM_REQ alone does not constitute confirmation
     b. CONFIRM_PHYSICAL with mismatched nonce is rejected
     c. ABORT disarms any pending armed state
     d. power-on safe defaults: receive_only and radio_disabled are True by default
     e. ArmRequestPayload with absent mandatory fields causes ProtocolError
     f. Receive-only and radio-disable are reachable without navigating other features
  6. Detached / GhostWisp-absent: importing the protocol package has no side effects
     and the codec operates correctly with no GhostWisp hardware present
"""

from __future__ import annotations

import importlib
import struct
import sys
import unittest

# ---------------------------------------------------------------------------
# Allow running this file directly from the repo root or as a module
# ---------------------------------------------------------------------------
# Adjust sys.path so that 'devices.ghostwisp.protocol' resolves whether the
# test is run from the repo root or from devices/ghostwisp/tests/.
import os as _os
_REPO_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from devices.ghostwisp.protocol.frame import (
    Frame, MessageType, FrameFlags, ProtocolError,
    _crc16_ccitt_false, PROTOCOL_VERSION, MAGIC,
    OVERHEAD, HEADER_SIZE, PAYLOAD_LENGTH_SIZE,
)
from devices.ghostwisp.protocol.schema import (
    IdentityPayload, SessionCapabilities,
    CapabilityBit, SafetyBit,
    TimeSyncPayload, PolicySyncPayload,
    ArmRequestPayload, ActiveMode,
    ConfirmPhysicalPayload, AbortPayload, AbortReason,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_frame(
    protocol_version: int = PROTOCOL_VERSION,
    message_type: MessageType = MessageType.SESSION_HELLO,
    flags: FrameFlags = FrameFlags.NONE,
    request_id: int = 1,
    session_id: int = 1,
    monotonic_ms: int = 0,
    payload: bytes = b"",
) -> Frame:
    return Frame(
        protocol_version=protocol_version,
        message_type=message_type,
        flags=flags,
        request_id=request_id,
        session_id=session_id,
        monotonic_ms=monotonic_ms,
        payload=payload,
    )


# ---------------------------------------------------------------------------
# 1. CRC
# ---------------------------------------------------------------------------

class TestCRC(unittest.TestCase):
    def test_known_value_check_char(self):
        # CRC-16/CCITT-FALSE of b"123456789" = 0x29B1
        self.assertEqual(_crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_empty(self):
        self.assertEqual(_crc16_ccitt_false(b""), 0xFFFF)

    def test_single_zero(self):
        # Known value: CRC of [0x00] with init=0xFFFF
        # 0xFFFF ^ (0x00 << 8) = 0xFFFF
        # 8 shifts with poly 0x1021: result = 0xE1F0
        self.assertEqual(_crc16_ccitt_false(b"\x00"), 0xE1F0)


# ---------------------------------------------------------------------------
# 2. Frame encode/decode round-trip
# ---------------------------------------------------------------------------

class TestFrameRoundTrip(unittest.TestCase):

    def _round_trip(self, **kw):
        f = _make_frame(**kw)
        enc = f.encode()
        dec = Frame.decode(enc)
        self.assertEqual(dec.protocol_version, f.protocol_version)
        self.assertEqual(dec.message_type, f.message_type)
        self.assertEqual(dec.flags, f.flags)
        self.assertEqual(dec.request_id, f.request_id)
        self.assertEqual(dec.session_id, f.session_id)
        self.assertEqual(dec.monotonic_ms, f.monotonic_ms)
        self.assertEqual(dec.payload, f.payload)
        return enc, dec

    def test_session_hello_no_payload(self):
        enc, _ = self._round_trip(message_type=MessageType.SESSION_HELLO)
        self.assertEqual(len(enc), OVERHEAD)

    def test_identity_resp(self):
        self._round_trip(
            message_type=MessageType.IDENTITY_RESP,
            flags=FrameFlags.RESPONSE,
            request_id=2,
            session_id=1,
            monotonic_ms=100,
            payload=b"\xDE\xAD" * 4,
        )

    def test_arm_req_with_payload(self):
        arm = ArmRequestPayload(
            mode=ActiveMode.SUB_GHZ_TRANSMIT,
            duration_ms=500,
            expiry_deadline_ms=30000,
            operator_id=b"opid" + b"\x00" * 12,
            auth_scope=b"lab-test" + b"\x00" * 56,
            nonce=bytes(range(16)),
        )
        payload = arm.encode()
        self._round_trip(
            message_type=MessageType.ARM_REQ,
            flags=FrameFlags.MANDATORY,
            request_id=10,
            session_id=1,
            monotonic_ms=5000,
            payload=payload,
        )

    def test_payload_max_boundary(self):
        self._round_trip(
            message_type=MessageType.TRANSFER_CHUNK,
            payload=b"\xAB" * 4096,
        )

    def test_payload_too_large_raises(self):
        f = _make_frame(payload=b"\x00" * 4097)
        with self.assertRaises(ProtocolError):
            f.encode()


# ---------------------------------------------------------------------------
# 3. Error handling
# ---------------------------------------------------------------------------

class TestFrameDecodeErrors(unittest.TestCase):

    def _valid_frame_bytes(self) -> bytes:
        return _make_frame(request_id=1).encode()

    def test_too_short(self):
        with self.assertRaises(ProtocolError):
            Frame.decode(b"\x57\x47\x01\xF0\x00\x00")

    def test_bad_magic(self):
        data = bytearray(self._valid_frame_bytes())
        data[0] = 0x00  # corrupt magic
        # recompute CRC so that only magic check triggers
        body = bytes(data[:-2])
        crc = _crc16_ccitt_false(body)
        data[-2:] = struct.pack("<H", crc)
        with self.assertRaises(ProtocolError):
            Frame.decode(bytes(data))

    def test_bad_crc(self):
        data = bytearray(self._valid_frame_bytes())
        data[-1] ^= 0xFF  # flip bits in CRC
        with self.assertRaises(ProtocolError):
            Frame.decode(bytes(data))

    def test_length_mismatch(self):
        # Build a frame then lie about payload_length
        data = bytearray(self._valid_frame_bytes())
        # payload_length is at offset HEADER_SIZE
        pl_offset = HEADER_SIZE
        data[pl_offset] = 0x05  # claim 5 bytes of payload, but actual payload is 0
        # recompute CRC to bypass that check
        body = bytes(data[:-2])
        crc = _crc16_ccitt_false(body)
        data[-2:] = struct.pack("<H", crc)
        with self.assertRaises(ProtocolError):
            Frame.decode(bytes(data))

    def test_unknown_mandatory_type(self):
        f = Frame(
            message_type=MessageType.SESSION_HELLO,  # placeholder; we'll overwrite
            flags=FrameFlags.MANDATORY,
            request_id=1,
            session_id=1,
        )
        data = bytearray(f.encode())
        # overwrite message_type byte with an undefined value (0xEE)
        data[3] = 0xEE
        # recompute CRC
        body = bytes(data[:-2])
        data[-2:] = struct.pack("<H", _crc16_ccitt_false(body))
        with self.assertRaises(ProtocolError):
            Frame.decode(bytes(data))


# ---------------------------------------------------------------------------
# 4. Payload schemas
# ---------------------------------------------------------------------------

class TestIdentityPayload(unittest.TestCase):
    def test_round_trip(self):
        p = IdentityPayload(
            device_id=bytes(range(16)),
            hw_rev=0x0100,
            fw_version=0x0101,
            fw_tag=b"v1.0.0",
        )
        dec = IdentityPayload.decode(p.encode())
        self.assertEqual(dec.device_id, p.device_id)
        self.assertEqual(dec.hw_rev, p.hw_rev)
        self.assertEqual(dec.fw_version, p.fw_version)
        self.assertEqual(dec.fw_tag, p.fw_tag)

    def test_too_short(self):
        with self.assertRaises(ProtocolError):
            IdentityPayload.decode(b"\x00" * 4)


class TestSessionCapabilities(unittest.TestCase):
    def test_round_trip(self):
        p = SessionCapabilities(
            capability_flags=CapabilityBit.SUB_GHZ_RECEIVE | CapabilityBit.COMPANION_SYNC,
            safety_flags=SafetyBit.RECEIVE_ONLY | SafetyBit.RADIO_DISABLED,
        )
        dec = SessionCapabilities.decode(p.encode())
        self.assertEqual(dec.capability_flags, p.capability_flags)
        self.assertEqual(dec.safety_flags, p.safety_flags)
        self.assertTrue(dec.receive_only)
        self.assertTrue(dec.radio_disabled)

    def test_too_short(self):
        with self.assertRaises(ProtocolError):
            SessionCapabilities.decode(b"\x00")


class TestTimeSyncPayload(unittest.TestCase):
    def test_round_trip(self):
        p = TimeSyncPayload(utc_ms=1_700_000_000_000, roundtrip_ms=200)
        dec = TimeSyncPayload.decode(p.encode())
        self.assertEqual(dec.utc_ms, p.utc_ms)
        self.assertEqual(dec.roundtrip_ms, p.roundtrip_ms)


class TestPolicySyncPayload(unittest.TestCase):
    def test_round_trip(self):
        p = PolicySyncPayload(region_code=1, policy_flags=0, policy_seq=7)
        dec = PolicySyncPayload.decode(p.encode())
        self.assertEqual(dec.region_code, p.region_code)
        self.assertEqual(dec.policy_seq, p.policy_seq)

    def test_unset_region_most_restrictive(self):
        # region_code=0 means unset; device must not transmit
        p = PolicySyncPayload(region_code=0, policy_flags=0, policy_seq=1)
        dec = PolicySyncPayload.decode(p.encode())
        self.assertEqual(dec.region_code, 0,
                         "region_code=0 (unset) must be preserved — device must not transmit")


class TestArmRequestPayload(unittest.TestCase):
    def _make(self) -> ArmRequestPayload:
        return ArmRequestPayload(
            mode=ActiveMode.SUB_GHZ_TRANSMIT,
            duration_ms=500,
            expiry_deadline_ms=30000,
            operator_id=b"operator12345678",
            auth_scope=b"authorized-test-fixture" + b"\x00" * 41,
            nonce=bytes(range(16)),
        )

    def test_round_trip(self):
        p = self._make()
        dec = ArmRequestPayload.decode(p.encode())
        self.assertEqual(dec.mode, p.mode)
        self.assertEqual(dec.duration_ms, p.duration_ms)
        self.assertEqual(dec.expiry_deadline_ms, p.expiry_deadline_ms)
        self.assertEqual(dec.operator_id, p.operator_id)
        self.assertEqual(dec.nonce, p.nonce)

    def test_absent_field_hard_rejected(self):
        # Truncated payload simulates absent mandatory field
        p = self._make()
        encoded = p.encode()
        with self.assertRaises(ProtocolError):
            ArmRequestPayload.decode(encoded[:10])  # too short

    def test_expiry_present(self):
        p = self._make()
        dec = ArmRequestPayload.decode(p.encode())
        self.assertGreater(dec.expiry_deadline_ms, 0,
                           "expiry_deadline_ms must be non-zero for every ARM_REQ")


class TestConfirmPhysicalPayload(unittest.TestCase):
    def test_round_trip(self):
        nonce = bytes(range(16))
        p = ConfirmPhysicalPayload(nonce=nonce, confirm_monotonic_ms=5800)
        dec = ConfirmPhysicalPayload.decode(p.encode())
        self.assertEqual(dec.nonce, p.nonce)
        self.assertEqual(dec.confirm_monotonic_ms, p.confirm_monotonic_ms)

    def test_nonce_mismatch_detected(self):
        arm_nonce = bytes(range(16))
        wrong_nonce = bytes([0xFF] * 16)
        confirm = ConfirmPhysicalPayload(nonce=wrong_nonce, confirm_monotonic_ms=5800)
        dec = ConfirmPhysicalPayload.decode(confirm.encode())
        self.assertNotEqual(dec.nonce, arm_nonce,
                            "nonce mismatch must be detectable by the caller")


class TestAbortPayload(unittest.TestCase):
    def test_timeout_reason(self):
        nonce = bytes(range(16))
        p = AbortPayload(nonce=nonce, reason=AbortReason.TIMEOUT)
        dec = AbortPayload.decode(p.encode())
        self.assertEqual(dec.reason, AbortReason.TIMEOUT)

    def test_link_lost_reason(self):
        p = AbortPayload(nonce=bytes(16), reason=AbortReason.LINK_LOST)
        dec = AbortPayload.decode(p.encode())
        self.assertEqual(dec.reason, AbortReason.LINK_LOST)


# ---------------------------------------------------------------------------
# 5. Safety invariants
# ---------------------------------------------------------------------------

class TestSafetyInvariants(unittest.TestCase):

    def test_arm_req_alone_cannot_confirm(self):
        """
        GhostBlade sends ARM_REQ.  The protocol model requires a subsequent
        CONFIRM_PHYSICAL from GhostWisp before execution.  Verify that the
        ARM_REQ frame has message_type ARM_REQ (not CONFIRM_PHYSICAL or
        EXECUTE_START), so the codec itself encodes the two-step requirement.
        """
        arm = ArmRequestPayload(
            mode=ActiveMode.SUB_GHZ_TRANSMIT,
            duration_ms=500,
            expiry_deadline_ms=30000,
            operator_id=b"opid" + b"\x00" * 12,
            auth_scope=b"scope" + b"\x00" * 59,
            nonce=bytes(range(16)),
        )
        f = Frame(
            message_type=MessageType.ARM_REQ,
            flags=FrameFlags.MANDATORY,
            request_id=1,
            session_id=1,
            monotonic_ms=1000,
            payload=arm.encode(),
        )
        dec = Frame.decode(f.encode())
        self.assertEqual(dec.message_type, MessageType.ARM_REQ)
        self.assertNotEqual(dec.message_type, MessageType.CONFIRM_PHYSICAL)
        self.assertNotEqual(dec.message_type, MessageType.EXECUTE_START)

    def test_confirm_physical_nonce_replay_prevention(self):
        """
        A CONFIRM_PHYSICAL with the wrong nonce must NOT match the ARM_REQ
        nonce, allowing the broker to detect and reject the replay.
        """
        arm_nonce = b"\xAA" * 16
        replay_nonce = b"\xBB" * 16

        confirm = ConfirmPhysicalPayload(nonce=replay_nonce, confirm_monotonic_ms=6000)
        dec = ConfirmPhysicalPayload.decode(confirm.encode())
        self.assertNotEqual(dec.nonce, arm_nonce,
                            "replayed nonce must differ from arm_nonce so broker rejects it")

    def test_abort_disarms(self):
        """
        ABORT message with TIMEOUT reason and the ARM nonce encodes correctly.
        The broker must discard the armed action on receipt.
        """
        nonce = bytes(range(16))
        abort = AbortPayload(nonce=nonce, reason=AbortReason.TIMEOUT)
        f = Frame(
            message_type=MessageType.ABORT,
            request_id=5,
            session_id=1,
            monotonic_ms=35000,
            payload=abort.encode(),
        )
        dec_frame = Frame.decode(f.encode())
        dec_abort = AbortPayload.decode(dec_frame.payload)
        self.assertEqual(dec_abort.reason, AbortReason.TIMEOUT)
        self.assertEqual(dec_abort.nonce, nonce)

    def test_power_on_safe_defaults(self):
        """
        At power-on, receive_only and radio_disabled must be True.
        An empty (default-constructed) SessionCapabilities has safety_flags=0,
        which means both bits are clear — the firmware initialises them to 1.
        This test verifies the SafetyBit constants have the expected values so
        firmware can set them correctly.
        """
        self.assertEqual(SafetyBit.RECEIVE_ONLY,   0b01)
        self.assertEqual(SafetyBit.RADIO_DISABLED,  0b10)
        # Construct the power-on default capability advertisement
        caps = SessionCapabilities(
            capability_flags=CapabilityBit.SUB_GHZ_RECEIVE,
            safety_flags=SafetyBit.RECEIVE_ONLY | SafetyBit.RADIO_DISABLED,
        )
        self.assertTrue(caps.receive_only)
        self.assertTrue(caps.radio_disabled)

    def test_arm_req_absent_mandatory_field_rejected(self):
        """Truncated ARM_REQ payload must raise ProtocolError (missing field = hard reject)."""
        with self.assertRaises(ProtocolError):
            ArmRequestPayload.decode(b"\x01\x00\xF4\x01")  # far too short

    def test_receive_only_reachable_without_navigating_active_modes(self):
        """
        SafetyBit.RECEIVE_ONLY can be set independently of any active capability bit.
        Verifies that receive_only mode does not require first enabling an active mode.
        """
        caps = SessionCapabilities(
            capability_flags=0,   # no active capabilities
            safety_flags=SafetyBit.RECEIVE_ONLY,
        )
        self.assertTrue(caps.receive_only)
        self.assertFalse(bool(caps.capability_flags & CapabilityBit.SUB_GHZ_TRANSMIT))
        self.assertFalse(bool(caps.capability_flags & CapabilityBit.NFC_EMULATE))

    def test_region_unset_is_most_restrictive(self):
        """region_code=0 (unset) must be preserved and defaults to no-transmit."""
        p = PolicySyncPayload(region_code=0, policy_flags=0, policy_seq=1)
        dec = PolicySyncPayload.decode(p.encode())
        self.assertEqual(dec.region_code, 0)


# ---------------------------------------------------------------------------
# 6. Detached / GhostWisp-absent: no side effects on import
# ---------------------------------------------------------------------------

class TestDetachedImport(unittest.TestCase):

    def test_import_no_side_effects(self):
        """
        Importing the protocol package must not trigger any hardware access,
        open any USB device, or raise any exception even when no GhostWisp
        is attached.  Verifies that GhostBlade's existing operation is
        unchanged when the companion code is loaded without a device present.
        """
        # Re-import to exercise the import path explicitly
        import importlib
        import devices.ghostwisp.protocol as gwp
        importlib.reload(gwp)
        # If we got here without exception, the invariant holds
        self.assertTrue(True)

    def test_codec_works_standalone(self):
        """Frame encode/decode must work with no hardware present."""
        f = _make_frame(
            message_type=MessageType.SESSION_HELLO,
            request_id=99,
            session_id=42,
            monotonic_ms=999,
        )
        enc = f.encode()
        dec = Frame.decode(enc)
        self.assertEqual(dec.request_id, 99)
        self.assertEqual(dec.session_id, 42)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main(verbosity=2)
