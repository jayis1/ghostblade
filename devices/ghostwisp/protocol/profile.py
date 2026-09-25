"""
GhostWisp companion protocol — bounded profile schema and parser.

Author: jayis1

A GhostWisp profile is a strictly bounded data record that encodes a set of
permissions and operational constraints for use on the device.  Profiles are
data, never native code: this module enforces that invariant with strict schema
validation, explicit field size caps, and an atomic two-phase parse sequence.

Safety invariants encoded here:
- ProfileHeader.body_length must not exceed MAX_PROFILE_BODY_BYTES (50 KiB).
  Enforces the RP2350B RAM budget; prevents unbounded allocation before
  deserialization.
- ProfileHeader.schema_version must be recognized (== PROFILE_SCHEMA_VERSION)
  or be explicitly flagged as a version-mismatch; unknown versions are never
  silently parsed.
- ProfileHeader.region_code must be within the set of known codes; region 0xFF
  (unknown) is rejected as out-of-scope, not silently accepted.
- ProfileHeader.expiry_utc_ms, when non-zero, must be >= the provided
  reference time (caller supplies current UTC ms); expired profiles are
  rejected, not executed.
- ProfileFlags.NO_TRANSMIT and RECEIVE_ONLY are always available and can be
  set without enabling any active capability.
- The partial-write guard: parse always operates on a complete, size-validated
  payload; callers must not pass incomplete buffers.  Atomic write semantics
  are the caller's responsibility (commit-on-checksum-pass).

No exec(), eval(), pickle, or YAML with unsafe loader is used anywhere in this
module.  All deserialization is struct.unpack from validated byte buffers.
"""

from __future__ import annotations

import dataclasses
import struct
from typing import Optional

from .frame import ProtocolError

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

PROFILE_SCHEMA_VERSION: int = 1

# Maximum allowed profile body size in bytes.  Enforced before deserialization
# to respect RP2350B-class RAM constraints.
MAX_PROFILE_BODY_BYTES: int = 50 * 1024   # 50 KiB

# Known region codes (must match PolicySyncPayload and firmware region table).
_KNOWN_REGION_CODES = frozenset({
    0,    # 0 = unset / most-restrictive (no transmit)
    1,    # EU868
    2,    # US915
    3,    # AU915
})


# ---------------------------------------------------------------------------
# Profile flags
# ---------------------------------------------------------------------------

class ProfileFlags:
    """Bitmask values for ProfileHeader.profile_flags."""
    NO_TRANSMIT  = 1 << 0   # profile disallows any RF/NFC/IR/HID transmit
    RECEIVE_ONLY = 1 << 1   # profile further constrains to receive-only mode
    # Bits 2-15 reserved; unknown set bits are preserved, not rejected.


# ---------------------------------------------------------------------------
# ProfileHeader payload
# ---------------------------------------------------------------------------

_PROFILE_STRUCT_FMT = "<16sHHBBHIQII"
#                        ^profile_id (16 bytes)
#                           ^schema_version (uint16)
#                             ^profile_flags (uint16)
#                               ^region_code (uint8)
#                                ^reserved (uint8 — must be 0)
#                                  ^reserved2 (uint16 — must be 0)
#                                    ^max_duration_ms (uint32; 0 = unlimited)
#                                      ^expiry_utc_ms (uint64; 0 = no expiry)
#                                        ^body_length (uint32; bytes of following body)
#                                          ^body_crc32 (uint32; CRC-32/ISO-HDLC of body)
_PROFILE_STRUCT_SIZE = struct.calcsize(_PROFILE_STRUCT_FMT)  # 44 bytes


