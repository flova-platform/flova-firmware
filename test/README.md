# Firmware tests

The test directory has one ownership boundary per test toolchain:

- `host/` contains host-side C++11 tests built by CMake. They
  exercise the portable SDK, configuration installer, generated Link codecs,
  and deterministic protocol fuzzing without Arduino or ESP dependencies.
- `test_datastream/` is the PlatformIO Unity suite. PlatformIO discovers
  embedded suites only when their directory name begins with `test_`, so that
  prefix is required and is not redundant.

Run host tests with CMake and embedded compile contracts with PlatformIO:

```sh
cmake -S . -B /tmp/flova-core-build
cmake --build /tmp/flova-core-build
ctest --test-dir /tmp/flova-core-build --output-on-failure
pio test -e test-esp32 -e test-esp8266 --without-uploading --without-testing
```

Keep board-specific hardware tests in a `test_*` PlatformIO suite and keep
portable behavior in `host/`. Do not add another wrapper directory between
`test/` and either test kind.
