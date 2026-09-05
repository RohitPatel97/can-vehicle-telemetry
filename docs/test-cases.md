# Reproducible problem/solution test cases

The compiled tests exercise the real protocol and adapter source. The STM32
suite supplies mock HAL/CMSIS functions: it is host integration testing, not a
CubeF4 firmware build, physical CAN test, or timing benchmark.

## Run the evidence

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
```

For Visual Studio, add `--config Debug` to the build and `-C Debug` to CTest.
The C protocol executable reports 8 named cases; the HAL integration executable
reports 12. CTest also runs six C++ simulator scenarios and checks their exact
counter summaries. The Python suite contains 13 protocol/reference cases.

## Scenarios

| Problem and input | Solution | Expected evidence |
|---|---|---|
| A payload bit flips during transfer | CRC-8 is checked before semantic decoding | `crc mismatch`; no UART output for the corrupted frame |
| Sender encodes speed `0xFFFF` with a newly computed CRC | Engineering range validation follows integrity validation | `value out of range`; frame rejected even with valid CRC |
| A sample is lost: sequences `10, 12, 13` | Reject the discontinuous frame once, then track its successor | 10 and 13 accepted; exactly one sequence error |
| Sequence reaches the 8-bit limit | Explicit modulo-256 arithmetic | `254, 255, 0` accepted without sequence errors |
| CAN interrupt arrives while main-loop UART is busy | ISR copies into a bounded circular buffer; UART runs in main context | No UART or receiver mutation in callback; preserved PRIMASK; exact `TEL,...\r\n` output after polling |
| Receive queue fills with 18 arrivals before polling | Preserve the first 16; drop the newest two and count them | 16 accepted, two queue overruns; next discontinuity rejected and following frame recovers |
| More interrupts arrive during every UART call | Limit one poll to the queue capacity | Poll transmits 16 samples and returns with 16 queued; next poll drains them in sequence |
| An extended ID, remote frame, different ID, or invalid DLC reaches software | Revalidate the CAN envelope before decoding | Three envelope errors and nine DLC errors in the rejection test; zero forwarded samples |
| `HAL_CAN_GetRxMessage` or HAL error callback fails | Defer and merge error counters in main context | Two CAN errors accounted once; no payload is fabricated |
| UART transmit returns `HAL_TIMEOUT` | Count the diagnostic transport failure and consume the accepted sample | One UART error; following sequence accepted; no automatic replay |
| Arrival times are `0 ms, 300 ms`, timeout is 250 ms, and both frames wait in the queue | Check elapsed time at each arrival before accepting recovery | Poll returns true; one outage event; final `timed_out` is false because traffic recovered |
| Tick counter rolls over after `0xFFFFFFF0` | Compare unsigned elapsed milliseconds to receive time | No timeout at elapsed 32 ms; exactly one transition at 33 ms |
| Deferred error counters reach `UINT32_MAX` | Saturating additions preserve a visible fault indication | Pending and accumulated adapter fault counters remain at the maximum |

## Source map

- [C protocol cases](../tests/test_telemetry.c): wire order, golden vector,
  boundaries, invalid inputs, CRC/range/flag faults, sequence and timer wrap.
- [STM32 integration cases](../tests/test_stm32_hal.c): 12 named functions,
  including `test_timeout_gap_survives_recovery_in_same_batch`, the regression
  for an outage previously hidden by an already queued recovery frame.
- [Python reference cases](../tests/test_reference.py): independent byte-level
  examples and receiver behavior with no third-party Python dependencies.
- [CTest registrations](../CMakeLists.txt): compiled suites plus asserted
  normal/CRC/range/sequence/timeout/CAN simulator summaries.
- [CI workflow](../.github/workflows/ci.yml): GCC, Clang and sanitizer jobs.

## Interpretation

Passing tests support claims about software behavior under these injected
conditions. They do not establish interrupt latency, bus-off recovery,
transceiver operation, CAN bitrate, or UART reliability on a physical board.
Complete the [hardware validation plan](hardware-validation.md) to collect
those measurements. CRC and the 8-bit sequence field are not authentication or
replay protection.
