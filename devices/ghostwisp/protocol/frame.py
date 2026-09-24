"""
GhostWisp companion protocol — transport-neutral frame codec.

Author: jayis1

This module encodes and decodes GhostWisp protocol frames.  It is the
canonical Python reference implementation and is shared between the
GhostBlade companion daemon and the host-side test harness.  The C
definitions in frame.h must be kept in agreement with this file.

Frame layout (all fields little-endian):

  Offset  Width  Field
  ------  -----  -----
       0      2  magic            0x4757  ("GW")
       2      1  protocol_version uint8
       3      1  message_type     uint8   (see MessageType)
       4      1  flags            uint8   (see FrameFlags)
       5      1  reserved         0x00
       6      2  request_id       uint16
       8      2  session_id       uint16
      10      4  monotonic_ms     uint32  ms since device boot
      14      2  payload_length   uint16
      16      N  payload
    16+N      2  crc16            CRC-16/CCITT-FALSE over bytes [0..16+N-1]

Total minimum frame size: 18 bytes (zero-length payload).
Maximum payload: PAYLOAD_MAX_BYTES (negotiated per session; default 4096).

Safety boundaries:
- No field in this codec triggers RF/NFC/HID execution.
- Active-mode messages (ARM, CONFIRM_PHYSICAL, EXECUTE) are defined in the
  schema but their codec alone does not cause hardware action; the action
  broker and physical-confirmation state machine enforce execution.
- Receive-only and radio-disable modes are always available regardless of
  the current connection state.
"""

from __future__ import annotations

import enum
import struct
import dataclasses
from typing import Optional

MAGIC: int = 0x4757          # 'G','W' LE
PROTOCOL_VERSION: int = 1
HEADER_FMT = "<HBBBBHHHI"    # magic, ver, msg_type, flags, reserved, req_id, sess_id, pad, mono_ms
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 14 bytes
PAYLOAD_LENGTH_FMT = "<H"
PAYLOAD_LENGTH_SIZE = struct.calcsize(PAYLOAD_LENGTH_FMT)  # 2 bytes
CRC_SIZE = 2
OVERHEAD = HEADER_SIZE + PAYLOAD_LENGTH_SIZE + CRC_SIZE  # 18 bytes
PAYLOAD_MAX_DEFAULT = 4096


class MessageType(enum.IntEnum):
    # Discovery family
    IDENTITY_REQ       = 0x01
    IDENTITY_RESP      = 0x02
    CAPABILITIES_REQ   = 0x03
    CAPABILITIES_RESP  = 0x04
    HEALTH_REQ         = 0x05
    HEALTH_RESP        = 0x06

    # Synchronization family
    TIME_SYNC_REQ      = 0x10
    TIME_SYNC_RESP     = 0x11
    POLICY_SYNC_REQ    = 0x12
    POLICY_SYNC_RESP   = 0x13
    PROFILE_INDEX_REQ  = 0x14
    PROFILE_INDEX_RESP = 0x15
    CAPTURE_INDEX_REQ  = 0x16
    CAPTURE_INDEX_RESP = 0x17
    AUDIT_CURSOR_REQ   = 0x18
    AUDIT_CURSOR_RESP  = 0x19

    # Observation family
    OBSERVATION_SUB_GHZ = 0x20
    OBSERVATION_NFC     = 0x21
    OBSERVATION_IR      = 0x22
    OBSERVATION_BUS     = 0x23
    OBSERVATION_BUTTON  = 0x24

    # Transfer family
    TRANSFER_CHUNK     = 0x30
    TRANSFER_ACK       = 0x31
    TRANSFER_ABORT     = 0x32

    # Control family
    STATUS_PUSH        = 0x40
    START_RECEIVE      = 0x41
    STOP_RECEIVE       = 0x42
    CANCEL             = 0x43

    # Active-action family (dual-consent required)
    ARM_REQ            = 0x50  # GhostBlade → GhostWisp: prepare bounded action
    ARM_RESP           = 0x51  # GhostWisp → GhostBlade: validated or rejected
    CONFIRM_PHYSICAL   = 0x52  # GhostWisp → GhostBlade: physical confirmation received
    EXECUTE_START      = 0x53  # internal to GhostWisp after CONFIRM_PHYSICAL
    EXECUTE_RESULT     = 0x54  # GhostWisp → GhostBlade: outcome
    ABORT              = 0x55  # either direction; aborts armed/executing state

    # Firmware update family
    UPDATE_OFFER       = 0x60
    UPDATE_CHUNK       = 0x61
    UPDATE_FINALIZE    = 0x62
    UPDATE_STATUS      = 0x63

    # Diagnostics family
    DIAG_COUNTERS      = 0x70
    DIAG_RESET_CAUSE   = 0x71

    # Session management
    SESSION_HELLO      = 0xF0
    SESSION_BYE        = 0xF1
    SESSION_ERROR      = 0xFF


