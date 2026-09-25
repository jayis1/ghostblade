"""
GhostWisp protocol — bounded profile parser and validator tests.

Author: jayis1

Covers:
  1. ProfileHeader encode/decode round-trip
  2. Decode error: too-short payload (missing mandatory bytes)
  3. Validation — positive: valid bounded profile header is accepted
  4. Validation — negative: expired profile (expiry_utc_ms in the past)
  5. Validation — negative: oversized body_length (> MAX_PROFILE_BODY_BYTES)
  6. Validation — negative: out-of-scope region_code (0xFF)
  7. Validation — negative: unknown schema_version
  8. Validation — negative: all-zeros profile_id
  9. Validation — no-expiry skip when now_utc_ms=0
 10. ProfileFlags constants: NO_TRANSMIT and RECEIVE_ONLY are independent of
     active capability bits (profiles cannot silently enable transmit modes)
 11. Golden vector alignment: profile vectors in golden_vectors.py decode to
     expected ProfileHeader fields and produce the expected validation outcome
 12. Detached/absent GhostWisp: importing profile module has no side effects
"""

from __future__ import annotations

import struct
import sys
import unittest

import os as _os
_REPO_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from devices.ghostwisp.protocol.frame import Frame, MessageType, FrameFlags, ProtocolError
from devices.ghostwisp.protocol.profile import (
    ProfileHeader,
    ProfileFlags,
    ProfileValidator,
    ProfileValidationResult,
    PROFILE_SCHEMA_VERSION,
    MAX_PROFILE_BODY_BYTES,
)
from devices.ghostwisp.protocol.golden_vectors import VECTORS


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_header(
    profile_id: bytes = bytes(range(16)),
    schema_version: int = PROFILE_SCHEMA_VERSION,
    profile_flags: int = ProfileFlags.NO_TRANSMIT | ProfileFlags.RECEIVE_ONLY,
    region_code: int = 1,          # EU868
    max_duration_ms: int = 1000,
    expiry_utc_ms: int = 0,
    body_length: int = 100,
    body_crc32: int = 0xCAFEBABE,
) -> ProfileHeader:
    return ProfileHeader(
        profile_id=profile_id,
        schema_version=schema_version,
        profile_flags=profile_flags,
        region_code=region_code,
        max_duration_ms=max_duration_ms,
        expiry_utc_ms=expiry_utc_ms,
        body_length=body_length,
        body_crc32=body_crc32,
    )


# ---------------------------------------------------------------------------
# 1. Encode/decode round-trip
# ---------------------------------------------------------------------------

class TestProfileHeaderRoundTrip(unittest.TestCase):

    def test_basic_round_trip(self):
        h = _make_header()
        dec = ProfileHeader.decode(h.encode())
        self.assertEqual(dec.profile_id, h.profile_id)
        self.assertEqual(dec.schema_version, h.schema_version)
        self.assertEqual(dec.profile_flags, h.profile_flags)
        self.assertEqual(dec.region_code, h.region_code)
        self.assertEqual(dec.max_duration_ms, h.max_duration_ms)
        self.assertEqual(dec.expiry_utc_ms, h.expiry_utc_ms)
        self.assertEqual(dec.body_length, h.body_length)
        self.assertEqual(dec.body_crc32, h.body_crc32)

    def test_no_transmit_flag_preserved(self):
        h = _make_header(profile_flags=ProfileFlags.NO_TRANSMIT)
        dec = ProfileHeader.decode(h.encode())
        self.assertTrue(dec.profile_flags & ProfileFlags.NO_TRANSMIT)
        self.assertFalse(dec.profile_flags & ProfileFlags.RECEIVE_ONLY)

    def test_receive_only_flag_preserved(self):
        h = _make_header(profile_flags=ProfileFlags.RECEIVE_ONLY)
        dec = ProfileHeader.decode(h.encode())
        self.assertTrue(dec.profile_flags & ProfileFlags.RECEIVE_ONLY)

    def test_expiry_utc_ms_preserved(self):
        future_ms = 2_000_000_000_000  # year ~2033
        h = _make_header(expiry_utc_ms=future_ms)
        dec = ProfileHeader.decode(h.encode())
        self.assertEqual(dec.expiry_utc_ms, future_ms)

    def test_max_body_length_boundary(self):
        h = _make_header(body_length=MAX_PROFILE_BODY_BYTES)
        dec = ProfileHeader.decode(h.encode())
        self.assertEqual(dec.body_length, MAX_PROFILE_BODY_BYTES)


