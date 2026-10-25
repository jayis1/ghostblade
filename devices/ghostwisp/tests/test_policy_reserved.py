"""Host-only policy-sync reserved-word regressions.

Author: jayis1

These tests validate payload parsing, not authentication or policy enforcement.
"""

import struct
import unittest

from devices.ghostwisp.protocol.frame import Frame, MessageType, ProtocolError
from devices.ghostwisp.protocol.schema import PolicySyncPayload


class TestPolicyReserved(unittest.TestCase):
    @staticmethod
    def policy_wire(reserved=0, region=1, flags=1, sequence=42):
        # Independent peer serialization; do not rely on the local encoder to
        # construct malformed input. The reserved word occupies bytes 2–3.
        return struct.pack("<BBHI", region, flags, reserved, sequence)

    def test_every_nonzero_reserved_word_is_rejected(self):
        for reserved in range(1, 0x10000):
            wire = self.policy_wire(reserved=reserved)
            for buffer_type in (bytes, bytearray):
                with self.subTest(reserved=reserved, buffer_type=buffer_type):
                    with self.assertRaisesRegex(
                            ProtocolError, "PolicySyncPayload: reserved word must be zero"):
                        PolicySyncPayload.decode(buffer_type(wire))

    def test_valid_frame_crc_does_not_authorize_reserved_policy_data(self):
        for message_type in (MessageType.POLICY_SYNC_REQ, MessageType.POLICY_SYNC_RESP):
            for reserved in (1, 0x100, 0x8000, 0xFFFF):
                with self.subTest(message_type=message_type, reserved=reserved):
                    wire = Frame(message_type=message_type,
                                 payload=self.policy_wire(reserved=reserved)).encode()
                    payload = Frame.decode(wire).payload
                    with self.assertRaisesRegex(ProtocolError, "reserved word must be zero"):
                        PolicySyncPayload.decode(payload)

    def test_conforming_payloads_keep_existing_wire_values(self):
        # Unknown regions/flags are deliberately preserved: the policy layer,
        # not this structural check, decides their restrictive interpretation.
        for region in (0, 1, 2, 3, 0xFF):
            for flags in (0, 1, 0x80, 0xFF):
                for sequence in (0, 42, 0xFFFFFFFF):
                    wire = self.policy_wire(region=region, flags=flags, sequence=sequence)
                    for buffer_type in (bytes, bytearray):
                        with self.subTest(region=region, flags=flags, sequence=sequence,
                                          buffer_type=buffer_type):
                            parsed = PolicySyncPayload.decode(buffer_type(wire))
                            self.assertEqual(parsed, PolicySyncPayload(region, flags, sequence))
                            self.assertEqual(parsed.encode(), wire)

    def test_every_truncated_prefix_raises_protocol_error(self):
        wire = self.policy_wire()
        self.assertEqual(len(wire), PolicySyncPayload.SIZE)
        for length in range(len(wire)):
            for buffer_type in (bytes, bytearray):
                with self.subTest(length=length, buffer_type=buffer_type):
                    with self.assertRaisesRegex(ProtocolError, "PolicySyncPayload: expected"):
                        PolicySyncPayload.decode(buffer_type(wire[:length]))

    def test_existing_following_bytes_behavior_is_unchanged(self):
        for buffer_type in (bytes, bytearray):
            with self.subTest(buffer_type=buffer_type):
                parsed = PolicySyncPayload.decode(buffer_type(self.policy_wire() + b"following"))
                self.assertEqual(parsed, PolicySyncPayload(1, 1, 42))
                with self.assertRaisesRegex(ProtocolError, "reserved word must be zero"):
                    PolicySyncPayload.decode(buffer_type(
                        self.policy_wire(reserved=1) + b"following"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
