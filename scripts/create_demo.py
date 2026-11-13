#!/usr/bin/env python3
"""Capture compiled telemetry evidence and render a standalone demo playback.

No simulated results are substituted when an executable is missing or fails.
Source and binary hashes identify the files present during capture; they do
not, by themselves, establish that a binary was built from those sources.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_TOKEN = "__TELEMETRY_DEMO_DATA__"
CAPTURE_TIMEOUT_SECONDS = 15
SUMMARY_KEYS = ("accepted", "crc", "range", "sequence", "timeout", "can")
SCENARIOS = (
    ("none", "Nominal telemetry", (6, 0, 0, 0, 0, 0)),
    ("crc", "Corruption detection", (4, 1, 0, 1, 0, 0)),
    ("range", "Semantic range rejection", (4, 0, 1, 1, 0, 0)),
    ("sequence", "Sequence discontinuity and recovery", (5, 0, 0, 1, 0, 0)),
    ("timeout", "Receive outage and recovery", (6, 0, 0, 0, 1, 0)),
    ("can", "CAN controller error and recovery", (4, 0, 0, 1, 0, 1)),
)
SOURCE_FILES = (
    "CMakeLists.txt",
    "apps/simulator.cpp",
    "demo/template.html",
    "include/telemetry/protocol.h",
    "include/telemetry/receiver.h",
    "platform/stm32f446re/telemetry_stm32_hal.c",
    "platform/stm32f446re/telemetry_stm32_hal.h",
    "scripts/create_demo.py",
    "src/protocol.c",
    "src/receiver.c",
    "tests/mocks/stm32f4xx_hal.h",
    "tests/test_demo.py",
    "tests/test_stm32_hal.c",
    "tests/test_telemetry.c",
)
FRAME_RE = re.compile(r"RX\[(\d+)\] ((?:[0-9A-F]{2} ){7}[0-9A-F]{2})\s+-> (.+)")
MISSING_RE = re.compile(r"RX\[(\d+)\] controller did not deliver a payload")
SUMMARY_RE = re.compile(
    r"SUMMARY accepted=(\d+) crc=(\d+) range=(\d+) sequence=(\d+) timeout=(\d+) can=(\d+)"
)
TOTAL_RE = re.compile(r"(\d+) tests, (\d+) failed assertions")


class DemoError(RuntimeError):
    """An evidence capture failed or did not match the demonstrated contract."""


def expected_statuses(scenario_id: str) -> list[str]:
    statuses = ["ok"] * 6
    if scenario_id in ("crc", "range", "can"):
        statuses[2] = {
            "crc": "crc mismatch",
            "range": "value out of range",
            "can": "can controller error",
        }[scenario_id]
        statuses[3] = "sequence mismatch"
    elif scenario_id == "sequence":
        statuses[2] = "sequence mismatch"
    elif scenario_id not in ("none", "timeout"):
        raise DemoError(f"Unknown demo scenario: {scenario_id}")
    return statuses


def uart_for_payload(payload: list[str]) -> str:
    raw = bytes.fromhex(" ".join(payload))
    speed = int.from_bytes(raw[1:3], "little")
    battery = int.from_bytes(raw[4:6], "little")
    return (
        f"TEL,{raw[0]},{speed // 100}.{speed % 100:02d},"
        f"{raw[3] - 40},{battery},{raw[6]}"
    )


def parse_simulator(stdout: str, scenario_id: str) -> dict[str, object]:
    """Reject incomplete, contradictory, or unexpectedly successful traces."""
    definitions = {item[0]: item[2] for item in SCENARIOS}
    if scenario_id not in definitions:
        raise DemoError(f"Unknown demo scenario: {scenario_id}")
    frames: list[dict[str, object]] = []
    events: list[str] = []
    summary = None
    header_seen = False
    for line in stdout.splitlines():
        if not line:
            continue
        if summary is not None:
            raise DemoError(f"{scenario_id}: unexpected output after summary")
        if line == "CAN telemetry simulation: id=0x321 bitrate=500000 frame_bytes=8":
            if header_seen or frames or events:
                raise DemoError(f"{scenario_id}: misplaced simulator header")
            header_seen = True
            continue
        if not header_seen:
            raise DemoError(f"{scenario_id}: missing simulator header")
        frame_match = FRAME_RE.fullmatch(line)
        missing_match = MISSING_RE.fullmatch(line)
        if frame_match or missing_match:
            match = frame_match or missing_match
            assert match is not None
            index = int(match[1])
            if index != len(frames):
                raise DemoError(f"{scenario_id}: frame indices must be consecutive from zero")
            frames.append({
                "index": index,
                "payload": frame_match[2].split() if frame_match else [],
                "status": frame_match[3] if frame_match else "can controller error",
                "uart": None,
            })
        elif line.startswith("UART> "):
            if not frames or frames[-1]["status"] != "ok" or frames[-1]["uart"] is not None:
                raise DemoError(f"{scenario_id}: UART output must map once to an accepted frame")
            payload = frames[-1]["payload"]
            assert isinstance(payload, list)
            uart = line.removeprefix("UART> ")
            if uart != uart_for_payload(payload):
                raise DemoError(f"{scenario_id}: UART values do not match the accepted payload")
            frames[-1]["uart"] = uart
        elif line.startswith("EVENT "):
            events.append(line)
        elif match := SUMMARY_RE.fullmatch(line):
            summary = dict(zip(SUMMARY_KEYS, map(int, match.groups())))
        else:
            raise DemoError(f"{scenario_id}: unrecognized simulator output")
    expected_summary = dict(zip(SUMMARY_KEYS, definitions[scenario_id]))
    if summary != expected_summary:
        raise DemoError(f"{scenario_id}: expected summary {expected_summary}, got {summary}")
    if [frame["status"] for frame in frames] != expected_statuses(scenario_id):
        raise DemoError(f"{scenario_id}: frame decisions do not match the expected fault/recovery sequence")
    for frame in frames:
        if (frame["status"] == "ok") != (frame["uart"] is not None):
            raise DemoError(f"{scenario_id}: accepted-only UART mapping is incomplete")
        if (frame["status"] == "can controller error") != (frame["payload"] == []):
            raise DemoError(f"{scenario_id}: missing-payload evidence is inconsistent")
    if sum(frame["uart"] is not None for frame in frames) != summary["accepted"]:
        raise DemoError(f"{scenario_id}: UART count differs from accepted count")
    expected_events = {
        "timeout": ["EVENT timeout elapsed_ms=271"],
        "can": ["EVENT can controller error"],
    }.get(scenario_id, [])
    if events != expected_events:
        raise DemoError(f"{scenario_id}: expected events {expected_events}, got {events}")
    return {"summary": summary, "frames": frames, "events": events}


def parse_suite(stdout: str, expected_total: int) -> dict[str, object]:
    cases: list[dict[str, str]] = []
    total = None
    for line in stdout.splitlines():
        if not line:
            continue
        if total is not None:
            raise DemoError("Test suite has output after its final total")
        if line.startswith("PASS ") and line[5:].strip():
            cases.append({"name": line[5:], "status": "PASS"})
        elif match := TOTAL_RE.fullmatch(line):
            total, failed = map(int, match.groups())
            if failed != 0:
                raise DemoError("Test suite reports failed assertions")
        else:
            raise DemoError("Test suite reports FAIL or unrecognized output")
    if total != expected_total or len(cases) != expected_total:
        raise DemoError(f"Expected {expected_total} named passing cases and matching suite total")
    if len({case["name"] for case in cases}) != expected_total:
        raise DemoError("Test suite repeats a case name")
    return {"cases": cases, "total": total}


def find_executables(build_dir: Path) -> dict[str, Path]:
    names = ("telemetry_sim", "telemetry_tests", "telemetry_hal_tests")
    for directory in (build_dir, build_dir / "Debug", build_dir / "Release",
                      build_dir / "RelWithDebInfo", build_dir / "MinSizeRel"):
        found = {}
        for name in names:
            for candidate in (directory / f"{name}.exe", directory / name):
                if candidate.is_file():
                    found[name] = candidate.resolve()
                    break
        if len(found) == len(names):
            return found
    raise DemoError(
        "A complete simulator/protocol/HAL executable set was not found in the build directory. "
        "Build first: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug; "
        "cmake --build build --config Debug --parallel. "
        "Then run python scripts/create_demo.py --build-dir build "
        "(or pass your existing build directory)."
    )


def display_executable(executable: Path, root: Path, build_dir: Path) -> str:
    try:
        return executable.relative_to(root.resolve()).as_posix()
    except ValueError:
        return "<build-dir>/" + executable.relative_to(build_dir.resolve()).as_posix()


def run_capture(command: list[str], root: Path) -> str:
    try:
        result = subprocess.run(
            command, cwd=root, capture_output=True, text=True, encoding="utf-8",
            timeout=CAPTURE_TIMEOUT_SECONDS, check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise DemoError(f"{Path(command[0]).name} exceeded the {CAPTURE_TIMEOUT_SECONDS}s capture limit") from error
    except (OSError, UnicodeError) as error:
        raise DemoError(f"Could not run {Path(command[0]).name}; rebuild the native executables") from error
    if result.returncode != 0:
        raise DemoError(f"{Path(command[0]).name} exited with status {result.returncode}; capture aborted")
    if result.stderr.strip():
        raise DemoError(f"{Path(command[0]).name} wrote unexpected stderr; capture aborted")
    return result.stdout


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_metadata(root: Path) -> tuple[str | None, bool | None]:
    values = []
    for arguments in (("rev-parse", "HEAD"), ("status", "--porcelain", "--untracked-files=normal")):
        try:
            result = subprocess.run(
                ["git", *arguments], cwd=root, capture_output=True, text=True,
                encoding="utf-8", timeout=10, check=False,
            )
        except (OSError, UnicodeError, subprocess.TimeoutExpired):
            return None, None
        if result.returncode != 0:
            return None, None
        values.append(result.stdout.strip())
    if not re.fullmatch(r"[0-9a-f]{40,64}", values[0]):
        return None, None
    return values[0], bool(values[1])


def capture_evidence(root: Path, build_dir: Path) -> dict[str, object]:
    executables = find_executables(build_dir)
    source_hashes = {name: file_sha256(root / name) for name in SOURCE_FILES}
    binary_hashes = {
        display_executable(path, root, build_dir): file_sha256(path)
        for path in executables.values()
    }
    revision, dirty = git_metadata(root)
    scenarios = []
    for scenario_id, title, _ in SCENARIOS:
        executable = executables["telemetry_sim"]
        arguments = ["--frames", "6", "--fault", scenario_id, "--fault-at", "2"]
        stdout = run_capture([str(executable), *arguments], root)
        scenarios.append({
            "id": scenario_id, "title": title,
            "command": [display_executable(executable, root, build_dir), *arguments],
            "exit_code": 0, **parse_simulator(stdout, scenario_id), "stdout": stdout,
        })
    suites = []
    for suite_id, title, name, total in (
        ("protocol", "C protocol host tests", "telemetry_tests", 8),
        ("hal", "STM32 adapter with HAL/CMSIS mocks", "telemetry_hal_tests", 15),
    ):
        executable = executables[name]
        stdout = run_capture([str(executable)], root)
        suites.append({
            "id": suite_id, "title": title,
            "command": [display_executable(executable, root, build_dir)],
            "exit_code": 0, **parse_suite(stdout, total), "stdout": stdout,
        })
    if source_hashes != {name: file_sha256(root / name) for name in SOURCE_FILES}:
        raise DemoError("Source files changed during capture; retry after edits finish")
    if binary_hashes != {
        display_executable(path, root, build_dir): file_sha256(path)
        for path in executables.values()
    }:
        raise DemoError("Executables changed during capture; rebuild and retry")
    return {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
        "git_revision": revision, "working_tree_dirty": dirty,
        "source_sha256": source_hashes, "binary_sha256": binary_hashes,
        "scenarios": scenarios, "suites": suites,
    }


def render_html(template: str, evidence: dict[str, object]) -> str:
    if template.count(TEMPLATE_TOKEN) != 1:
        raise DemoError(f"Demo template must contain exactly one {TEMPLATE_TOKEN} token")
    safe_json = json.dumps(evidence, ensure_ascii=False, indent=2)
    for character, escaped in (("&", "\\u0026"), ("<", "\\u003c"), (">", "\\u003e"),
                               ("\u2028", "\\u2028"), ("\u2029", "\\u2029")):
        safe_json = safe_json.replace(character, escaped)
    return template.replace(TEMPLATE_TOKEN, safe_json)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"),
                        help="directory containing freshly built simulator and test executables")
    parser.add_argument("--output", type=Path, default=Path("demo/index.html"),
                        help="standalone HTML output; evidence.json is written beside it")
    arguments = parser.parse_args(argv)
    build_dir = arguments.build_dir.resolve()
    output = arguments.output.resolve()
    try:
        if output.name.lower() == "evidence.json":
            raise DemoError("Choose an HTML output name; evidence.json is reserved for captured results")
        template = (REPOSITORY_ROOT / "demo/template.html").read_text(encoding="utf-8")
        # Check the template before running any executable or replacing output.
        render_html(template, {})
        evidence = capture_evidence(REPOSITORY_ROOT, build_dir)
        html = render_html(template, evidence)
        output.parent.mkdir(parents=True, exist_ok=True)
        (output.parent / "evidence.json").write_text(
            json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8",
        )
        output.write_text(html, encoding="utf-8")
    except (DemoError, OSError) as error:
        print(f"Demo capture failed: {error}", file=sys.stderr)
        return 1
    print(f"Captured 6 simulator scenarios, 8 protocol cases, and 15 HAL mock cases.\nDemo: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