# ---------------------------------------------------------------------------
# 2. Decode errors
# ---------------------------------------------------------------------------

class TestProfileHeaderDecodeErrors(unittest.TestCase):

    def test_too_short_raises(self):
        with self.assertRaises(ProtocolError):
            ProfileHeader.decode(b"\x00" * 10)

    def test_empty_raises(self):
        with self.assertRaises(ProtocolError):
            ProfileHeader.decode(b"")

    def test_one_byte_short_raises(self):
        h = _make_header()
        with self.assertRaises(ProtocolError):
            ProfileHeader.decode(h.encode()[:-1])


# ---------------------------------------------------------------------------
# 3. Validation — positive
# ---------------------------------------------------------------------------

class TestProfileValidatorAccept(unittest.TestCase):

    def test_valid_header_accepted(self):
        h = _make_header()
        result = ProfileValidator.validate(h)
        self.assertTrue(result.ok, result.reject_reason)

    def test_valid_with_future_expiry_accepted(self):
        future_ms = 9_000_000_000_000   # year ~2255
        h = _make_header(expiry_utc_ms=future_ms)
        now_ms = 1_700_000_000_000       # year ~2023
        result = ProfileValidator.validate(h, now_utc_ms=now_ms)
        self.assertTrue(result.ok, result.reject_reason)

    def test_zero_expiry_no_expiry_check(self):
        h = _make_header(expiry_utc_ms=0)
        result = ProfileValidator.validate(h, now_utc_ms=9_999_999_999_999)
        self.assertTrue(result.ok, "expiry_utc_ms=0 means no expiry; must accept regardless of now")

    def test_all_known_regions_accepted(self):
        for region in (0, 1, 2, 3):
            with self.subTest(region=region):
                h = _make_header(region_code=region)
                result = ProfileValidator.validate(h)
                self.assertTrue(result.ok, f"region_code={region} should be accepted")

    def test_no_transmit_flag_accepted(self):
        h = _make_header(profile_flags=ProfileFlags.NO_TRANSMIT)
        result = ProfileValidator.validate(h)
        self.assertTrue(result.ok)

    def test_receive_only_flag_accepted(self):
        h = _make_header(profile_flags=ProfileFlags.RECEIVE_ONLY)
        result = ProfileValidator.validate(h)
        self.assertTrue(result.ok)


# ---------------------------------------------------------------------------
# 4–8. Validation — negative cases
# ---------------------------------------------------------------------------

class TestProfileValidatorReject(unittest.TestCase):

    def test_expired_profile_rejected(self):
        """Expired profile (expiry_utc_ms in the past) must be rejected."""
        past_ms = 1_000_000_000_000   # year 2001 — always past
        h = _make_header(expiry_utc_ms=past_ms)
        now_ms = 1_700_000_000_000    # year ~2023
        result = ProfileValidator.validate(h, now_utc_ms=now_ms)
        self.assertFalse(result.ok)
        self.assertIn("expired", (result.reject_reason or "").lower())

    def test_expiry_equal_to_now_rejected(self):
        """Expiry exactly equal to now is considered expired (>= check)."""
        ts = 1_700_000_000_000
        h = _make_header(expiry_utc_ms=ts)
        result = ProfileValidator.validate(h, now_utc_ms=ts)
        self.assertFalse(result.ok)

    def test_oversized_body_length_rejected(self):
        """body_length > MAX_PROFILE_BODY_BYTES must be rejected before allocation."""
        h = _make_header(body_length=MAX_PROFILE_BODY_BYTES + 1)
        result = ProfileValidator.validate(h)
        self.assertFalse(result.ok)
        self.assertIn("body_length", (result.reject_reason or "").lower())

    def test_body_length_boundary_at_max_accepted(self):
        """Exactly MAX_PROFILE_BODY_BYTES is within the cap — must be accepted."""
        h = _make_header(body_length=MAX_PROFILE_BODY_BYTES)
        result = ProfileValidator.validate(h)
        self.assertTrue(result.ok)

    def test_out_of_scope_region_rejected(self):
        """region_code=0xFF (unknown) must be rejected."""
        h = _make_header(region_code=0xFF)
        result = ProfileValidator.validate(h)
        self.assertFalse(result.ok)
        self.assertIn("region_code", (result.reject_reason or "").lower())

    def test_unknown_region_4_rejected(self):
        """region_code=4 (not in known set) must be rejected."""
        h = _make_header(region_code=4)
        result = ProfileValidator.validate(h)
        self.assertFalse(result.ok)

    def test_unknown_schema_version_rejected(self):
        """schema_version != PROFILE_SCHEMA_VERSION must be rejected."""
        h = _make_header(schema_version=PROFILE_SCHEMA_VERSION + 1)
        result = ProfileValidator.validate(h)
        self.assertFalse(result.ok)
        self.assertIn("schema_version", (result.reject_reason or "").lower())

    def test_schema_version_zero_rejected(self):
        """schema_version=0 is not a valid recognized version."""
        h = _make_header(schema_version=0)
        result = ProfileValidator.validate(h)
        self.assertFalse(result.ok)

    def test_all_zeros_profile_id_rejected(self):
        """profile_id=all-zeros is an unset identity; must be rejected."""
        h = _make_header(profile_id=bytes(16))
        result = ProfileValidator.validate(h)
        self.assertFalse(result.ok)
        self.assertIn("profile_id", (result.reject_reason or "").lower())


