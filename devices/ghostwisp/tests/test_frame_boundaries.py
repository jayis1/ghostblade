"""Host-only frame integrity and exact-envelope regressions.

Author: jayis1

Exercises the production codec, not a transport or authenticated session.
"""

import binascii
import struct
import unittest

from devices.ghostwisp.protocol.frame import (
    Frame, MessageType, OVERHEAD, PAYLOAD_MAX_DEFAULT, ProtocolError,
    _crc16_ccitt_false,
)


class TestFrameBoundaries(unittest.TestCase):
    def test_crc_matches_independent_standard_library_implementation(self):
        for payload in (b"", b"123456789", bytes(range(256)),
                        bytes(range(256)) * 16):
            with self.subTest(size=len(payload)):
                self.assertEqual(_crc16_ccitt_false(payload),
                                 binascii.crc_hqx(payload, 0xFFFF))
                wire = Frame(payload=payload).encode()
                self.assertEqual(struct.unpack("<H", wire[-2:])[0],
                                 binascii.crc_hqx(wire[:-2], 0xFFFF))

    def test_every_truncated_prefix_is_rejected(self):
        # Include minimum, ordinary, and maximum wire lengths. Short prefixes
        # fail before CRC work; longer prefixes still cannot be full frames.
        for size in (0, 32, PAYLOAD_MAX_DEFAULT):
            wire = Frame(message_type=MessageType.TRANSFER_CHUNK,
                         payload=b"\xAB" * size).encode()
            for length in range(len(wire)):
                for input_type in (bytes, bytearray):
                    with self.subTest(size=size, length=length,
                                      input_type=input_type):
                        with self.assertRaises(ProtocolError):
                            Frame.decode(input_type(wire[:length]))
            self.assertEqual(Frame.decode(wire).payload, b"\xAB" * size)

    def test_crc_valid_trailing_bytes_are_not_silently_ignored(self):
        # Recompute CRC independently so rejection must enforce the declared
        # payload length, rather than merely notice a stale checksum.
        for payload in (b"", b"ordinary"):
            original = Frame(payload=payload).encode()
            for suffix in (b"\x00", b"\x57\x47", Frame().encode()):
                body = original[:-2] + suffix
                wire = body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))
                for input_type in (bytes, bytearray):
                    with self.subTest(payload=payload, suffix=suffix,
                                      input_type=input_type):
                        with self.assertRaisesRegex(ProtocolError, "length mismatch"):
                            Frame.decode(input_type(wire))

    def test_each_single_bit_corruption_is_rejected(self):
        wire = Frame(payload=bytes(range(32))).encode()
        self.assertEqual(len(wire), OVERHEAD + 32)
        for offset in range(len(wire)):
            for bit in range(8):
                corrupted = bytearray(wire)
                corrupted[offset] ^= 1 << bit
                with self.subTest(offset=offset, bit=bit):
                    with self.assertRaisesRegex(ProtocolError, "CRC mismatch"):
                        Frame.decode(corrupted)


if __name__ == "__main__":
    unittest.main(verbosity=2)
