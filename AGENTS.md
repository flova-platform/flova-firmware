# Flova firmware repository guide

This repository contains the portable Flova device SDK, Arduino services,
ESP32/ESP8266 board packages, firmware examples, and the bounded Flova Link
protocol implementation.

Read [.docs/codebase-map.md](.docs/codebase-map.md) and the relevant document in
[.docs/README.md](.docs/README.md) before changing an architectural or protocol
area.

## Repository boundaries

- `packages/flova-device-sdk`: canonical C++11 `flova::Device` runtime. Its
  portable include closure must not depend on Arduino, ESP, GPIO, Wi-Fi,
  WebSocket, TLS, filesystem, exceptions, RTTI, or unbounded containers.
- `packages/flova-arduino`: Arduino clock, storage, logging, TLS, OTA, and
  Device Link services.
- `packages/flova-esp32` and `packages/flova-esp8266`: board-specific
  provisioning, boot/storage policy, TLS/resource setup, and hardware
  composition.
- `examples/`: selected PlatformIO applications and SDK compile contracts.
  The root `src/` is only a required PlatformIO source anchor; it contains no
  firmware implementation.
- `protocol/`: CDDL authority, generated zcbor codecs, message catalog, and
  deterministic conformance vectors. Do not hand-edit generated files.
- `test/`: host and embedded tests. `scripts/`: generators and static guards.

`FlovaCore.h` is the only device runtime. `FlovaLinkMessages.h` contains the
bounded Arduino codec records but no transport base class. Add SDK behavior to
the `flova::` service interfaces and compose board services explicitly.

## Engineering rules

- Route local writes, remote writes, scheduled actions, and mapped outputs
  through one bounded datastream write path.
- Never update authoritative cached state, persistence, or revisions after a
  rejected hardware write.
- Transport callbacks may copy into fixed-capacity queues only. Apply hardware
  work from the board-owned `flova::Device::run()` loop.
- Keep frames, strings, queues, registries, command history, configuration
  records, and CBOR nesting explicitly bounded.
- Persist and verify each configuration record before acknowledging it; retain
  the last active A/B generation until promotion succeeds.
- Keep Device Link schema-directed and deterministic: CDDL is authoritative,
  complete frames are at most 512 bytes, and ESP8266 Link uses the verified
  2,048-byte RX / 512-byte TX profile. OTA remains a separate 16,384-byte RX /
  512-byte TX workload.
- Do not log credentials, provisioning tokens, device secrets, or complete
  sensitive payloads.
- Comment ownership, lifecycle, memory, hardware, and protocol invariants—not
  obvious syntax.
- Preserve unrelated staged or uncommitted work. Inspect `git status` before
  editing and never reset or discard the worktree to isolate a task.

## Validation

Run the relevant checks before handoff. For a broad firmware change, run:

```sh
cmake -S . -B /tmp/flova-core-build
cmake --build /tmp/flova-core-build
ctest --test-dir /tmp/flova-core-build --output-on-failure
scripts/check_flova_link_contract.sh
scripts/check_esp8266_stack_usage.py
pio run -e universal-esp32 -e universal-esp8266
pio run -e datastream-api-esp32 -e datastream-api-esp8266
pio test -e test-esp32 -e test-esp8266 --without-uploading --without-testing
pio test -e test-bootstrap-esp8266 --without-uploading --without-testing
git diff --check
```

Document any unavailable target with the exact command and failure. Build
success is not physical acceptance: provisioning, WSS/TLS, reconnect, OTA,
power-loss configuration recovery, and ESP8266 heap/fragmentation checks
require hardware or the documented external test setup.
