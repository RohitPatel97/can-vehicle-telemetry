from __future__ import annotations

import unittest

from tools.telemetry_reference import (
    ALLOWED_FLAGS,
    MAX_BATTERY_MV,
    MAX_COOLANT_C,
    MAX_SPEED_CENTI_KPH,
    MIN_BATTERY_MV,
    MIN_COOLANT_C,
    ProtocolError,
    Receiver,
    Sample,
    Status,
    crc8,
    decode,
    encode,
    parse_hex_frame,
)


class ProtocolReferenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.sample = Sample(12_345, 92, 14_320)

    def assert_status(self, status: Status, operation) -> None:  # type: ignore[no-untyped-def]
        with self.assertRaises(ProtocolError) as caught:
            operation()
        self.assertEqual(caught.exception.status, status)

    def test_crc_standard_check_vector(self) -> None:
        self.assertEqual(crc8(b"123456789"), 0x4B)

    def test_golden_wire_vector(self) -> None:
        frame = encode(self.sample, sequence=42, flags=ALLOWED_FLAGS)
        self.assertEqual(frame.hex(" "), "2a 39 30 84 f0 37 03 be")
        self.assertEqual(decode(frame).sample, self.sample)

    def test_valid_boundaries(self) -> None:
        boundaries = (
            Sample(0, MIN_COOLANT_C, MIN_BATTERY_MV),
            Sample(MAX_SPEED_CENTI_KPH, MAX_COOLANT_C, MAX_BATTERY_MV),
        )
        for sample in boundaries:
            with self.subTest(sample=sample):
                self.assertEqual(decode(encode(sample, 255)).sample, sample)

    def test_encoder_rejects_all_out_of_range_fields(self) -> None:
        invalid = (
            Sample(MAX_SPEED_CENTI_KPH + 1, 90, 14_000),
            Sample(10_000, MIN_COOLANT_C - 1, 14_000),
            Sample(10_000, MAX_COOLANT_C + 1, 14_000),
            Sample(10_000, 90, MIN_BATTERY_MV - 1),
            Sample(10_000, 90, MAX_BATTERY_MV + 1),
        )
        for sample in invalid:
            with self.subTest(sample=sample):
                self.assert_status(Status.RANGE, lambda sample=sample: encode(sample, 0))

    def test_corrupted_payload_fails_crc(self) -> None:
        frame = bytearray(encode(self.sample, 1))
        frame[1] ^= 0x01
        self.assert_status(Status.CRC, lambda: decode(bytes(frame)))

    def test_semantically_invalid_payload_with_valid_crc_fails_range(self) -> None:
        frame = bytearray(encode(self.sample, 1))
        frame[1:3] = (65_535).to_bytes(2, "little")
        frame[7] = crc8(frame[:7])
        self.assert_status(Status.RANGE, lambda: decode(bytes(frame)))

    def test_reserved_flag_with_valid_crc_is_rejected(self) -> None:
        frame = bytearray(encode(self.sample, 1))
        frame[6] = 0x80
        frame[7] = crc8(frame[:7])
        self.assert_status(Status.FLAGS, lambda: decode(bytes(frame)))

    def test_wrong_length_is_rejected_before_crc(self) -> None:
        frame = encode(self.sample, 1)
        self.assert_status(Status.LENGTH, lambda: decode(frame[:-1]))

    def test_hex_parser_accepts_documented_forms(self) -> None:
        expected = encode(self.sample, 42, ALLOWED_FLAGS)
        compact = expected.hex()
        self.assertEqual(parse_hex_frame(compact), expected)
        self.assertEqual(parse_hex_frame(expected.hex(" ")), expected)
        self.assertEqual(parse_hex_frame(expected.hex(":")), expected)


class ReceiverReferenceTests(unittest.TestCase):
    sample = Sample(5_000, 85, 13_900)

    def test_sequence_wraps_at_255(self) -> None:
        receiver = Receiver(timeout_ms=250)
        for sequence in (254, 255, 0, 1):
            decoded = receiver.process(encode(self.sample, sequence), sequence * 10)
            self.assertEqual(decoded.sequence, sequence)
        self.assertEqual(receiver.stats.accepted_frames, 4)

    def test_sequence_failure_resynchronizes_next_frame(self) -> None:
        receiver = Receiver(timeout_ms=250)
        receiver.process(encode(self.sample, 10), 0)
        with self.assertRaises(ProtocolError) as caught:
            receiver.process(encode(self.sample, 12), 20)
        self.assertEqual(caught.exception.status, Status.SEQUENCE)
        receiver.process(encode(self.sample, 13), 40)
        self.assertEqual(receiver.stats.sequence_errors, 1)
        self.assertEqual(receiver.stats.accepted_frames, 2)

    def test_timeout_counts_only_state_transitions_and_handles_rollover(self) -> None:
        receiver = Receiver(timeout_ms=32)
        receiver.process(encode(self.sample, 1), 0xFFFFFFF0)
        self.assertFalse(receiver.check_timeout(15))
        self.assertTrue(receiver.check_timeout(17))
        self.assertTrue(receiver.check_timeout(18))
        self.assertEqual(receiver.stats.timeout_events, 1)
        receiver.process(encode(self.sample, 2), 20)
        self.assertFalse(receiver.timed_out)

    def test_decode_and_can_errors_are_counted(self) -> None:
        receiver = Receiver(timeout_ms=250)
        frame = bytearray(encode(self.sample, 1))
        frame[2] ^= 0x80
        with self.assertRaises(ProtocolError):
            receiver.process(bytes(frame), 0)
        self.assertEqual(receiver.stats.crc_errors, 1)
        self.assertEqual(receiver.note_can_error(), Status.CAN)
        self.assertEqual(receiver.stats.can_errors, 1)


if __name__ == "__main__":
    unittest.main()
