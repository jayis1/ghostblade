"""Complete profile body integrity regressions. Author: jayis1."""

import dataclasses
import unittest
from unittest import mock

from devices.ghostwisp.protocol.profile import (
    MAX_PROFILE_BODY_BYTES, ProfileFlags, ProfileHeader, ProfileValidator,
)


class ProfileBodyTests(unittest.TestCase):
    def header(self, **changes):
        # Independent CRC-32/ISO-HDLC check value for b"123456789".
        header = ProfileHeader(
            profile_id=bytes(range(16)), schema_version=1,
            profile_flags=ProfileFlags.NO_TRANSMIT, region_code=0,
            max_duration_ms=1000, expiry_utc_ms=0,
            body_length=9, body_crc32=0xCBF43926,
        )
        return dataclasses.replace(header, **changes)

    def test_known_answer_and_mutable_body(self):
        for factory in (bytes, bytearray):
            with self.subTest(factory=factory):
                body = factory(b"123456789")
                self.assertTrue(ProfileValidator.validate_body(self.header(), body).ok)
                self.assertEqual(body, b"123456789")

    def test_empty_body(self):
        self.assertTrue(ProfileValidator.validate_body(
            self.header(body_length=0, body_crc32=0), b"",
        ).ok)

    def test_maximum_body(self):
        # CRC of 50 KiB of zero bytes, independently calculated in the test.
        import binascii
        body = bytes(MAX_PROFILE_BODY_BYTES)
        header = self.header(body_length=len(body), body_crc32=binascii.crc32(body))
        self.assertTrue(ProfileValidator.validate_body(header, body).ok)

    def test_every_truncated_prefix_and_trailing_data(self):
        for size in range(9):
            with self.subTest(size=size):
                self.assertFalse(ProfileValidator.validate_body(
                    self.header(), b"123456789"[:size],
                ).ok)
        self.assertFalse(ProfileValidator.validate_body(
            self.header(), b"123456789\x00",
        ).ok)

    def test_every_body_bit_corruption(self):
        for bit in range(72):
            with self.subTest(bit=bit):
                body = bytearray(b"123456789")
                body[bit // 8] ^= 1 << (bit % 8)
                result = ProfileValidator.validate_body(self.header(), body)
                self.assertFalse(result.ok)
                self.assertIn("CRC", result.reject_reason)

    def test_wrong_checksum(self):
        self.assertFalse(ProfileValidator.validate_body(
            self.header(body_crc32=0), b"123456789",
        ).ok)

    def test_invalid_headers_rejected_before_checksum(self):
        changes = (
            {"schema_version": 2}, {"region_code": 255},
            {"body_length": MAX_PROFILE_BODY_BYTES + 1},
            {"profile_id": bytes(16)}, {"expiry_utc_ms": 100},
        )
        with mock.patch("devices.ghostwisp.protocol.profile.binascii.crc32") as crc:
            for change in changes:
                with self.subTest(change=change):
                    header = self.header(**change)
                    expected = ProfileValidator.validate(header, now_utc_ms=100)
                    result = ProfileValidator.validate_body(header, b"123456789", now_utc_ms=100)
                    self.assertEqual(result, expected)
            crc.assert_not_called()

    def test_mismatched_length_rejected_before_checksum(self):
        with mock.patch("devices.ghostwisp.protocol.profile.binascii.crc32") as crc:
            for body in (b"", b"1234567890", bytes(MAX_PROFILE_BODY_BYTES + 1)):
                with self.subTest(size=len(body)):
                    self.assertFalse(ProfileValidator.validate_body(self.header(), body).ok)
            crc.assert_not_called()

    def test_expiry_and_no_expiry_conventions_unchanged(self):
        header = self.header(expiry_utc_ms=100)
        self.assertTrue(ProfileValidator.validate_body(header, b"123456789", now_utc_ms=99).ok)
        self.assertFalse(ProfileValidator.validate_body(header, b"123456789", now_utc_ms=100).ok)
        self.assertTrue(ProfileValidator.validate_body(header, b"123456789", now_utc_ms=0).ok)


if __name__ == "__main__":
    unittest.main()
