"""
GhostWisp companion protocol — golden test vectors.

Author: jayis1

These vectors are the frozen wire contract between GhostBlade and GhostWisp.
Each entry specifies a fully serialized frame as a hex string, and the
expected parsed result.  Both the Python codec tests and the GhostWisp
firmware unit tests must validate every vector here without modification.

A failing vector is a breaking change.  A new protocol revision may add
vectors but must not alter existing ones.

Vector naming convention:
    <family>_<description>

Field order in each dict matches the Frame/Payload dataclass field order.
"""

from __future__ import annotations
from typing import List, Dict, Any

VECTORS: List[Dict[str, Any]] = [

    # ------------------------------------------------------------------
    # SESSION_HELLO: minimal zero-payload session open
    # ------------------------------------------------------------------
    {
        "id": "session_hello_minimal",
        "description": "SESSION_HELLO with no payload; baseline sync word, version, and CRC check.",
        "hex": (
            "5747"          # magic = 0x4757 (LE: 'W','G' stored as 0x47,0x57)
            "01"            # protocol_version = 1
            "F0"            # message_type = SESSION_HELLO (0xF0)
            "00"            # flags = NONE
            "00"            # reserved
            "0100"          # request_id = 1
            "0100"          # session_id = 1
            "0000"          # pad
            "01000000"      # monotonic_ms = 1
            "0000"          # payload_length = 0
            # crc computed below
        ),
        "frame": {
            "protocol_version": 1,
            "message_type": "SESSION_HELLO",
            "flags": 0,
            "request_id": 1,
            "session_id": 1,
            "monotonic_ms": 1,
            "payload": "",
        },
    },

    # ------------------------------------------------------------------
    # IDENTITY_RESP: identity payload round-trip
    # ------------------------------------------------------------------
    {
        "id": "identity_resp",
        "description": "IDENTITY_RESP with a 16-byte device ID, hw_rev 0x0100, fw 0x0101, tag 'v1.0.0'.",
        "payload_class": "IdentityPayload",
        "payload": {
            "device_id": "deadbeefcafebabe0102030405060708",
            "hw_rev": 0x0100,
            "fw_version": 0x0101,
            "fw_tag": "76312e302e30",  # b"v1.0.0" hex
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "IDENTITY_RESP",
            "flags": 8,      # RESPONSE
            "request_id": 2,
            "session_id": 1,
            "monotonic_ms": 100,
        },
    },

    # ------------------------------------------------------------------
    # CAPABILITIES_RESP: safe defaults (receive_only + radio_disabled)
    # ------------------------------------------------------------------
    {
        "id": "capabilities_resp_safe_defaults",
        "description": (
            "CAPABILITIES_RESP with receive-only and radio-disabled safety bits set "
            "(the power-on default).  Sub-GHz receive and companion-sync capabilities advertised."
        ),
        "payload_class": "SessionCapabilities",
        "payload": {
            "capability_flags": 0b00000000_00000101,   # SUB_GHZ_RECEIVE | COMPANION_SYNC  (bits 0,10)
            "safety_flags":     0b00000000_00000011,   # RECEIVE_ONLY | RADIO_DISABLED
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "CAPABILITIES_RESP",
            "flags": 8,      # RESPONSE
            "request_id": 3,
            "session_id": 1,
            "monotonic_ms": 150,
        },
    },

    # ------------------------------------------------------------------
    # TIME_SYNC_REQ
    # ------------------------------------------------------------------
    {
        "id": "time_sync_req",
        "description": "TIME_SYNC_REQ carrying UTC epoch and roundtrip echo for RTT estimation.",
        "payload_class": "TimeSyncPayload",
        "payload": {
            "utc_ms": 1_700_000_000_000,   # 2023-11-14T22:13:20Z in ms
            "roundtrip_ms": 200,
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "TIME_SYNC_REQ",
            "flags": 2,    # IDEMPOTENT
            "request_id": 4,
            "session_id": 1,
            "monotonic_ms": 200,
        },
    },

    # ------------------------------------------------------------------
    # POLICY_SYNC_RESP: EU868 region, no-transmit override cleared
    # ------------------------------------------------------------------
    {
        "id": "policy_sync_resp_eu868",
        "description": (
            "POLICY_SYNC_RESP negotiated to EU868 (region_code=1) with no additional "
            "restrictions; region is user-set, never auto-inferred."
        ),
        "payload_class": "PolicySyncPayload",
        "payload": {
            "region_code": 1,    # EU868
            "policy_flags": 0,
            "policy_seq": 7,
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "POLICY_SYNC_RESP",
            "flags": 8,   # RESPONSE
            "request_id": 5,
            "session_id": 1,
            "monotonic_ms": 300,
        },
    },

    # ------------------------------------------------------------------
    # ARM_REQ: all mandatory fields present
    # ------------------------------------------------------------------
    {
        "id": "arm_req_sub_ghz",
        "description": (
            "ARM_REQ for a sub-GHz transmit action.  All mandatory fields are present: "
            "operator_id, auth_scope, duration, expiry, and nonce.  "
            "GhostBlade cannot confirm alone — CONFIRM_PHYSICAL with matching nonce required."
        ),
        "payload_class": "ArmRequestPayload",
        "payload": {
            "mode": 1,                              # SUB_GHZ_TRANSMIT
            "duration_ms": 500,
            "expiry_deadline_ms": 30000,            # 30 s from arm time
            "operator_id": "6f70657261746f72494401020304050607080910",   # 16 bytes
            "auth_scope": (
                "6c61622d67617261676520342d62617920646f6f72"               # "lab-garage 4-bay door"
                + "00" * 43                                                 # NUL padding to 64 bytes
            ),
            "nonce": "aabbccddeeff00112233445566778899",  # 16 bytes random
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "ARM_REQ",
            "flags": 4,   # MANDATORY
            "request_id": 10,
            "session_id": 1,
            "monotonic_ms": 5000,
        },
    },

    # ------------------------------------------------------------------
    # CONFIRM_PHYSICAL: nonce matches ARM_REQ
    # ------------------------------------------------------------------
    {
        "id": "confirm_physical_matching_nonce",
        "description": (
            "CONFIRM_PHYSICAL with the nonce from the preceding ARM_REQ.  "
            "A mismatched nonce must be rejected to prevent replay of old confirmations."
        ),
        "payload_class": "ConfirmPhysicalPayload",
        "payload": {
            "nonce": "aabbccddeeff00112233445566778899",  # must match ARM_REQ nonce
            "confirm_monotonic_ms": 5800,
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "CONFIRM_PHYSICAL",
            "flags": 0,
            "request_id": 11,
            "session_id": 1,
            "monotonic_ms": 5800,
        },
    },

    # ------------------------------------------------------------------
    # ABORT: timeout expiry — auto-disarm
    # ------------------------------------------------------------------
    {
        "id": "abort_timeout",
        "description": (
            "ABORT with reason=TIMEOUT (0x01).  Sent when expiry_deadline_ms elapses "
            "before CONFIRM_PHYSICAL is received.  Must discard the pending armed action; "
            "no latent armed state may remain."
        ),
        "payload_class": "AbortPayload",
        "payload": {
            "nonce": "aabbccddeeff00112233445566778899",
            "reason": 1,   # TIMEOUT
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "ABORT",
            "flags": 0,
            "request_id": 12,
            "session_id": 1,
            "monotonic_ms": 35000,
        },
    },

    # ------------------------------------------------------------------
    # ABORT: link-lost — auto-disarm on disconnect
    # ------------------------------------------------------------------
    {
        "id": "abort_link_lost",
        "description": (
            "ABORT with reason=LINK_LOST (0x05).  Sent when USB connection drops while "
            "an action is in the ARMED state.  The action must be discarded, never queued "
            "for later execution."
        ),
        "payload_class": "AbortPayload",
        "payload": {
            "nonce": "aabbccddeeff00112233445566778899",
            "reason": 5,   # LINK_LOST
        },
        "frame": {
            "protocol_version": 1,
            "message_type": "ABORT",
            "flags": 0,
            "request_id": 13,
            "session_id": 1,
            "monotonic_ms": 36000,
        },
    },

    # ------------------------------------------------------------------
    # SESSION_ERROR: unknown mandatory message type → hard reject
    # ------------------------------------------------------------------
    {
        "id": "session_error_unknown_mandatory",
        "description": (
            "SESSION_ERROR response when an unknown message with MANDATORY flag is received.  "
            "Version skew must produce a visible error, not a silent downgrade."
        ),
        "frame": {
            "protocol_version": 1,
            "message_type": "SESSION_ERROR",
            "flags": 8,    # RESPONSE
            "request_id": 0,
            "session_id": 1,
            "monotonic_ms": 500,
            "payload": "",
        },
    },

    # ------------------------------------------------------------------
    # PROFILE_INDEX_RESP: valid bounded profile header (positive vector)
    # ------------------------------------------------------------------
    {
        "id": "profile_index_resp_valid",
        "description": (
            "PROFILE_INDEX_RESP carrying a valid ProfileHeader.  "
            "schema_version=1, region_code=1 (EU868), profile_flags=0x0003 "
            "(NO_TRANSMIT | RECEIVE_ONLY), body_length=100 bytes (within 50 KiB cap), "
            "expiry_utc_ms=0 (no expiry).  "
            "ProfileValidator must accept this header."
        ),
        "payload_class": "ProfileHeader",
        "payload": {
            "profile_id": "000102030405060708090a0b0c0d0e0f",
            "schema_version": 1,
            "profile_flags": 3,     # NO_TRANSMIT | RECEIVE_ONLY
            "region_code": 1,       # EU868
            "max_duration_ms": 1000,
            "expiry_utc_ms": 0,
            "body_length": 100,
            "body_crc32": 0xDEADBEEF,
        },
        "hex": (
            "574701150800060001000000900100002C00"
            "000102030405060708090A0B0C0D0E0F"
            "0100"              # schema_version = 1
            "0300"              # profile_flags = 3
            "01"                # region_code = 1 (EU868)
            "00"                # reserved
            "0000"              # reserved2
            "E8030000"          # max_duration_ms = 1000
            "0000000000000000"  # expiry_utc_ms = 0
            "64000000"          # body_length = 100
            "EFBEADDE"          # body_crc32 = 0xDEADBEEF
            "7222"              # CRC
        ),
        "frame": {
            "protocol_version": 1,
            "message_type": "PROFILE_INDEX_RESP",
            "flags": 8,         # RESPONSE
            "request_id": 6,
            "session_id": 1,
            "monotonic_ms": 400,
        },
        "validation": "accept",
    },

    # ------------------------------------------------------------------
    # PROFILE_INDEX_RESP: expired profile (negative vector — must reject)
    # ------------------------------------------------------------------
    {
        "id": "profile_index_resp_expired",
        "description": (
            "PROFILE_INDEX_RESP with expiry_utc_ms=1000000000000 (year 2001, always past).  "
            "ProfileValidator.validate(now_utc_ms > expiry_utc_ms) must return ok=False.  "
            "Expired profiles must be rejected, not executed."
        ),
        "payload_class": "ProfileHeader",
        "payload": {
            "profile_id": "000102030405060708090a0b0c0d0e0f",
            "schema_version": 1,
            "profile_flags": 3,
            "region_code": 1,
            "max_duration_ms": 1000,
            "expiry_utc_ms": 1_000_000_000_000,
            "body_length": 100,
            "body_crc32": 0xDEADBEEF,
        },
        "hex": (
            "574701150800070001000000F40100002C00"
            "000102030405060708090A0B0C0D0E0F"
            "0100"
            "0300"
            "01"
            "00"
            "0000"
            "E8030000"
            "0010A5D4E8000000"  # expiry_utc_ms = 1_000_000_000_000 (LE)
            "64000000"
            "EFBEADDE"
            "9465"
        ),
        "frame": {
            "protocol_version": 1,
            "message_type": "PROFILE_INDEX_RESP",
            "flags": 8,
            "request_id": 7,
            "session_id": 1,
            "monotonic_ms": 500,
        },
        "validation": "reject",
        "reject_reason_contains": "expired",
    },

    # ------------------------------------------------------------------
    # PROFILE_INDEX_RESP: oversized body_length (negative vector — must reject)
    # ------------------------------------------------------------------
    {
        "id": "profile_index_resp_oversized",
        "description": (
            "PROFILE_INDEX_RESP with body_length=51201 (> 50 KiB cap).  "
            "ProfileValidator must reject before any allocation or deserialization."
        ),
        "payload_class": "ProfileHeader",
        "payload": {
            "profile_id": "000102030405060708090a0b0c0d0e0f",
            "schema_version": 1,
            "profile_flags": 3,
            "region_code": 1,
            "max_duration_ms": 1000,
            "expiry_utc_ms": 0,
            "body_length": 51_201,   # > MAX_PROFILE_BODY_BYTES (50 * 1024)
            "body_crc32": 0xDEADBEEF,
        },
        "hex": (
            "574701150800080001000000580200002C00"
            "000102030405060708090A0B0C0D0E0F"
            "0100"
            "0300"
            "01"
            "00"
            "0000"
            "E8030000"
            "0000000000000000"
            "01C80000"          # body_length = 51201
            "EFBEADDE"
            "158B"
        ),
        "frame": {
            "protocol_version": 1,
            "message_type": "PROFILE_INDEX_RESP",
            "flags": 8,
            "request_id": 8,
            "session_id": 1,
            "monotonic_ms": 600,
        },
        "validation": "reject",
        "reject_reason_contains": "body_length",
    },

    # ------------------------------------------------------------------
    # PROFILE_INDEX_RESP: out-of-scope region code (negative vector — must reject)
    # ------------------------------------------------------------------
    {
        "id": "profile_index_resp_out_of_scope_region",
        "description": (
            "PROFILE_INDEX_RESP with region_code=0xFF (unknown/out-of-scope).  "
            "ProfileValidator must reject: a profile specifying an unauthorized region "
            "must never reach the action broker.  Profiles are data, not code; "
            "this is a validation/UX path, not an execution path."
        ),
        "payload_class": "ProfileHeader",
        "payload": {
            "profile_id": "000102030405060708090a0b0c0d0e0f",
            "schema_version": 1,
            "profile_flags": 3,
            "region_code": 0xFF,     # unknown region
            "max_duration_ms": 1000,
            "expiry_utc_ms": 0,
            "body_length": 100,
            "body_crc32": 0xDEADBEEF,
        },
        "hex": (
            "574701150800090001000000BC0200002C00"
            "000102030405060708090A0B0C0D0E0F"
            "0100"
            "0300"
            "FF"                # region_code = 0xFF (out-of-scope)
            "00"
            "0000"
            "E8030000"
            "0000000000000000"
            "64000000"
            "EFBEADDE"
            "6A53"
        ),
        "frame": {
            "protocol_version": 1,
            "message_type": "PROFILE_INDEX_RESP",
            "flags": 8,
            "request_id": 9,
            "session_id": 1,
            "monotonic_ms": 700,
        },
        "validation": "reject",
        "reject_reason_contains": "region_code",
    },
]
