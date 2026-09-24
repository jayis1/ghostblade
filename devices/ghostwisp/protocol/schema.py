"""
GhostWisp companion protocol — payload schemas.

Author: jayis1

Each message family defines a Python dataclass that can be serialized to
and deserialized from the Frame.payload bytes.  The intent is a shared,
transport-neutral schema that both the GhostBlade companion daemon and the
host-side test harness consume.  C equivalents are in schema.h.

All multi-byte integers are little-endian.

Safety invariants encoded here:
- ARM_REQ includes expiry_deadline_ms so every armed action is time-bounded.
- ARM_REQ includes operator_id and auth_scope so identity and scope are
  always present; absent fields cause hard rejection.
- CONFIRM_PHYSICAL carries a confirm_nonce that must match the nonce in the
  corresponding ARM_REQ, preventing replay of an old confirmation.
- Active-action schema alone cannot cause RF/NFC/HID execution; the action
  broker and physical-confirmation state machine on GhostWisp firmware are
  the enforcement layer.
- Receive-only and radio-disable modes are first-class fields in
  SessionCapabilities, defaulting to True.
"""

from __future__ import annotations

import dataclasses
import struct
from typing import List

from .frame import ProtocolError

# ---------------------------------------------------------------------------
# Discovery family
# ---------------------------------------------------------------------------

_DEVICE_ID_LEN = 16  # bytes (128-bit stable device identity)
_VERSION_STR_MAX = 32


@dataclasses.dataclass
class IdentityPayload:
    """SESSION_HELLO / IDENTITY_REQ / IDENTITY_RESP payload."""

    STRUCT_FMT = f"<{_DEVICE_ID_LEN}sHH{_VERSION_STR_MAX}s"
    SIZE = struct.calcsize(STRUCT_FMT)

    device_id: bytes       # 16-byte stable identifier (hardware-derived)
    hw_rev: int            # BCD-encoded hardware revision
    fw_version: int        # BCD-encoded firmware version (major * 100 + minor)
    fw_tag: bytes          # NUL-padded ASCII git describe tag, max 32 bytes

    def encode(self) -> bytes:
        tag = self.fw_tag[:_VERSION_STR_MAX].ljust(_VERSION_STR_MAX, b"\x00")
        dev_id = self.device_id[:_DEVICE_ID_LEN].ljust(_DEVICE_ID_LEN, b"\x00")
        return struct.pack(self.STRUCT_FMT, dev_id, self.hw_rev, self.fw_version, tag)

    @classmethod
    def decode(cls, data: bytes) -> "IdentityPayload":
        if len(data) < cls.SIZE:
            raise ProtocolError(f"IdentityPayload: expected {cls.SIZE}, got {len(data)}")
        device_id, hw_rev, fw_version, fw_tag = struct.unpack_from(cls.STRUCT_FMT, data)
        return cls(
            device_id=device_id,
            hw_rev=hw_rev,
            fw_version=fw_version,
            fw_tag=fw_tag.rstrip(b"\x00"),
        )


@dataclasses.dataclass
class SessionCapabilities:
    """CAPABILITIES_REQ / CAPABILITIES_RESP payload.

    Flags default to the safest state: receive_only=True, radio_disabled=True.
    Active capabilities are opt-in per session after pairing.
    """

    STRUCT_FMT = "<HH"
    SIZE = struct.calcsize(STRUCT_FMT)

    # Capability bitmask (see CapabilityBit below)
    capability_flags: int = 0
    # Safety bitmask (see SafetyBit below)
    safety_flags: int = 0

    def encode(self) -> bytes:
        return struct.pack(self.STRUCT_FMT, self.capability_flags, self.safety_flags)

    @classmethod
    def decode(cls, data: bytes) -> "SessionCapabilities":
        if len(data) < cls.SIZE:
            raise ProtocolError(f"SessionCapabilities: expected {cls.SIZE}, got {len(data)}")
        cap, safety = struct.unpack_from(cls.STRUCT_FMT, data)
        return cls(capability_flags=cap, safety_flags=safety)

    @property
    def receive_only(self) -> bool:
        return bool(self.safety_flags & SafetyBit.RECEIVE_ONLY)

    @property
    def radio_disabled(self) -> bool:
        return bool(self.safety_flags & SafetyBit.RADIO_DISABLED)


