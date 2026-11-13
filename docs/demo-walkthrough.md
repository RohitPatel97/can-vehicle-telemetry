# Five-minute engineering demo

This walkthrough is tailored to the Collins Aerospace interview preparation
themes: diagnosing failures, making reliability decisions, and verifying
recovery. It uses the CAN portfolio project as evidence. The available context
is preparation material, not a transcript of the interview or a statement of
Collins product requirements.

## Open and present

Open the [live demo](https://rohitpatel97.github.io/can-vehicle-telemetry/) or
[`demo/index.html`](../demo/index.html) in a browser. The entire demo is one
offline HTML file, including its captured evidence. No server, account, internet
connection, or CAN board is needed to replay it. Start with normal traffic, then
select fault scenarios and step through their frames. The presenter notes and
raw logs are available for follow-up questions.

The display replays recorded execution of the compiled host programs. Its
controls select and replay those captures; they do not operate a live CAN bus.
The backlog explanation uses synthetic test inputs and is labelled separately
from the captured HAL test results.

## A five-minute sequence

| Time | Show | Explain |
|---|---|---|
| 0:00–0:30 | Project overview | "The problem is deciding which telemetry is trustworthy enough to forward. I separated the portable protocol, receiver state, and hardware adapter so the logic can be tested independently." |
| 0:30–1:10 | Normal traffic; step through a frame | "Eight bytes encode a sequence, speed, coolant temperature, voltage, flags, and CRC. The C receiver validates the frame, and only accepted samples produce UART output." |
| 1:10–2:10 | CRC fault, then range fault | "A flipped bit fails integrity checking. An impossible value with a newly computed CRC passes integrity checking but fails semantic validation. Those are different failure classes." |
| 2:10–2:50 | Sequence and timeout scenes | "A sequence discontinuity is rejected once and becomes the new baseline; the next sequential sample recovers. Timeout is counted once on entry, and valid recovery clears the active state." |
| 2:50–4:10 | Backlog explanation and HAL results | "The tricky bug was confusing delayed processing with missing traffic. The bounded poll could leave newer frames queued, then compare an older processed timestamp against the current clock. The fix preserves receive order and only advances to the current clock after an atomic empty-queue/time snapshot." |
| 4:10–5:00 | Evidence and limitations | "The capture runner runs the actual host executables; this demo replays the verified output. The HAL tests use doubles. The next validation steps are a complete Cube firmware build and paired physical CAN/UART captures." |

The CAN-controller-error scene is an optional follow-up: the driver reports a
failure without delivering a payload, so no frame is fabricated or forwarded.

## Details to be ready to explain

- **Why two rejections after CRC or range corruption?** The invalid frame does
  not advance receiver sequence state. The next valid frame is discontinuous,
  so it is rejected once while establishing a new baseline. With six inputs,
  four are accepted. This deliberate policy is visible in the demo.
- **What does the timeout number mean?** The simulator inserts 251 ms into an
  existing 20 ms interval. The actual simulated gap is 271 ms, above the 250 ms
  limit. This is synthetic elapsed time, not a measured board delay.
- **Why not check the newest queued timestamp immediately?** A queued frame has
  not yet passed envelope, CRC, range, or sequence checks. Treating arrival as
  acceptance would allow invalid traffic to hide an outage.
- **What does the queue fix cost?** Current-clock timeout reporting waits while
  queued history is processed. Main-loop service still needs measurement. The
  queue has 16 slots and drops the newest frame on overflow.
- **Why keep UART out of the ISR?** Validation, formatting, and blocking I/O are
  deferred to main context. The example still uses a 10 ms blocking UART call;
  a full drain can spend up to 160 ms in UART waits alone.
- **Does CRC provide security?** It detects accidental corruption. It does not
  authenticate a sender, and an 8-bit sequence is not replay protection.
- **Is this aerospace hardware validation?** No. This demonstrates transferable
  software practices using a vehicle telemetry example. It does not demonstrate
  an airborne system, real CAN electrical behavior, or certification.

## A short debugging story

**Problem:** continuous traffic could produce a false timeout under backlog.

**Investigation:** a deterministic HAL double refilled the queue during UART
calls. With a 32 ms threshold, a poll ended at tick 48 with the last processed
timestamp at 0 and valid arrivals at ticks 3 through 48 still pending.

**Decision:** retain a bounded poll and evaluate receive history in order. Check
queue emptiness and time under the same PRIMASK-preserving critical section.

**Verification:** the three October regression cases failed before the fix and
passed afterward. They cover continuous backlog, a real gap across polls, and
tick rollover. The demo captures the current suite result; the historical
before/after record is in [WORKFLOW.md](../WORKFLOW.md).

## Regenerate from current code

With CMake 3.20+, a C11/C++17 compiler, and Python 3.10+ installed, run from the
repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
python scripts/create_demo.py --build-dir build
```

The capture runner executes all six simulator scenarios and both C suites. It
checks exit codes, expected counters, status order, accepted-only UART output,
and named test results before producing the HTML and `demo/evidence.json`.
Missing executables or unexpected results fail generation with an error.

Source and executable hashes identify the files present during capture. They
are provenance aids, not proof that an arbitrary supplied executable was built
from those source files. Rebuild before capturing to keep the evidence current.

Share the generated `demo/index.html` for a self-contained replay. Share
`demo/evidence.json` alongside it when someone wants to inspect the raw records.
