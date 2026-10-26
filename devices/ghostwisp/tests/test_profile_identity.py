"""Profile identity wire-width regressions. Author: jayis1."""

import dataclasses
import unittest
from unittest import mock

from devices.ghostwisp.protocol.frame import Frame, MessageType, ProtocolError
from devices.ghostwisp.protocol.profile import ProfileHeader, ProfileValidator
from devices.ghostwisp.tests.test_profile import _make_header


class TestProfileIdentity(unittest.TestCase):
    def test_oversized_ids_rejected_without_truncation(self):
        # Include an ID whose only nonzero byte would be discarded on the wire.
        for identity in (b"a" * 17, b"a" * 256, bytes(16) + b"x"):
            with self.subTest(length=len(identity), identity=identity):
                header = _make_header(profile_id=identity, body_length=0, body_crc32=0)
                result = ProfileValidator.validate(header)
                self.assertFalse(result.ok)
                self.assertIn("profile_id", result.reject_reason or "")
                with self.assertRaisesRegex(ProtocolError, "profile_id"):
                    header.encode()
                with mock.patch("devices.ghostwisp.protocol.profile.binascii.crc32") as crc:
                    result = ProfileValidator.validate_body(header, b"")
                    self.assertFalse(result.ok)
                    self.assertIn("profile_id", result.reject_reason or "")
                    crc.assert_not_called()
                self.assertEqual(header.profile_id, identity)

    def test_different_oversized_ids_cannot_alias(self):
        header = _make_header(profile_id=b"a" * 16 + b"1")
        other = dataclasses.replace(header, profile_id=b"a" * 16 + b"2")
        for item in (header, other):
            with self.assertRaises(ProtocolError):
                item.encode()

    def test_existing_short_and_full_width_ids_keep_wire_bytes(self):
        for size in range(1, 17):
            identity = bytes(range(1, size + 1))
            with self.subTest(size=size):
                header = _make_header(profile_id=identity)
                self.assertTrue(ProfileValidator.validate(header).ok)
                encoded = header.encode()
                self.assertEqual(encoded[:16], identity.ljust(16, b"\0"))
                frame = Frame(message_type=MessageType.PROFILE_INDEX_RESP, payload=encoded)
                decoded = ProfileHeader.decode(Frame.decode(frame.encode()).payload)
                self.assertEqual(decoded.profile_id, identity)
                self.assertEqual(decoded.encode(), encoded)

    def test_trailing_zero_and_unset_id_conventions_unchanged(self):
        for identity in (b"x\0", b"x" + bytes(15)):
            header = _make_header(profile_id=identity)
            decoded = ProfileHeader.decode(header.encode())
            self.assertEqual(decoded.profile_id, b"x")
            self.assertEqual(decoded.encode(), header.encode())
        for identity in (b"", bytes(1), bytes(16)):
            header = _make_header(profile_id=identity)
            self.assertFalse(ProfileValidator.validate(header).ok)
            self.assertFalse(ProfileValidator.validate(ProfileHeader.decode(header.encode())).ok)


if __name__ == "__main__":
    unittest.main()