class CapabilityBit:
    SUB_GHZ_RECEIVE  = 1 << 0
    SUB_GHZ_TRANSMIT = 1 << 1   # requires dual-consent; armed only
    NFC_INSPECT      = 1 << 2
    NFC_EMULATE      = 1 << 3   # requires dual-consent; armed only
    IR_RECEIVE       = 1 << 4
    IR_TRANSMIT      = 1 << 5   # requires dual-consent; armed only
    USB_SERIAL       = 1 << 6
    USB_HID          = 1 << 7   # requires dual-consent; armed only
    BUS_PROBE        = 1 << 8
    FIRMWARE_UPDATE  = 1 << 9
    COMPANION_SYNC   = 1 << 10


class SafetyBit:
    RECEIVE_ONLY     = 1 << 0   # default True at power-on
    RADIO_DISABLED   = 1 << 1   # default True at power-on
    HID_DISABLED     = 1 << 2
    NFC_EMULATE_OFF  = 1 << 3


# ---------------------------------------------------------------------------
# Synchronization family
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class TimeSyncPayload:
    """TIME_SYNC_REQ / TIME_SYNC_RESP payload."""

    STRUCT_FMT = "<QI"  # utc_ms (8), roundtrip_monotonic_ms (4)
    SIZE = struct.calcsize(STRUCT_FMT)

    utc_ms: int            # UTC Unix time in milliseconds
    roundtrip_ms: int      # monotonic_ms echo for RTT estimation

    def encode(self) -> bytes:
        return struct.pack(self.STRUCT_FMT, self.utc_ms, self.roundtrip_ms)

    @classmethod
    def decode(cls, data: bytes) -> "TimeSyncPayload":
        if len(data) < cls.SIZE:
            raise ProtocolError(f"TimeSyncPayload: expected {cls.SIZE}, got {len(data)}")
        utc_ms, rtt = struct.unpack_from(cls.STRUCT_FMT, data)
        return cls(utc_ms=utc_ms, roundtrip_ms=rtt)


@dataclasses.dataclass
class PolicySyncPayload:
    """POLICY_SYNC_REQ / POLICY_SYNC_RESP payload.

    Region policy is always the stricter of the two devices' effective
    policies.  If region is 0xFF (unset) the device must not transmit.
    """

    STRUCT_FMT = "<BBHI"  # region_code (1), policy_flags (1), reserved (2), policy_seq (4)
    SIZE = struct.calcsize(STRUCT_FMT)

    region_code: int       # 0=unset/most-restrictive, 1=EU868, 2=US915, 3=AU915
    policy_flags: int      # bitmask; bit 0 = no-transmit override
    policy_seq: int        # monotonically increasing sequence number

    def encode(self) -> bytes:
        return struct.pack(self.STRUCT_FMT, self.region_code, self.policy_flags, 0, self.policy_seq)

    @classmethod
    def decode(cls, data: bytes) -> "PolicySyncPayload":
        if len(data) < cls.SIZE:
            raise ProtocolError(f"PolicySyncPayload: expected {cls.SIZE}, got {len(data)}")
        region, flags, _res, seq = struct.unpack_from(cls.STRUCT_FMT, data)
        return cls(region_code=region, policy_flags=flags, policy_seq=seq)


# ---------------------------------------------------------------------------
# Active-action family (dual-consent)
# ---------------------------------------------------------------------------

_OPERATOR_ID_LEN = 16
_AUTH_SCOPE_LEN  = 64
_NONCE_LEN       = 16


