"""Profile wire-envelope regressions. Author: jayis1.

Exercise the shipped decoder, including malformed headers inside valid frames.
These are host checks, not signature verification or hardware tests.
"""

import struct
import unittest

from devices.ghostwisp.protocol.frame import Frame, MessageType, ProtocolError
from devices.ghostwisp.protocol.profile import ProfileHeader, ProfileValidator


class TestProfileReservedFields(unittest.TestCase):
    def setUp(self):
        self.header = ProfileHeader(
            profile_id=b"bounded-profile",
            schema_version=1,
            profile_flags=3,
            region_code=0,
            max_duration_ms=1000,
            expiry_utc_ms=0,
            body_length=0,
            body_crc32=0,
        )
        self.wire = self.header.encode()
        # Explicit little-endian wire offsets, independent of private constants.
        self.reserved_offset = struct.calcsize("<16sHHB")
        self.reserved2_offset = struct.calcsize("<16sHHBB")

    def test_reserved_byte_rejects_every_nonzero_value(self):
        for value in range(1, 256):
            for buffer_type in (bytes, bytearray):
                with self.subTest(value=value, buffer_type=buffer_type):
                    wire = bytearray(self.wire)
                    wire[self.reserved_offset] = value
                    with self.assertRaisesRegex(ProtocolError, "reserved byte must be zero"):
                        ProfileHeader.decode(buffer_type(wire))

    def test_reserved_word_rejects_each_bit_and_all_bits(self):
        for value in [1 << bit for bit in range(16)] + [0xFFFF]:
            for buffer_type in (bytes, bytearray):
                with self.subTest(value=value, buffer_type=buffer_type):
                    wire = bytearray(self.wire)
                    struct.pack_into("<H", wire, self.reserved2_offset, value)
                    with self.assertRaisesRegex(ProtocolError, "reserved word must be zero"):
                        ProfileHeader.decode(buffer_type(wire))

    def test_valid_frame_crc_does_not_authorize_reserved_profile_data(self):
        for offset in (self.reserved_offset, self.reserved2_offset):
            with self.subTest(offset=offset):
                wire = bytearray(self.wire)
                wire[offset] = 1
                frame = Frame(message_type=MessageType.PROFILE_INDEX_RESP, payload=bytes(wire))
                decoded = Frame.decode(frame.encode())
                with self.assertRaisesRegex(ProtocolError, "must be zero"):
                    ProfileHeader.decode(decoded.payload)

    def test_all_truncated_header_prefixes_raise_protocol_error(self):
        for length in range(len(self.wire)):
            with self.subTest(length=length):
                with self.assertRaises(ProtocolError):
                    ProfileHeader.decode(self.wire[:length])

    def test_zero_reserved_header_preserves_following_body_and_flags(self):
        # Unknown flag bits remain preserved by the existing profile contract.
        self.header.profile_flags = 0xFFFF
        self.header.body_length = 4
        wire = self.header.encode() + b"body"
        decoded = ProfileHeader.decode(wire)
        self.assertEqual(decoded, self.header)
        self.assertTrue(ProfileValidator.validate(decoded).ok)
        self.assertEqual(wire[decoded.SIZE:], b"body")


if __name__ == "__main__":
    unittest.main()
