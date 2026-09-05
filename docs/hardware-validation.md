# STM32F446RE hardware validation plan

The repository's protocol logic is host-tested. Physical CAN/UART behavior has
not been run on a board in this repository, so the steps below are a validation
plan rather than a test report.

## Equipment

- NUCLEO-F446RE (or another STM32F446RE board)
- 3.3 V CAN transceiver such as TJA1051T/3 or SN65HVD230
- second 500 kbit/s CAN node or USB-CAN adapter
- two 120-ohm termination resistors, one at each end of the bus
- USB serial terminal configured for the CubeMX-selected UART settings
- optional oscilloscope or logic analyzer with CAN decoding

The MCU CAN pins are logic-level only. Do not connect them directly to CAN_H
and CAN_L; a transceiver is required. Ensure all nodes share ground.

## Firmware setup

1. Generate an STM32CubeIDE project for the exact board.
2. Configure the system clock so CAN1 receives a 45 MHz APB1 clock, or
   recalculate the bit timing in the integration example.
3. Configure USART2 (commonly 115200 baud, 8 data bits, no parity, 1 stop bit).
4. Add `src/protocol.c`, `src/receiver.c`, the STM32 adapter, and the public
   include directory to the Cube project.
5. Copy the initialization/callback excerpts into CubeMX `USER CODE` sections.
6. Build with warnings enabled and flash the target.

## Validation checklist

Record board revision, transceiver, CubeF4 version, compiler version, clock
tree, CAN sample point, bus length, and test date alongside the results.

| Test | Stimulus | Expected observation |
|---|---|---|
| Nominal receive | Send ID `0x321`, DLC 8, valid CRC and sequence | One `TEL,...` line on UART; accepted counter increments |
| Identifier filter | Send the valid payload on ID `0x322` | No telemetry UART line |
| Frame format filter | Send an extended-ID or remote frame matching the base identifier | No telemetry UART line; inspect hardware filtering and software envelope diagnostics |
| CRC fault | Flip one payload bit without updating byte 7 | No UART line; CRC error counter increments |
| Semantic range | Set speed to `0xFFFF`, then recompute CRC | No UART line; range counter increments |
| Sequence jump | Send sequence 10, then 12, then 13 | 10 and 13 forwarded; 12 rejected; sequence counter increments |
| Timeout | Stop valid traffic for more than 250 ms | Timeout policy activates once per outage |
| Queued outage/recovery | Delay main polling while arrivals at 0 ms and 300 ms queue | New outage reported even if recovery is accepted in that poll; final ongoing state is clear |
| Queue overload | Inject more than 16 arrivals before main drains the queue | Newest excess frames dropped, software overrun counter increments; receiver resynchronizes after loss |
| UART service time | Sustain valid traffic while measuring callback and main-loop duration | No formatting or UART transmission in the CAN ISR; measure main-loop blocking and queue occupancy |
| CAN error | Disconnect or short the bus under controlled conditions | HAL error callback runs; CAN error counter increments |
| Recovery | Restore a correctly terminated bus and sequential traffic | Controller and application recover per the chosen fault policy |
| Rollover soak | Exercise across `HAL_GetTick()` rollover or inject timestamps | No false negative due to timer wrap |

## Evidence to capture

- CubeMX `.ioc` clock and peripheral configuration
- exact firmware commit SHA and build log
- CAN analyzer trace showing identifier, DLC, bytes, and timestamps
- UART log paired with the CAN trace
- scope capture confirming nominal bit time if physical-layer timing is in scope
- completed checklist with pass/fail and observed deviations

Only after completing and preserving that evidence should the README's hardware
status be changed from “not yet hardware-validated.”