@dataclasses.dataclass
class ProfileHeader:
    """PROFILE_INDEX_RESP / profile transfer header payload.

    A ProfileHeader is always the first record in a profile transfer.  It
    describes the profile's identity, constraints, and body size.  The caller
    must validate the header (via ProfileValidator) before allocating space for
    or accepting the body.
    """

    profile_id: bytes          # 16-byte stable profile identity
    schema_version: int        # must equal PROFILE_SCHEMA_VERSION
    profile_flags: int         # bitmask; see ProfileFlags
    region_code: int           # 0=unset, 1=EU868, 2=US915, 3=AU915
    max_duration_ms: int       # maximum allowed action duration (0 = unlimited)
    expiry_utc_ms: int         # UTC expiry in ms (0 = no expiry)
    body_length: int           # byte count of the profile body that follows
    body_crc32: int            # CRC-32/ISO-HDLC of the profile body

    # --- class-level size constant ---
    SIZE: int = dataclasses.field(default=_PROFILE_STRUCT_SIZE, init=False, repr=False, compare=False)

    def encode(self) -> bytes:
        pid = self.profile_id[:16].ljust(16, b"\x00")
        return struct.pack(
            _PROFILE_STRUCT_FMT,
            pid,
            self.schema_version,
            self.profile_flags,
            self.region_code,
            0,    # reserved
            0,    # reserved2
            self.max_duration_ms,
            self.expiry_utc_ms,
            self.body_length,
            self.body_crc32,
        )

    @classmethod
    def decode(cls, data: bytes) -> "ProfileHeader":
        """Decode raw bytes into a ProfileHeader.

        Raises ProtocolError if the buffer is too short.  Does NOT perform
        semantic validation — call ProfileValidator.validate() for that.
        """
        if len(data) < _PROFILE_STRUCT_SIZE:
            raise ProtocolError(
                f"ProfileHeader: expected {_PROFILE_STRUCT_SIZE} bytes, got {len(data)}"
            )
        (
            profile_id,
            schema_version,
            profile_flags,
            region_code,
            _reserved,
            _reserved2,
            max_duration_ms,
            expiry_utc_ms,
            body_length,
            body_crc32,
        ) = struct.unpack_from(_PROFILE_STRUCT_FMT, data)
        return cls(
            profile_id=profile_id.rstrip(b"\x00"),
            schema_version=schema_version,
            profile_flags=profile_flags,
            region_code=region_code,
            max_duration_ms=max_duration_ms,
            expiry_utc_ms=expiry_utc_ms,
            body_length=body_length,
            body_crc32=body_crc32,
        )


# ---------------------------------------------------------------------------
# Profile validation result
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class ProfileValidationResult:
    """Outcome of ProfileValidator.validate().

    ok == True only when every check passed.  If ok is False, reject_reason
    gives the human-readable explanation.  Callers must treat ok=False as a
    hard rejection — do not attempt partial acceptance.
    """
    ok: bool
    reject_reason: Optional[str] = None

    @classmethod
    def accept(cls) -> "ProfileValidationResult":
        return cls(ok=True)

    @classmethod
    def reject(cls, reason: str) -> "ProfileValidationResult":
        return cls(ok=False, reject_reason=reason)


# ---------------------------------------------------------------------------
# Profile validator
# ---------------------------------------------------------------------------

class ProfileValidator:
    """Stateless profile header validator.

    All checks fail toward the most restrictive interpretation.  A validation
    failure means the profile must be discarded; GhostWisp must not enter an
    armed or executing state based on a rejected profile.

    Usage::

        result = ProfileValidator.validate(header, now_utc_ms=time.time_ns() // 1_000_000)
        if not result.ok:
            # log result.reject_reason; discard profile
            ...
    """

    @staticmethod
    def validate(
        header: ProfileHeader,
        now_utc_ms: int = 0,
    ) -> ProfileValidationResult:
        """Validate a ProfileHeader against all safety invariants.

        Args:
            header: parsed ProfileHeader (from ProfileHeader.decode()).
            now_utc_ms: current UTC time in milliseconds.  If 0, expiry
                checking is skipped (caller opts out explicitly).  Callers
                on GhostWisp should always supply the synchronized time.

        Returns:
            ProfileValidationResult — check .ok before proceeding.
        """
        # 1. Schema version must be recognized.
        if header.schema_version != PROFILE_SCHEMA_VERSION:
            return ProfileValidationResult.reject(
                f"unsupported profile schema_version {header.schema_version}; "
                f"expected {PROFILE_SCHEMA_VERSION}"
            )

        # 2. Body size must not exceed the RAM-budget cap.
        if header.body_length > MAX_PROFILE_BODY_BYTES:
            return ProfileValidationResult.reject(
                f"profile body_length {header.body_length} exceeds maximum "
                f"{MAX_PROFILE_BODY_BYTES} bytes"
            )

        # 3. Region code must be a known value.
        if header.region_code not in _KNOWN_REGION_CODES:
            return ProfileValidationResult.reject(
                f"unknown region_code {header.region_code:#x}; "
                "out-of-scope profiles are rejected, not executed"
            )

        # 4. Expiry check (only when caller supplies current time).
        if now_utc_ms > 0 and header.expiry_utc_ms > 0:
            if now_utc_ms >= header.expiry_utc_ms:
                return ProfileValidationResult.reject(
                    f"profile expired: expiry_utc_ms={header.expiry_utc_ms}, "
                    f"now_utc_ms={now_utc_ms}"
                )

        # 5. profile_id must not be all-zeros (unset identity).
        if not any(header.profile_id):
            return ProfileValidationResult.reject(
                "profile_id is all zeros; unset identity is not a valid profile"
            )

        return ProfileValidationResult.accept()
