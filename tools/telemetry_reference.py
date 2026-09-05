"""Executable reference model for the eight-byte vehicle telemetry payload.

The embedded implementation in ``src/`` is authoritative for deployment. This
module intentionally mirrors it with only Python's standard library so protocol
vectors and failure behavior can be checked on a machine without a C compiler.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum

CAN_ID = 0x321
CAN_BITRATE = 500_000
FRAME_SIZE = 8

MAX_SPEED_CENTI_KPH = 30_000
MIN_COOLANT_C = -40
MAX_COOLANT_C = 150
MIN_BATTERY_MV = 6_000
MAX_BATTERY_MV = 18_000

FLAG_ENGINE_RUNNING = 0x01
FLAG_CHARGING = 0x02
ALLOWED_FLAGS = FLAG_ENGINE_RUNNING | FLAG_CHARGING


class Status(str, Enum):
    """Stable validation outcomes shared conceptually with the C API."""

    OK = "ok"
    LENGTH = "invalid length"
    CRC = "crc mismatch"
    RANGE = "value out of range"
    FLAGS = "reserved flag set"
    SEQUENCE = "sequence mismatch"
    TIMEOUT = "receive timeout"
    CAN = "can controller error"


class ProtocolError(ValueError):
    """A frame or sample violated the protocol contract."""

    def __init__(self, status: Status) -> None:
        super().__init__(status.value)
        self.status = status


@dataclass(frozen=True)
class Sample:
    """Engineering values represented entirely with deterministic integers."""

    speed_centi_kph: int
    coolant_c: int
    battery_mv: int


@dataclass(frozen=True)
class Decoded:
    sample: Sample
    sequence: int
    flags: int


@dataclass
class ReceiverStats:
    accepted_frames: int = 0
    crc_errors: int = 0
    range_errors: int = 0
    flag_errors: int = 0
    length_errors: int = 0
    sequence_errors: int = 0
    timeout_events: int = 0
    can_errors: int = 0


def crc8(data: bytes) -> int:
    """Return CRC-8/SAE-J1850 (poly 0x1D, init/xorout 0xFF)."""

    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc ^ 0xFF


def _validate_sample(sample: Sample) -> None:
    if not (
        0 <= sample.speed_centi_kph <= MAX_SPEED_CENTI_KPH
        and MIN_COOLANT_C <= sample.coolant_c <= MAX_COOLANT_C
        and MIN_BATTERY_MV <= sample.battery_mv <= MAX_BATTERY_MV
    ):
        raise ProtocolError(Status.RANGE)


def encode(sample: Sample, sequence: int, flags: int = 0) -> bytes:
    """Encode and validate a sample into the canonical eight-byte payload."""

    _validate_sample(sample)
    if not 0 <= sequence <= 0xFF:
        raise ProtocolError(Status.RANGE)
    if not 0 <= flags <= 0xFF or flags & ~ALLOWED_FLAGS:
        raise ProtocolError(Status.FLAGS)

    payload = bytearray(FRAME_SIZE)
    payload[0] = sequence
    payload[1:3] = sample.speed_centi_kph.to_bytes(2, "little")
    payload[3] = sample.coolant_c + 40
    payload[4:6] = sample.battery_mv.to_bytes(2, "little")
    payload[6] = flags
    payload[7] = crc8(payload[:7])
    return bytes(payload)


def decode(frame: bytes) -> Decoded:
    """Validate and decode a canonical payload; raise ``ProtocolError`` on failure."""

    if len(frame) != FRAME_SIZE:
        raise ProtocolError(Status.LENGTH)
    if crc8(frame[:7]) != frame[7]:
        raise ProtocolError(Status.CRC)
    if frame[6] & ~ALLOWED_FLAGS:
        raise ProtocolError(Status.FLAGS)

    sample = Sample(
        speed_centi_kph=int.from_bytes(frame[1:3], "little"),
        coolant_c=frame[3] - 40,
        battery_mv=int.from_bytes(frame[4:6], "little"),
    )
    _validate_sample(sample)
    return Decoded(sample=sample, sequence=frame[0], flags=frame[6])


@dataclass
class Receiver:
    """Stateful sequence, timeout, and diagnostic-counter reference model."""

    timeout_ms: int
    stats: ReceiverStats = field(default_factory=ReceiverStats)
    last_accepted_ms: int = 0
    last_sequence: int = 0
    synchronized: bool = False
    timed_out: bool = False

    def process(self, frame: bytes, now_ms: int) -> Decoded:
        """Process a frame using uint32-style millisecond timestamps."""

        try:
            candidate = decode(frame)
        except ProtocolError as error:
            counter_for_status = {
                Status.CRC: "crc_errors",
                Status.RANGE: "range_errors",
                Status.FLAGS: "flag_errors",
                Status.LENGTH: "length_errors",
            }.get(error.status)
            if counter_for_status is not None:
                setattr(
                    self.stats,
                    counter_for_status,
                    getattr(self.stats, counter_for_status) + 1,
                )
            raise

        if self.synchronized and candidate.sequence != (self.last_sequence + 1) & 0xFF:
            self.stats.sequence_errors += 1
            self.last_sequence = candidate.sequence
            raise ProtocolError(Status.SEQUENCE)

        self.synchronized = True
        self.timed_out = False
        self.last_sequence = candidate.sequence
        self.last_accepted_ms = now_ms & 0xFFFFFFFF
        self.stats.accepted_frames += 1
        return candidate

    def check_timeout(self, now_ms: int) -> bool:
        if not self.synchronized or self.timeout_ms == 0:
            return False
        elapsed = ((now_ms & 0xFFFFFFFF) - self.last_accepted_ms) & 0xFFFFFFFF
        expired = elapsed > self.timeout_ms
        if expired and not self.timed_out:
            self.timed_out = True
            self.stats.timeout_events += 1
        return expired

    def note_can_error(self) -> Status:
        self.stats.can_errors += 1
        return Status.CAN


def parse_hex_frame(text: str) -> bytes:
    """Parse ``AA BB``/``AA:BB``/``AABB`` forms into one payload."""

    compact = text.strip().replace(" ", "").replace(":", "").replace("-", "")
    try:
        frame = bytes.fromhex(compact)
    except ValueError as error:
        raise ValueError(f"not hexadecimal: {text!r}") from error
    if len(frame) != FRAME_SIZE:
        raise ValueError(f"expected {FRAME_SIZE} bytes, got {len(frame)}")
    return frame


def frame_to_dict(decoded: Decoded) -> dict[str, int | float]:
    """Convert decoded data into a JSON-friendly engineering-value mapping."""

    return {
        "sequence": decoded.sequence,
        "speed_kph": decoded.sample.speed_centi_kph / 100.0,
        "coolant_c": decoded.sample.coolant_c,
        "battery_v": decoded.sample.battery_mv / 1000.0,
        "flags": decoded.flags,
    }

