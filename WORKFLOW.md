# CAN telemetry project workflow

Repository: [RohitPatel97/can-vehicle-telemetry](https://github.com/RohitPatel97/can-vehicle-telemetry)

## Completed implementation

- Portable C11 wire format with CRC-8, semantic validation and receiver state.
- STM32 adapter with a 16-frame IRQ-to-main circular buffer, bounded drain,
  receive timestamps, envelope checks, accepted-only UART, and diagnostics.
- Outage regression fix: queued frames received at 0 ms and 300 ms trigger a
  250 ms timeout event even if recovery is accepted in the same main-loop poll.
- Explicit unsigned CRC arithmetic for strict Clang conversion warnings.
- Backlog timeout fix: current-clock checks wait until queued receive history
  has been processed, with an atomic queue-empty/tick snapshot.
- Eight protocol cases, fifteen HAL integration cases, thirteen Python reference
  cases, and six CTest simulator scenarios with expected counter summaries.
- Offline demo capture runner with six recorded simulator scenarios, compiled
  protocol/HAL suite results, ten capture-validation tests, and presenter notes.
- [README](README.md) project breakdown and [test case guide](docs/test-cases.md)
  describe the problem, solution, inputs and expected evidence.

## Verification commands

November 13, 2026 local result: all 8 CTest entries and 23 Python tests passed
(13 protocol/reference cases plus 10 demo capture-validation cases). The
simulator was rebuilt before capturing the demo; execution remains host-only.

October 1, 2026 local result: 8 C protocol cases, 15 HAL mock cases,
13 Python cases, and 8/8 CTest entries passed. A Windows CMake/Ninja build
used Zig 0.16.0/Clang 21.1.0 with explicit Zig archive rules. This is host
verification; the mock is not a complete STM32CubeF4 firmware build.

September 4, 2026 local result: 8 C protocol cases, 12 HAL mock cases,
13 Python cases, and 8/8 CTest entries passed. Native Zig 0.16.0/Clang 21.1.0
built the executables directly because Windows CMake did not detect an archive
tool. See the README for the distinction between this run and full CI builds.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
```

CI adds GCC, Clang and GCC AddressSanitizer/UndefinedBehaviorSanitizer builds.
Check the [latest CI run](https://github.com/RohitPatel97/can-vehicle-telemetry/actions)
against the commit being reviewed; a configured job is not evidence that it ran.
Local execution evidence is recorded in the README verification snapshot.

## Change record — 2026-11-13

- **Problem:** the project had executable tests and simulator logs but no
  presentation-ready walkthrough connecting fault diagnosis, reliability
  decisions, and evidence for an interview discussion.
- **Solution:** add a standard-library Python runner that captures six scenarios
  from the compiled simulator and the protocol/HAL suites. It validates the
  records before embedding them in an offline interactive HTML viewer. The
  viewer exposes payload bytes, receiver decisions, UART rows, scenario totals,
  raw logs, provenance, and a separately labelled backlog explanation.
- **Regression cases:** ten new Python tests reject incomplete or contradictory
  logs, forwarded rejected frames, incorrect UART values, false suite success,
  missing binaries, subprocess failures/timeouts, and unsafe JSON embedding.
  The simulator timeout message now reports 271 ms, including the normal 20 ms
  interval, and capture validation rejects the previous 251 ms diagnostic.
- **Observed result:** rebuilt the simulator with the existing Windows
  CMake/Ninja Zig configuration; all 8 CTest entries and 23 Python tests passed.
  `python scripts/create_demo.py --build-dir build-review` captured all six
  scenarios, 8 protocol cases, and 15 HAL mock cases. Browser verification checked
  all 36 frame decisions/payloads/UART rows against the evidence JSON, keyboard
  playback, cross-scenario navigation, and the matching JSON download. Desktop
  and 390-pixel mobile layouts had no horizontal overflow or script errors;
  offline playback made no external network requests.
- **Documentation:** README links to the demo; `docs/demo-walkthrough.md` provides
  a five-minute presenter guide and technical follow-up explanations. The
  framing comes from interview preparation themes, not an interview transcript.
- **Remaining limits:** playback is recorded host execution, not a live CAN bus.
  HAL timing inputs are synthetic. Physical CAN/UART measurements and a full
  Cube firmware build remain pending. Hashes identify captured source/binary
  files but do not prove how an arbitrary binary was built.
- **Initial capture:** the offline deliverable was produced locally against
  the previously published baseline
  [`cbab992`](https://github.com/RohitPatel97/can-vehicle-telemetry/commit/cbab992e21dae5d1ae285c5838a61f6ce6005c6b).
  Embedded capture metadata records that the working tree contained changes.
  The live GitHub Pages workflow generates fresh evidence from its deployment
  commit after GCC, Clang, and sanitizer jobs pass. Only generated HTML and
  evidence JSON are included in the Pages artifact.

## Public demo publication — 2026-11-13

- **Published implementation:**
  [`cb12be3`](https://github.com/RohitPatel97/can-vehicle-telemetry/commit/cb12be3ea7492dd29e3e4fe0a4fa5622c81668aa).
- **Shareable demo:** [CAN telemetry engineering walkthrough](https://rohitpatel97.github.io/can-vehicle-telemetry/).
  It is publicly accessible over HTTPS without a GitHub sign-in.
- **CI and deployment:** [run 43503936748](https://github.com/RohitPatel97/can-vehicle-telemetry/actions/runs/43503936748)
  passed GCC host tests, Clang host tests, GCC ASan/UBSan, and the Pages
  publishing job. Publishing rebuilds the host programs, captures fresh
  verified output, and uploads only the generated HTML and evidence JSON.
- **Live verification:** an unauthenticated browser loaded the public page and
  checked all six scenarios / 36 frames against the deployed JSON, including
  payloads, decisions, UART rows, counters, and logs. Keyboard playback,
  cross-scenario navigation, and the downloaded JSON passed. Desktop and mobile
  layouts had no horizontal overflow or script errors. The capture revision
  matched the published implementation, its working tree was clean, and every
  captured source hash matched the corresponding Git blob.
- **Maintenance:** updates on `main` deploy only after both compiler jobs and
  the sanitizer job succeed. Pull requests do not deploy. The checked-in offline
  copy remains the original local capture; the live page carries evidence for
  its deployment commit.
- **Limits:** the public page replays host execution and HAL mock results.
  Physical CAN/UART validation and complete Cube firmware compilation remain
  pending; publication does not expand those claims.

## Change record — 2026-10-01

- **Problem:** after processing its 16-frame budget, the adapter compared the
  last processed receive timestamp with the current clock even when newer
  frames remained queued. Continuous arrivals could therefore produce a false
  timeout; a real queued gap could also be reported in the wrong poll.
- **Solution:** snapshot queue emptiness and the current tick under the same
  PRIMASK-preserving lock, and apply the final current-clock check only for an
  empty queue. Preserve timeout checks at every processed arrival timestamp.
- **Regression cases:** with a 32 ms timeout, inject sequential frames every
  3 ms during UART calls, leaving timestamps 3 through 48 queued after the first
  poll. Expect no outage, then normal idle detection at elapsed 33 ms after
  draining. A control with the first pending arrival at 40 ms must report one
  real outage in the next poll. Repeat continuous traffic across tick rollover.
- **Observed result:** all three new HAL cases failed against the previous
  adapter (12 failed assertions). After the fix, all 15 HAL cases, 8 C protocol
  cases, six simulator scenarios (8/8 CTest entries total), and 13 Python cases
  passed. Strict compiler warnings, including `-Wconversion -Werror`, were on.
  Local commands were `cmake --build build-review --parallel`,
  `ctest --test-dir build-review --output-on-failure`, and the Python discovery
  command above. The local Zig setup explicitly invokes `zig ar qc` for CMake
  static-library creation; the standard compiler setup uses the commands above.
- **Documentation:** README now describes the fix, quick-start requirements,
  current test evidence, and deferred reporting under backlog. The test guide
  records the reproducible before/after scenarios; the API comment and changelog
  describe the same behavior.
- **Remaining limits:** a nonempty queue delays current-clock timeout reporting
  until queued history is processed. The ISR queue stays bounded and UART stays
  blocking. Host mocks do not model real scheduling; physical CAN/UART validation,
  measured throughput/latency, and a full Cube firmware build remain pending.
- **Published implementation commit:**
  [`1894bab7ee2a408ee77622769f1df92ab9abd56b`](https://github.com/RohitPatel97/can-vehicle-telemetry/commit/1894bab7ee2a408ee77622769f1df92ab9abd56b).
  [CI run for this commit](https://github.com/RohitPatel97/can-vehicle-telemetry/actions/runs/36881160831)
  passed all three jobs: GCC host tests, Clang host tests, and GCC
  AddressSanitizer/UndefinedBehaviorSanitizer. This CI evidence is independent
  of the Windows host run above and does not establish physical validation.

## Next three tasks

1. **Build against STM32CubeF4.** Generate an exact-board CubeIDE project, add
   the adapter and integration example, record the HAL/compiler version and
   `.ioc` clock/pin/filter configuration, and preserve a successful build log.
   Do not add `tests/mocks` to a firmware include path.
2. **Collect physical CAN/UART evidence.** Follow the
   [hardware plan](docs/hardware-validation.md) with a transceiver and a second
   CAN node. Capture paired analyzer/UART logs for nominal traffic, rejected
   frames, outages, queue saturation and recovery. Record measured latency and
   throughput instead of treating host simulation as hardware validation.
3. **Evaluate asynchronous UART.** Measure main-loop service time first, then
   compare the current 10 ms blocking call with a bounded DMA/non-blocking
   output queue. Define output-overrun and failed-transmission policies and add
   host tests for ownership and recovery before collecting board measurements.

## Maintenance boundaries

Use the dedicated project task to record each change, test command, result,
remaining limitation and published commit. Keep sample traffic synthetic.
Resume claims should say host-tested embedded C/C++, CAN/UART integration and
fault injection; they must not claim physical STM32 validation until board
evidence is captured. The diagnostic output can drop samples, and CRC/sequence
checks do not provide authentication or replay protection.
