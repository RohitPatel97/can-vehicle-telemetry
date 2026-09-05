# Contributing

Thanks for improving the telemetry protocol. Small, reviewable changes with a
test are preferred.

## Development setup

Requirements are CMake 3.20+, a C11/C++17 compiler, and Python 3.10+ (standard
library only). Configure and run both implementations:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
```

Keep warnings-as-errors enabled. On GCC or Clang, also run a sanitizer build
when changing parsers or state transitions:

```sh
cmake -S . -B build-sanitized -DTELEMETRY_ENABLE_SANITIZERS=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

## Pull requests

1. Explain the behavior change and its safety impact.
2. Add or update C and Python tests for protocol behavior.
3. Keep the C and Python reference implementations byte-for-byte compatible.
4. Update the README protocol table for any wire-format change.
5. Do not report hardware validation without attaching reproducible evidence
   (board, transceiver, clock tree, toolchain, trace, and observed result).

The eight-byte format is a public interface. Changes to offsets, scale factors,
CRC parameters, or validation rules require a versioning/migration plan rather
than a silent edit.

## Style

- C is C11 and keeps allocation, floating point, and hidden global state out of
  the protocol library.
- C++ is C++17 and used only for host tooling.
- Python tooling stays dependency-free unless a dependency provides clear value.
- Prefer integer engineering units (`centi-km/h`, `mV`) at API boundaries.
- Public interfaces need comments that state ownership, error, and timing rules.

By contributing, you agree that your contribution is licensed under the MIT
License in this repository.

