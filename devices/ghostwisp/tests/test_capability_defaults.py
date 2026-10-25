"""GhostWisp safe capability defaults and wire compatibility.

Author: jayis1
"""

import struct
import unittest

from devices.ghostwisp.protocol.frame import Frame, MessageType, ProtocolError
from devices.ghostwisp.protocol.schema import (
    CapabilityBit,
    SafetyBit,
    SessionCapabilities,
)


class TestCapabilityDefaults(unittest.TestCase):
    def test_default_instance_is_receive_only_and_radio_disabled(self):
        caps = SessionCapabilities()
        self.assertEqual(caps.capability_flags, 0)
        self.assertEqual(
            caps.safety_flags, SafetyBit.RECEIVE_ONLY | SafetyBit.RADIO_DISABLED
        )
        self.assertTrue(caps.receive_only)
        self.assertTrue(caps.radio_disabled)

    def test_setting_capabilities_does_not_remove_default_restrictions(self):
        caps = SessionCapabilities(capability_flags=CapabilityBit.COMPANION_SYNC)
        self.assertTrue(caps.receive_only)
        self.assertTrue(caps.radio_disabled)

    def test_default_flags_survive_crc_checked_frame_round_trip(self):
        original = SessionCapabilities()
        wire = Frame(
            message_type=MessageType.CAPABILITIES_RESP,
            payload=original.encode(),
        ).encode()
        decoded = SessionCapabilities.decode(Frame.decode(wire).payload)
        self.assertEqual(decoded, original)
        self.assertEqual(decoded.encode(), struct.pack('<HH', 0, 3))
        self.assertTrue(decoded.receive_only)
        self.assertTrue(decoded.radio_disabled)

    def test_explicit_flags_keep_existing_wire_meaning(self):
        # Includes zero, individual defined bits, and unknown/reserved bits.
        for flags in (0, 1, 2, 3, 4, 8, 15, 0x8000, 0xFFFF):
            with self.subTest(flags=flags):
                wire = struct.pack('<HH', 0xFFFF, flags)
                caps = SessionCapabilities.decode(wire)
                self.assertEqual(caps.capability_flags, 0xFFFF)
                self.assertEqual(caps.safety_flags, flags)
                self.assertEqual(caps.encode(), wire)
                self.assertEqual(
                    SessionCapabilities(0xFFFF, flags).encode(), wire
                )
                self.assertEqual(caps.receive_only, bool(flags & 1))
                self.assertEqual(caps.radio_disabled, bool(flags & 2))

    def test_truncated_payload_cannot_fall_back_to_defaults(self):
        wire = SessionCapabilities().encode()
        for length in range(SessionCapabilities.SIZE):
            with self.subTest(length=length):
                with self.assertRaises(ProtocolError):
                    SessionCapabilities.decode(wire[:length])


if __name__ == '__main__':
    unittest.main()
