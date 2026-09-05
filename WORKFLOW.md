# CAN telemetry project workflow

Repository: [RohitPatel97/can-vehicle-telemetry](https://github.com/RohitPatel97/can-vehicle-telemetry)

## Completed implementation

- Portable C11 wire format with CRC-8, semantic validation and receiver state.
- STM32 adapter with a 16-frame IRQ-to-main circular buffer, bounded drain,
  receive timestamps, envelope checks, accepted-only UART, and diagnostics.
- Outage regression fix: queued frames received at 0 ms and 300 ms trigger a
  250 ms timeout event even if recovery is accepted in the same main-loop poll.
- Explicit unsigned CRC arithmetic for strict Clang conversion warnings.
- Eight protocol cases, twelve HAL integration cases, thirteen Python reference
  cases, and six CTest simulator scenarios with expected counter summaries.
- [README](README.md) project breakdown and [test case guide](docs/test-cases.md)
  describe the problem, solution, inputs and expected evidence.

## Verification commands

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
