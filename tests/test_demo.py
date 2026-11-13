from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from scripts.create_demo import (
    DemoError,
    REPOSITORY_ROOT,
    SCENARIOS,
    display_executable,
    find_executables,
    parse_simulator,
    parse_suite,
    render_html,
    run_capture,
)
from tools.telemetry_reference import Sample, crc8, encode


def simulator_output(scenario_id: str) -> str:
    """Generate parser fixtures, never published as runtime capture evidence."""
    lines = ["CAN telemetry simulation: id=0x321 bitrate=500000 frame_bytes=8"]
    for index in range(6):
        sequence = index + (scenario_id == "sequence" and index >= 2)
        sample = Sample(4200 + index * 37, 82 + index, 13800 + index * 5)
        payload = bytearray(encode(sample, sequence, 3))
        status = "ok"
        if index == 2:
            if scenario_id == "can":
                lines.extend(["EVENT can controller error", f"RX[{index}] controller did not deliver a payload"])
                continue
            if scenario_id == "crc":
                payload[1] ^= 1
                status = "crc mismatch"
            elif scenario_id == "range":
                payload[1:3] = b"\xff\xff"
                payload[7] = crc8(payload[:7])
                status = "value out of range"
            elif scenario_id == "sequence":
                status = "sequence mismatch"
            elif scenario_id == "timeout":
                lines.append("EVENT timeout elapsed_ms=271")
        if index == 3 and scenario_id in ("crc", "range", "can"):
            status = "sequence mismatch"
        lines.append(f"RX[{index}] {payload.hex(' ').upper()} -> {status}")
        if status == "ok":
            speed = sample.speed_centi_kph
            lines.append(f"UART> TEL,{sequence},{speed // 100}.{speed % 100:02d},{sample.coolant_c},{sample.battery_mv},3")
    totals = next(values for name, _, values in SCENARIOS if name == scenario_id)
    labels = ("accepted", "crc", "range", "sequence", "timeout", "can")
    lines.append("SUMMARY " + " ".join(f"{key}={value}" for key, value in zip(labels, totals)))
    return "\r\n".join(lines) + "\r\n"