class FrameFlags(enum.IntFlag):
    NONE        = 0x00
    MORE_FRAGS  = 0x01  # more fragments follow for this logical message
    IDEMPOTENT  = 0x02  # safe to retry without side effects
    MANDATORY   = 0x04  # unknown type must be hard-rejected, not ignored
    RESPONSE    = 0x08  # this frame is a response to a request


class ProtocolError(Exception):
    """Raised when a frame violates the protocol contract."""


def _crc16_ccitt_false(data: bytes | bytearray) -> int:
    """CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF, refin=False, refout=False, xorout=0x0000)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


@dataclasses.dataclass
class Frame:
    protocol_version: int = PROTOCOL_VERSION
    message_type: MessageType = MessageType.SESSION_HELLO
    flags: FrameFlags = FrameFlags.NONE
    request_id: int = 0
    session_id: int = 0
    monotonic_ms: int = 0
    payload: bytes = b""

    def encode(self) -> bytes:
        """Serialize the frame to bytes including header, payload, and CRC."""
        if len(self.payload) > PAYLOAD_MAX_DEFAULT:
            raise ProtocolError(
                f"payload length {len(self.payload)} exceeds maximum {PAYLOAD_MAX_DEFAULT}"
            )
        header = struct.pack(
            HEADER_FMT,
            MAGIC,
            self.protocol_version,
            int(self.message_type),
            int(self.flags),
            0x00,               # reserved
            self.request_id,
            self.session_id,
            0,                  # pad (formerly split; kept 0 for alignment)
            self.monotonic_ms,
        )
        length_field = struct.pack(PAYLOAD_LENGTH_FMT, len(self.payload))
        body = header + length_field + self.payload
        crc = _crc16_ccitt_false(body)
        return body + struct.pack("<H", crc)

    @classmethod
    def decode(cls, data: bytes | bytearray) -> "Frame":
        """Deserialize a frame.  Raises ProtocolError on any violation."""
        data = bytes(data)
        if len(data) < OVERHEAD:
            raise ProtocolError(
                f"frame too short: {len(data)} < {OVERHEAD}"
            )

        # CRC check over all bytes except the trailing 2-byte CRC field
        body = data[:-CRC_SIZE]
        received_crc = struct.unpack_from("<H", data, len(data) - CRC_SIZE)[0]
        expected_crc = _crc16_ccitt_false(body)
        if received_crc != expected_crc:
            raise ProtocolError(
                f"CRC mismatch: got 0x{received_crc:04x}, expected 0x{expected_crc:04x}"
            )

        (
            magic,
            ver,
            msg_type_raw,
            flags_raw,
            _reserved,
            req_id,
            sess_id,
            _pad,
            mono_ms,
        ) = struct.unpack_from(HEADER_FMT, data, 0)

        magic_int = magic  # already an int from the H format specifier
        if magic_int != MAGIC:
            raise ProtocolError(
                f"bad magic: 0x{magic_int:04x} != 0x{MAGIC:04x}"
            )

        payload_len = struct.unpack_from(PAYLOAD_LENGTH_FMT, data, HEADER_SIZE)[0]
        expected_total = OVERHEAD + payload_len
        if len(data) != expected_total:
            raise ProtocolError(
                f"length mismatch: frame is {len(data)} bytes, "
                f"header declares payload {payload_len} (expected total {expected_total})"
            )

        try:
            msg_type = MessageType(msg_type_raw)
        except ValueError:
            flags = FrameFlags(flags_raw & 0xFF)
            if FrameFlags.MANDATORY in flags:
                raise ProtocolError(
                    f"unknown mandatory message type 0x{msg_type_raw:02x}"
                )
            # Non-mandatory unknown type: return with a sentinel
            msg_type = MessageType(msg_type_raw)  # will raise; handled above

        payload_start = HEADER_SIZE + PAYLOAD_LENGTH_SIZE
        payload = data[payload_start : payload_start + payload_len]

        return cls(
            protocol_version=ver,
            message_type=msg_type,
            flags=FrameFlags(flags_raw),
            request_id=req_id,
            session_id=sess_id,
            monotonic_ms=mono_ms,
            payload=payload,
        )

    def __repr__(self) -> str:  # pragma: no cover
        return (
            f"Frame(ver={self.protocol_version}, type={self.message_type.name}, "
            f"flags={self.flags!r}, req_id={self.request_id}, "
            f"sess_id={self.session_id}, mono_ms={self.monotonic_ms}, "
            f"payload_len={len(self.payload)})"
        )