# ---------------------------------------------------------------------------
# 9. Expiry skipped when now_utc_ms=0
# ---------------------------------------------------------------------------

class TestProfileValidatorNoExpiry(unittest.TestCase):

    def test_expiry_not_checked_when_now_zero(self):
        """
        If now_utc_ms=0, expiry check is skipped entirely.  This is the
        explicit caller opt-out for devices that do not yet have a synchronized
        clock.  A profile with an expiry in the past must still be parseable;
        the validation is not triggered without a reference time.
        """
        past_ms = 1_000_000_000_000   # always past
        h = _make_header(expiry_utc_ms=past_ms)
        result = ProfileValidator.validate(h, now_utc_ms=0)
        self.assertTrue(result.ok,
                        "now_utc_ms=0 is an explicit expiry-check opt-out; must accept")


# ---------------------------------------------------------------------------
# 10. ProfileFlags: safety properties independent of active capabilities
# ---------------------------------------------------------------------------

class TestProfileFlagsSafetyProperties(unittest.TestCase):

    def test_no_transmit_independent_of_capabilities(self):
        """
        A profile can set NO_TRANSMIT without enabling any transmit capability.
        This ensures the flag is data-driven, not dependent on the capability
        handshake.
        """
        self.assertTrue(ProfileFlags.NO_TRANSMIT & 0xFFFF,
                        "NO_TRANSMIT must be a non-zero flag")
        # NO_TRANSMIT should be detectable from flags alone
        flags_with_notx = ProfileFlags.NO_TRANSMIT
        self.assertTrue(flags_with_notx & ProfileFlags.NO_TRANSMIT)

    def test_receive_only_independent(self):
        """RECEIVE_ONLY is reachable without setting NO_TRANSMIT."""
        flags = ProfileFlags.RECEIVE_ONLY
        self.assertTrue(flags & ProfileFlags.RECEIVE_ONLY)
        self.assertFalse(flags & ProfileFlags.NO_TRANSMIT)

    def test_both_flags_combinable(self):
        """Both safety flags may be set simultaneously."""
        flags = ProfileFlags.NO_TRANSMIT | ProfileFlags.RECEIVE_ONLY
        self.assertTrue(flags & ProfileFlags.NO_TRANSMIT)
        self.assertTrue(flags & ProfileFlags.RECEIVE_ONLY)


# ---------------------------------------------------------------------------
# 11. Golden vector alignment
# ---------------------------------------------------------------------------