class DemoCaptureTests(unittest.TestCase):
    def test_all_scenarios_capture_decisions_and_uart_evidence(self) -> None:
        for scenario_id, _, expected in SCENARIOS:
            with self.subTest(scenario=scenario_id):
                captured = parse_simulator(simulator_output(scenario_id), scenario_id)
                self.assertEqual(captured["summary"]["accepted"], expected[0])
                self.assertEqual(len(captured["frames"]), 6)
                self.assertEqual(sum(frame["uart"] is not None for frame in captured["frames"]), expected[0])
        can_frame = parse_simulator(simulator_output("can"), "can")["frames"][2]
        self.assertEqual(can_frame, {"index": 2, "payload": [], "status": "can controller error", "uart": None})

    def test_incomplete_duplicate_or_contradictory_capture_is_rejected(self) -> None:
        nominal = simulator_output("none")
        mutations = [
            nominal.replace("accepted=6", "accepted=5"),
            nominal.replace("RX[1]", "RX[2]"),
            nominal.replace(" -> ok", " -> unknown", 1),
            nominal.replace("UART> TEL,0,42.00,82,13800,3\r\n", "", 1),
            nominal.replace("UART> TEL,0,42.00", "UART> TEL,0,99.00", 1),
            nominal.replace("UART> TEL,0,42.00,82,13800,3", "UART> TEL,0,42.00,82,13800,3\r\nUART> TEL,0,42.00,82,13800,3", 1),
            nominal + "SUMMARY accepted=6 crc=0 range=0 sequence=0 timeout=0 can=0\n",
            nominal.split("SUMMARY")[0],
            nominal.replace("RX[0] ", "RX[0] FF ", 1),
            nominal.replace("CAN telemetry simulation: id=0x321 bitrate=500000 frame_bytes=8\r\n", ""),
        ]
        for index, output in enumerate(mutations):
            with self.subTest(mutation=index), self.assertRaises(DemoError):
                parse_simulator(output, "none")

    def test_rejected_frame_cannot_be_forwarded(self) -> None:
        output = simulator_output("crc").replace("-> crc mismatch", "-> crc mismatch\r\nUART> TEL,2,42.74,84,13810,3")
        with self.assertRaisesRegex(DemoError, "accepted frame"):
            parse_simulator(output, "crc")

    def test_timeout_requires_actual_elapsed_gap_and_event(self) -> None:
        output = simulator_output("timeout")
        for modified in (output.replace("elapsed_ms=271", "elapsed_ms=251"),
                         output.replace("EVENT timeout elapsed_ms=271\r\n", "")):
            with self.subTest(output=modified), self.assertRaisesRegex(DemoError, "expected events"):
                parse_simulator(modified, "timeout")

    def test_suite_requires_named_unique_passes_and_zero_assertions(self) -> None:
        good = "PASS wire order\nPASS timeout\n2 tests, 0 failed assertions\n"
        self.assertEqual(parse_suite(good, 2), {
            "cases": [{"name": "wire order", "status": "PASS"}, {"name": "timeout", "status": "PASS"}],
            "total": 2,
        })
        for bad in (good.replace("PASS timeout", "FAIL timeout"),
                    good.replace("0 failed", "1 failed"),
                    good.replace("2 tests", "3 tests"),
                    good.replace("PASS timeout\n", ""),
                    good.replace("timeout", "wire order"),
                    good + "PASS hidden\n"):
            with self.subTest(output=bad), self.assertRaises(DemoError):
                parse_suite(bad, 2)

    def test_html_embedding_cannot_close_script_and_preserves_data(self) -> None:
        evidence = {"stdout": "</script><script>alert('&')</script>\u2028\u2029"}
        template = "<script>const data = __TELEMETRY_DEMO_DATA__;</script>"
        html = render_html(template, evidence)
        self.assertEqual(html.count("</script>"), 1)
        embedded = html.removeprefix("<script>const data = ").removesuffix(";</script>")
        self.assertNotIn("<", embedded)
        self.assertNotIn(">", embedded)
        self.assertNotIn("&", embedded)
        self.assertEqual(json.loads(embedded), evidence)

    def test_missing_or_duplicate_template_marker_is_rejected(self) -> None:
        for template in ("no marker", "__TELEMETRY_DEMO_DATA__ __TELEMETRY_DEMO_DATA__"):
            with self.subTest(template=template), self.assertRaises(DemoError):
                render_html(template, {})

    def test_missing_executables_give_rebuild_instructions(self) -> None:
        with tempfile.TemporaryDirectory(dir=REPOSITORY_ROOT) as temporary:
            build_dir = Path(temporary)
            with self.assertRaisesRegex(DemoError, "cmake --build build"):
                find_executables(build_dir)
            # Partial builds must not appear to provide complete runtime evidence.
            (build_dir / "telemetry_sim.exe").write_bytes(b"fixture")
            with self.assertRaises(DemoError):
                find_executables(build_dir)

    def test_debug_executable_discovery_keeps_display_paths_relative(self) -> None:
        with tempfile.TemporaryDirectory(dir=REPOSITORY_ROOT) as temporary:
            build_dir = Path(temporary)
            debug = build_dir / "Debug"
            debug.mkdir()
            for name in ("telemetry_sim", "telemetry_tests", "telemetry_hal_tests"):
                (debug / f"{name}.exe").write_bytes(b"fixture")
            found = find_executables(build_dir)
            self.assertEqual(len(found), 3)
            self.assertTrue(all(path.parent == debug.resolve() for path in found.values()))
            displayed = display_executable(found["telemetry_sim"], build_dir, build_dir)
            self.assertEqual(displayed, "Debug/telemetry_sim.exe")
            external = display_executable(found["telemetry_sim"], build_dir / "other-root", build_dir)
            self.assertEqual(external, "<build-dir>/Debug/telemetry_sim.exe")

    def test_capture_rejects_nonzero_exit_stderr_and_timeout(self) -> None:
        with patch("scripts.create_demo.subprocess.run") as run:
            run.return_value = subprocess.CompletedProcess(["sim"], 0, "captured", "")
            self.assertEqual(run_capture(["sim"], REPOSITORY_ROOT), "captured")
            self.assertEqual(run.call_args.kwargs["timeout"], 15)
            for returncode, stderr in ((1, ""), (0, "unexpected failure")):
                run.return_value = subprocess.CompletedProcess(["sim"], returncode, "captured", stderr)
                with self.subTest(returncode=returncode, stderr=stderr), self.assertRaises(DemoError):
                    run_capture(["sim"], REPOSITORY_ROOT)
            run.side_effect = subprocess.TimeoutExpired(["sim"], 15)
            with self.assertRaisesRegex(DemoError, "capture limit"):
                run_capture(["sim"], REPOSITORY_ROOT)


if __name__ == "__main__":
    unittest.main()
