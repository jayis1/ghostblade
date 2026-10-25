"""Profile length bounds at the public validation boundary. Author: jayis1."""

import dataclasses
import unittest
from unittest import mock

from devices.ghostwisp.protocol.profile import (
    MAX_PROFILE_BODY_BYTES, ProfileHeader, ProfileValidator,
)


class UnreadableBody(bytearray):
    def __len__(self):
        raise AssertionError("invalid header must be rejected before body access")


class ProfileLengthBoundsTests(unittest.TestCase):
    def header(self, length):
        return ProfileHeader(
            profile_id=b"bounded-profile", schema_version=1,
            profile_flags=3, region_code=0, max_duration_ms=0,
            expiry_utc_ms=0, body_length=length, body_crc32=0,
        )

    def test_negative_lengths_rejected_by_header_validator(self):
        # Locally constructed/replaced headers do not pass through uint32 unpack.
        for length in (-1, -MAX_PROFILE_BODY_BYTES, -(1 << 64)):
            with self.subTest(length=length):
                result = ProfileValidator.validate(self.header(length))
                self.assertFalse(result.ok)
                self.assertIn("body_length", result.reject_reason or "")

    def test_negative_lengths_rejected_before_body_access_or_checksum(self):
        for length in (-1, -(1 << 64)):
            with self.subTest(length=length):
                header = self.header(length)
                with mock.patch(
                    "devices.ghostwisp.protocol.profile.binascii.crc32"
                ) as crc:
                    # The invalid header must be rejected before len(body).
                    result = ProfileValidator.validate_body(header, UnreadableBody())
                    self.assertEqual(result, ProfileValidator.validate(header))
                    self.assertFalse(result.ok)
                    crc.assert_not_called()

    def test_size_boundaries_and_wire_round_trip(self):
        for length in (0, 1, MAX_PROFILE_BODY_BYTES):
            with self.subTest(length=length):
                header = self.header(length)
                self.assertTrue(ProfileValidator.validate(header).ok)
                decoded = ProfileHeader.decode(header.encode())
                self.assertEqual(decoded, header)
                self.assertTrue(ProfileValidator.validate(decoded).ok)
        for length in (MAX_PROFILE_BODY_BYTES + 1, (1 << 32) - 1):
            with self.subTest(length=length):
                header = ProfileHeader.decode(self.header(length).encode())
                self.assertFalse(ProfileValidator.validate(header).ok)

    def test_replaced_decoded_header_is_revalidated(self):
        header = ProfileHeader.decode(self.header(0).encode())
        changed = dataclasses.replace(header, body_length=-1)
        self.assertFalse(ProfileValidator.validate(changed).ok)
        self.assertTrue(ProfileValidator.validate(header).ok)


if __name__ == "__main__":
    unittest.main()
