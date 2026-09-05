#!/usr/bin/env python3
"""Decode telemetry payloads from command arguments or newline-delimited stdin."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT))

from tools.telemetry_reference import (  # noqa: E402
    ProtocolError,
    Receiver,
    decode,
    frame_to_dict,
    parse_hex_frame,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate and decode eight-byte vehicle telemetry frames."
    )
    parser.add_argument(
        "frames",
        nargs="*",
        metavar="HEX",
        help="frame as 16 hex digits (spaces/colons/dashes are accepted)",
    )
    parser.add_argument(
        "--stateful",
        action="store_true",
        help="also enforce consecutive sequence numbers",
    )
    parser.add_argument("--start-ms", type=int, default=0)
    parser.add_argument("--step-ms", type=int, default=20)
    parser.add_argument("--timeout-ms", type=int, default=250)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    inputs = arguments.frames or [line.strip() for line in sys.stdin if line.strip()]
    receiver = Receiver(timeout_ms=arguments.timeout_ms)
    failures = 0

    for index, text in enumerate(inputs):
        result: dict[str, object] = {"input": text}
        try:
            frame = parse_hex_frame(text)
            decoded = (
                receiver.process(frame, arguments.start_ms + index * arguments.step_ms)
                if arguments.stateful
                else decode(frame)
            )
            result.update({"status": "ok", **frame_to_dict(decoded)})
        except (ProtocolError, ValueError) as error:
            result["status"] = "rejected"
            result["reason"] = str(error)
            failures += 1
        print(json.dumps(result, sort_keys=True))

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

