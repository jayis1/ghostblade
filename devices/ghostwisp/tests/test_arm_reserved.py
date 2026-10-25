"""Host regressions for the existing ARM_REQ reserved byte.

Author: jayis1

These exercise structural parsing, not authorization or action execution.
"""

import binascii
import struct
import unittest

from devices.ghostwisp.protocol.frame import Frame, MessageType, ProtocolError
from devices.ghostwisp.protocol.schema import ActiveMode, ArmRequestPayload


class TestArmReserved(unittest.TestCase):
    def setUp(self):
        self.request = ArmRequestPayload(
            mode=ActiveMode.IR_TRANSMIT,
            duration_ms=100,
            expiry_deadline_ms=1000,
            operator_id=b"lab-operator",
            auth_scope=b"owned test fixture",
            nonce=bytes(range(16)),
        )
        self.payload = self.request.encode()

    def test_every_nonzero_reserved_byte_is_rejected(self):
        for value in range(1, 256):
            malformed = bytearray(self.payload)
            malformed[1] = value
            for buffer_type in (bytes, bytearray):
                with self.subTest(value=value, buffer_type=buffer_type):
                    with self.assertRaisesRegex(
                        ProtocolError, "ArmRequestPayload: reserved byte must be zero"
                    ):
                        ArmRequestPayload.decode(buffer_type(malformed))

    def test_crc_valid_envelope_does_not_authorize_reserved_data(self):
        for value in range(1, 256):
            malformed = bytearray(self.payload)
            malformed[1] = value
            # Independently checksum the malformed payload's outer envelope.
            body = Frame(message_type=MessageType.ARM_REQ,
                         payload=bytes(malformed)).encode()[:-2]
            wire = body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))
            for buffer_type in (bytes, bytearray):
                with self.subTest(value=value, buffer_type=buffer_type):
                    frame = Frame.decode(buffer_type(wire))
                    self.assertEqual(frame.message_type, MessageType.ARM_REQ)
                    with self.assertRaisesRegex(ProtocolError, "reserved byte"):
                        ArmRequestPayload.decode(frame.payload)

    def test_all_truncated_prefixes_keep_mandatory_field_error(self):
        for length in range(ArmRequestPayload.SIZE):
            for buffer_type in (bytes, bytearray):
                with self.subTest(length=length, buffer_type=buffer_type):
                    with self.assertRaisesRegex(ProtocolError, "absent mandatory field"):
                        ArmRequestPayload.decode(buffer_type(self.payload[:length]))

    def test_conforming_round_trip_preserves_fields_and_wire_bytes(self):
        for mode in (ActiveMode.SUB_GHZ_TRANSMIT, ActiveMode.NFC_EMULATE,
                     ActiveMode.IR_TRANSMIT, ActiveMode.USB_HID):
            self.request.mode = mode
            payload = self.request.encode()
            self.assertEqual(payload[1], 0)
            for buffer_type in (bytes, bytearray):
                with self.subTest(mode=mode, buffer_type=buffer_type):
                    decoded = ArmRequestPayload.decode(buffer_type(payload))
                    self.assertEqual(decoded, self.request)
                    self.assertEqual(decoded.encode(), payload)

    def test_following_bytes_remain_caller_responsibility(self):
        for buffer_type in (bytes, bytearray):
            with self.subTest(buffer_type=buffer_type):
                decoded = ArmRequestPayload.decode(
                    buffer_type(self.payload + b"following bytes"))
                self.assertEqual(decoded, self.request)


if __name__ == "__main__":
    unittest.main(verbosity=2)
