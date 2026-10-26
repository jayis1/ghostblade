"""Nonce wire-width regressions for the companion contract. Author: jayis1."""

import unittest

from devices.ghostwisp.protocol.frame import Frame, MessageType, ProtocolError
from devices.ghostwisp.protocol.schema import (
    AbortPayload, AbortReason, ArmRequestPayload, ConfirmPhysicalPayload,
)


def payloads(nonce):
    """Synthetic codec records only; no device or action broker is invoked."""
    return (
        (MessageType.ARM_REQ, ArmRequestPayload(
            mode=1, duration_ms=100, expiry_deadline_ms=1000,
            operator_id=b"synthetic", auth_scope=b"codec fixture", nonce=nonce,
        )),
        (MessageType.CONFIRM_PHYSICAL, ConfirmPhysicalPayload(nonce, 500)),
        (MessageType.ABORT, AbortPayload(nonce, AbortReason.USER_CANCEL)),
    )


class NonceWidthTests(unittest.TestCase):
    def test_oversized_nonces_are_rejected(self):
        for size in (17, 32, 4096):
            for kind, payload in payloads(b"n" * size):
                with self.subTest(kind=kind, size=size):
                    with self.assertRaisesRegex(ProtocolError, "nonce.*16-byte"):
                        payload.encode()

    def test_distinct_oversized_nonces_cannot_collapse(self):
        prefix = bytes(range(16))
        for suffix in (b"a", b"b", b"\x00"):
            for kind, payload in payloads(prefix + suffix):
                with self.subTest(kind=kind, suffix=suffix):
                    with self.assertRaises(ProtocolError):
                        payload.encode()

    def test_existing_padding_and_frame_round_trips(self):
        for size in range(17):
            nonce = bytes(range(size))
            expected = nonce.ljust(16, b"\x00")
            for kind, payload in payloads(nonce):
                with self.subTest(kind=kind, size=size):
                    wire = payload.encode()
                    frame = Frame.decode(Frame(message_type=kind, payload=wire).encode())
                    decoded = type(payload).decode(frame.payload)
                    self.assertEqual(decoded.nonce, expected)
                    self.assertEqual(decoded.encode(), wire)

    def test_zero_and_full_width_nonces_are_preserved(self):
        # All-zero ABORT remains the existing abort-any wire sentinel.
        for nonce in (b"\x00" * 16, b"\xff" * 16, bytes(range(16))):
            for kind, payload in payloads(nonce):
                with self.subTest(kind=kind, nonce=nonce):
                    decoded = type(payload).decode(payload.encode())
                    self.assertEqual(decoded.nonce, nonce)

    def test_mutated_nonce_is_rechecked_at_encoding(self):
        for kind, payload in payloads(b"valid"):
            payload.nonce += b"x" * 16
            with self.subTest(kind=kind):
                with self.assertRaises(ProtocolError):
                    payload.encode()


if __name__ == "__main__":
    unittest.main()
