# Changelog

All notable changes are documented here. This project follows Semantic
Versioning and the structure of Keep a Changelog.

## Unreleased

### Fixed

- Detect receive outages at queued frame timestamps so a valid recovery frame
  cannot erase an outage before the next main-loop timeout check.
- Explicit unsigned CRC arithmetic to satisfy Clang conversion warnings.

### Added

- Twelve host integration cases compiling the STM32 adapter against a test-only
  HAL/CMSIS double; covers interrupt deferral, bounded queue handling, envelope
  rejection, fault accounting, UART failure, and outage recovery.
- Six asserted CTest simulator scenarios, including exact diagnostic summaries.
- README project breakdown, problem/solution test guide, and explicit queue,
  concurrency, diagnostic delivery, and hardware validation boundaries.

## 1.0.0 - 2026-08-26

### Added

- Hardware-neutral C11 encoder, decoder, and CRC-8/SAE-J1850 implementation.
- Stateful receiver with sequence, timeout, controller-error, and diagnostic
  counter handling.
- C++17 host simulator with CRC, semantic range, sequence, timeout, and CAN
  fault injection.
- C unit suite plus a dependency-free Python executable reference model and
  tests.
- STM32F446RE Cube HAL adapter and 500 kbit/s bxCAN integration example.
- GCC/Clang GitHub Actions builds, tests, simulator smoke runs, and sanitizer
  coverage.
- Hardware validation plan that distinguishes host evidence from pending board
  work.
