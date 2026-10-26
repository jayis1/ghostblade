"""Stable discovery identities must fit their existing wire field.

Author: jayis1
"""

import struct
import unittest

from devices.ghostwisp.protocol.frame import Frame, MessageType, ProtocolError
from devices.ghostwisp.protocol.schema import IdentityPayload


class DeviceIdentityWidthTests(unittest.TestCase):
    def identity(self, device_id):
        return IdentityPayload(device_id, 0x0102, 0x0304, b"width-test")

    def test_oversized_ids_are_rejected_not_collapsed(self):
        prefix = b"0123456789abcdef"
        for length in (17, 18, 32, 4096):
            for suffix in (b"A", b"B", b"\x00"):
                device_id = prefix + suffix * (length - len(prefix))
                with self.subTest(length=length, suffix=suffix):
                    with self.assertRaisesRegex(ProtocolError, "device_id.*16-byte"):
                        self.identity(device_id).encode()

    def test_conforming_ids_keep_exact_wire_bytes(self):
        # Independent layout oracle; include legacy empty/short-ID padding.
        for length in range(17):
            device_id = bytes(range(1, length + 1))
            with self.subTest(length=length):
                expected = struct.pack(
                    "<16sHH32s", device_id, 0x0102, 0x0304, b"width-test"
                )
                wire = self.identity(device_id).encode()
                self.assertEqual(wire, expected)
                parsed = IdentityPayload.decode(wire)
                self.assertEqual(parsed.device_id, device_id.ljust(16, b"\x00"))
                self.assertEqual(parsed.encode(), expected)

    def test_full_width_id_distinctions_survive_discovery_frames(self):
        prefix = b"0123456789abcde"
        for kind in (MessageType.SESSION_HELLO, MessageType.IDENTITY_REQ,
                     MessageType.IDENTITY_RESP):
            for last in range(256):
                device_id = prefix + bytes([last])
                with self.subTest(kind=kind, last=last):
                    payload = self.identity(device_id).encode()
                    wire = Frame(message_type=kind, payload=payload).encode()
                    for buffer_type in (bytes, bytearray):
                        decoded_frame = Frame.decode(buffer_type(wire))
                        parsed = IdentityPayload.decode(decoded_frame.payload)
                        self.assertEqual(parsed.device_id, device_id)
                        self.assertEqual(parsed.encode(), payload)

    def test_tag_truncation_remains_unchanged(self):
        identity = self.identity(b"0123456789abcdef")
        identity.fw_tag = b"t" * 33
        self.assertEqual(IdentityPayload.decode(identity.encode()).fw_tag, b"t" * 32)


if __name__ == "__main__":
    unittest.main()