@dataclasses.dataclass
class ArmRequestPayload:
    """ARM_REQ payload: GhostBlade → GhostWisp.

    All fields are mandatory.  Absence of any field (detected by size check)
    must cause hard rejection on GhostWisp.  GhostBlade cannot confirm an
    action by itself; CONFIRM_PHYSICAL from GhostWisp with a matching nonce
    is required.
    """

    # mode: values from ActiveMode enum
    STRUCT_FMT = (
        f"<B"                      # mode (1)
        f"B"                       # reserved (1)
        f"H"                       # duration_ms (2)
        f"I"                       # expiry_deadline_ms (4)  monotonic; auto-disarm at expiry
        f"{_OPERATOR_ID_LEN}s"     # operator_id (16)
        f"{_AUTH_SCOPE_LEN}s"      # auth_scope (64)  NUL-padded description of authorized target
        f"{_NONCE_LEN}s"           # nonce (16)       random per-request
    )
    SIZE = struct.calcsize(STRUCT_FMT)

    mode: int
    duration_ms: int
    expiry_deadline_ms: int   # monotonic ms; must expire <= 30 s after arm
    operator_id: bytes
    auth_scope: bytes
    nonce: bytes

    def encode(self) -> bytes:
        op = self.operator_id[:_OPERATOR_ID_LEN].ljust(_OPERATOR_ID_LEN, b"\x00")
        scope = self.auth_scope[:_AUTH_SCOPE_LEN].ljust(_AUTH_SCOPE_LEN, b"\x00")
        nonce = self.nonce[:_NONCE_LEN].ljust(_NONCE_LEN, b"\x00")
        return struct.pack(
            self.STRUCT_FMT,
            self.mode, 0,
            self.duration_ms,
            self.expiry_deadline_ms,
            op, scope, nonce,
        )

    @classmethod
    def decode(cls, data: bytes) -> "ArmRequestPayload":
        if len(data) < cls.SIZE:
            raise ProtocolError(
                f"ArmRequestPayload: expected {cls.SIZE}, got {len(data)}; "
                "absent mandatory field causes hard rejection"
            )
        mode, _res, dur, expiry, op_id, scope, nonce = struct.unpack_from(cls.STRUCT_FMT, data)
        return cls(
            mode=mode,
            duration_ms=dur,
            expiry_deadline_ms=expiry,
            operator_id=op_id.rstrip(b"\x00"),
            auth_scope=scope.rstrip(b"\x00"),
            nonce=nonce,
        )


class ActiveMode:
    """Valid modes for ArmRequestPayload.mode."""
    SUB_GHZ_TRANSMIT = 0x01
    NFC_EMULATE      = 0x02
    IR_TRANSMIT      = 0x03
    USB_HID          = 0x04


@dataclasses.dataclass
class ConfirmPhysicalPayload:
    """CONFIRM_PHYSICAL payload: GhostWisp → GhostBlade.

    The nonce must match the nonce in the corresponding ARM_REQ.  A mismatch
    means the confirmation is for a different (possibly replayed) arm request
    and must be rejected.
    """

    STRUCT_FMT = f"<{_NONCE_LEN}sI"  # nonce (16) + confirm_monotonic_ms (4)
    SIZE = struct.calcsize(STRUCT_FMT)

    nonce: bytes             # must match ARM_REQ nonce
    confirm_monotonic_ms: int  # GhostWisp monotonic time at button press

    def encode(self) -> bytes:
        nonce = self.nonce[:_NONCE_LEN].ljust(_NONCE_LEN, b"\x00")
        return struct.pack(self.STRUCT_FMT, nonce, self.confirm_monotonic_ms)

    @classmethod
    def decode(cls, data: bytes) -> "ConfirmPhysicalPayload":
        if len(data) < cls.SIZE:
            raise ProtocolError(f"ConfirmPhysicalPayload: expected {cls.SIZE}, got {len(data)}")
        nonce, mono = struct.unpack_from(cls.STRUCT_FMT, data)
        return cls(nonce=nonce, confirm_monotonic_ms=mono)


@dataclasses.dataclass
class AbortPayload:
    """ABORT payload: either direction.  Cancels any pending ARM/EXECUTING state."""

    STRUCT_FMT = f"<{_NONCE_LEN}sB"  # nonce (16) + reason (1)
    SIZE = struct.calcsize(STRUCT_FMT)

    nonce: bytes    # nonce of the ARM_REQ being aborted; all-zeros = abort any
    reason: int     # AbortReason value

    def encode(self) -> bytes:
        nonce = self.nonce[:_NONCE_LEN].ljust(_NONCE_LEN, b"\x00")
        return struct.pack(self.STRUCT_FMT, nonce, self.reason)

    @classmethod
    def decode(cls, data: bytes) -> "AbortPayload":
        if len(data) < cls.SIZE:
            raise ProtocolError(f"AbortPayload: expected {cls.SIZE}, got {len(data)}")
        nonce, reason = struct.unpack_from(cls.STRUCT_FMT, data)
        return cls(nonce=nonce, reason=reason)


class AbortReason:
    TIMEOUT           = 0x01  # expiry_deadline_ms elapsed
    USER_CANCEL       = 0x02  # physical cancel on GhostWisp
    POLICY_VIOLATION  = 0x03  # region/safety policy prevents execution
    REMOTE_ABORT      = 0x04  # GhostBlade withdrew the request
    LINK_LOST         = 0x05  # USB disconnect during armed state
