# Repository guide

This repository contains the Flova embedded SDK and universal ESP32/ESP8266 firmware. Keep changes compatible with constrained Arduino targets and the binary Flova Link contract.

## Layout

- `packages/flova-device-sdk`: Arduino-free C++11 datastream and schedule core.
- `packages/flova-arduino`: Arduino clock, logging, device-link, and storage interfaces.
- `packages/flova-esp32` and `packages/flova-esp8266`: provisioning, board storage, Wi-Fi, and universal hardware mappings.
- `examples/universal-*`: production universal firmware entry points.
- `examples/datastream-api-*`: public SDK examples and compile-contract checks.
- `.docs`: maintained architecture, API, protocol, and validation documentation.

## Engineering rules

- Route local writes, cloud writes, and mapped outputs through the shared datastream write pipeline.
- Keep `FlovaCore.h` free of Arduino, ESP, GPIO, exceptions, RTTI, and unbounded containers.
- Never update authoritative cached state after a rejected hardware write.
- Keep queues, registries, strings, and command history bounded.
- Do not invoke hardware from a transport callback outside `FlovaDevice::loop()` semantics.
- Keep firmware and Engine aligned on the versioned Flova Link frame and deterministic CDDL CBOR contract.
- Do not log credentials, provisioning tokens, device secrets, or complete sensitive payloads.
- Prefer fixed storage and existing Arduino facilities; justify any dependency added to firmware.
- Comment invariants and hardware/protocol constraints, not obvious syntax.

## Validation

Run all supported compile contracts before handing off firmware changes:

```sh
cmake -S . -B /tmp/flova-core-build
cmake --build /tmp/flova-core-build
ctest --test-dir /tmp/flova-core-build --output-on-failure
pio run -e universal-esp32 -e universal-esp8266
pio run -e datastream-api-esp32 -e datastream-api-esp8266
pio test -e test-esp32 -e test-esp8266 --without-uploading --without-testing
git diff --check
```

Document any target that cannot be built and include the exact failure. See `.docs/testing.md` for hardware-dependent checks.