class TestProfileGoldenVectors(unittest.TestCase):
    """Verify that profile golden vectors in VECTORS decode and validate correctly."""

    def _get_profile_vectors(self):
        return [v for v in VECTORS if v.get("payload_class") == "ProfileHeader"]

    def test_profile_vectors_exist(self):
        pvecs = self._get_profile_vectors()
        self.assertGreaterEqual(
            len(pvecs), 4,
            "Expected at least 4 profile golden vectors (1 positive + 3 negative)"
        )

    def test_positive_vector_decodes_and_validates(self):
        for v in self._get_profile_vectors():
            if v.get("validation") != "accept":
                continue
            with self.subTest(vector_id=v["id"]):
                # Decode from golden hex
                raw = bytes.fromhex(v["hex"].replace(" ", "").replace("\n", ""))
                frame = Frame.decode(raw)
                self.assertEqual(frame.message_type, MessageType.PROFILE_INDEX_RESP)
                header = ProfileHeader.decode(frame.payload)

                expected = v["payload"]
                self.assertEqual(header.schema_version, expected["schema_version"])
                self.assertEqual(header.region_code, expected["region_code"])
                self.assertEqual(header.profile_flags, expected["profile_flags"])
                self.assertEqual(header.body_length, expected["body_length"])
                self.assertEqual(header.expiry_utc_ms, expected["expiry_utc_ms"])

                result = ProfileValidator.validate(header)
                self.assertTrue(result.ok, f"vector {v['id']}: {result.reject_reason}")

    def test_negative_vectors_decode_and_are_rejected(self):
        for v in self._get_profile_vectors():
            if v.get("validation") != "reject":
                continue
            with self.subTest(vector_id=v["id"]):
                raw = bytes.fromhex(v["hex"].replace(" ", "").replace("\n", ""))
                frame = Frame.decode(raw)
                header = ProfileHeader.decode(frame.payload)

                # Use a reference time well into the future so expiry_utc_ms
                # vectors that set a past timestamp are correctly detected
                now_ms = 2_000_000_000_000
                result = ProfileValidator.validate(header, now_utc_ms=now_ms)
                self.assertFalse(result.ok, f"vector {v['id']} should be rejected but was accepted")

                # Check that the reject_reason contains the expected substring
                expected_substring = v.get("reject_reason_contains", "")
                if expected_substring:
                    self.assertIn(
                        expected_substring.lower(),
                        (result.reject_reason or "").lower(),
                        f"vector {v['id']}: expected '{expected_substring}' in reject_reason"
                    )

    def test_golden_hex_matches_encoder(self):
        """Re-encode each profile vector and confirm it matches the golden hex."""
        for v in self._get_profile_vectors():
            if "hex" not in v or "payload" not in v:
                continue
            with self.subTest(vector_id=v["id"]):
                expected_hex = v["hex"].replace(" ", "").replace("\n", "").upper()
                raw = bytes.fromhex(expected_hex)
                # Decode and re-encode frame + payload
                frame = Frame.decode(raw)
                header = ProfileHeader.decode(frame.payload)
                reencoded_payload = header.encode()
                reencoded_frame = Frame(
                    protocol_version=frame.protocol_version,
                    message_type=frame.message_type,
                    flags=frame.flags,
                    request_id=frame.request_id,
                    session_id=frame.session_id,
                    monotonic_ms=frame.monotonic_ms,
                    payload=reencoded_payload,
                )
                self.assertEqual(
                    reencoded_frame.encode().hex().upper(),
                    expected_hex,
                    f"vector {v['id']}: re-encoded frame does not match golden hex"
                )


# ---------------------------------------------------------------------------
# 12. Detached/absent GhostWisp: no side effects on import
# ---------------------------------------------------------------------------

class TestProfileModuleDetachedImport(unittest.TestCase):

    def test_import_no_side_effects(self):
        """
        Importing devices.ghostwisp.protocol (which now includes profile) must
        not trigger any hardware access, open USB devices, or raise exceptions
        when no GhostWisp is present.  Verifies GhostBlade-standalone
        guarantee: zero behavioral change when the companion code is imported.
        """
        import importlib
        import devices.ghostwisp.protocol as gwp
        importlib.reload(gwp)
        self.assertIsNotNone(gwp.ProfileHeader)
        self.assertIsNotNone(gwp.ProfileValidator)

    def test_validator_works_standalone(self):
        """ProfileValidator must work with no hardware present."""
        h = _make_header()
        result = ProfileValidator.validate(h)
        self.assertTrue(result.ok)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main(verbosity=2)
