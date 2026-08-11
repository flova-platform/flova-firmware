# Flova Firmware SDK

Flova Firmware is a bounded, local-first device runtime for constrained boards.
The portable SDK keeps datastream state, safety rules, persistence hooks,
offline delivery, scheduling, provisioning, and board-independent protocol
contracts separate from Arduino and ESP implementation details.

This repository is currently an MVP/development codebase. The standalone
`flova::Device` runtime is the canonical API for new board ports. Existing
ESP32 and ESP8266 applications still use the older `FlovaDevice` compatibility
surface while that runtime is being migrated; new functionality must target
the standalone core.

## Architecture

```text
flova-device-sdk   portable C++11 runtime and domain contracts
        ↓
flova-arduino      Arduino services and Device Link adapter
        ↓
flova-esp32/8266   board lifecycle, storage, networking, and hardware wiring
        ↓
examples/          selected firmware applications
```

The [codebase map](.docs/codebase-map.md) explains package ownership and the
dependency direction. The root `src/` directory is only a PlatformIO source
anchor; each environment selects its application from `examples/*/src`.

## Repository layout

- `packages/flova-device-sdk`: portable `flova::Device` SDK.
- `packages/flova-arduino`: Arduino clock, storage, logging, TLS, OTA, and
  Device Link services.
- `packages/flova-esp32`, `packages/flova-esp8266`: board-specific wrappers.
- `examples/universal-*`: universal ESP firmware applications.
- `examples/datastream-api-*`: typed API compile contracts.
- `examples/custom-board-basic`: normal-C++11 custom-board integration.
- `protocol`: CDDL schema, generated codecs, and conformance vectors; see the
  [protocol asset guide](protocol/README.md).
- `scripts`: focused validation, generation, and PlatformIO integration tools;
  see the [script guide](scripts/README.md).
- `.docs`: maintained architecture, API, protocol, board-porting, and testing
  documentation.

## Build the universal firmware

From the repository root:

```sh
pio run -e universal-esp32
pio run -e universal-esp8266
```

The ESP8266 Link profile uses verified BearSSL with 2,048-byte RX and
512-byte TX buffers. Every WebSocket binary message contains exactly one
complete Flova Link frame of at most 512 bytes, including its fixed 12-byte
header. OTA disconnects Link first and uses a separate verified HTTPS profile
with 16,384-byte RX and 512-byte TX buffers.

## Portable board integration

A new board should include `FlovaCore.h`, implement four bounded services, and
compose the runtime in its own application:

```cpp
#include <FlovaCore.h>

flova::Device device(link, storage, clock, logger);
auto relay = device.datastream<bool>("relay")
    .mode(flova::Mode::State)
    .offline(flova::OfflinePolicy::KeepLatest)
    .persist(flova::PersistencePolicy::Persistent);

static flova::WriteResult writeRelay(bool enabled) {
  // `writeBoardRelay` belongs to the board HAL, not the portable SDK.
  return writeBoardRelay(enabled) ? flova::WriteResult::accept()
                                  : flova::WriteResult::reject("hardware_write_failed");
}

int main() {
  relay.onWrite(writeRelay);
  if (!device.begin()) return 1;
  for (;;) device.run();
}
```

The board supplies:

- `flova::Link` for bounded structured messages and datastream binding;
- `flova::Storage` for fixed-size records and capability reporting;
- `flova::Clock` for monotonic time and optional UTC synchronization;
- `flova::Logger` for bounded diagnostics.

Provisioning, OTA, boot control, schedules, and hardware mappings are optional
board services. Transport callbacks must queue bounded work; hardware writes
run from the device loop. See [Custom boards](.docs/custom-boards.md) and the
[custom-board example](examples/custom-board-basic/README.md).

## Local-first datastream semantics

`read()` reads only the local cache. `refresh()` invokes the registered reader.
`report()` records an observation without invoking an actuator. `write()` uses
the same handler as remote commands and updates the cache only after the
hardware operation is accepted. Offline policies determine whether the newest
state is retained, history is persisted, data is dropped, or delivery is
rejected.

## Device Link and provisioning

Flova Link v1 is defined by
[`protocol/flova-link-v1.cddl`](protocol/flova-link-v1.cddl). It uses
deterministic bounded CBOR, generated zcbor codecs, fixed-size frames, numeric
runtime datastream IDs, and streamed transactional configuration:

```text
CONFIG_BEGIN → CONFIG_RECORD* → CONFIG_END
```

The firmware persists and verifies records in the inactive A/B generation
before acknowledging them, then promotes the generation atomically. See
[Device Link](.docs/cloud-protocol.md) and
[Provisioning](.docs/provisioning.md).

## Validation

Run the complete software validation set with:

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

See [Testing and release checks](.docs/testing.md) for hardware acceptance.
Software builds do not replace physical provisioning, WSS/TLS, reconnect, OTA,
power-loss recovery, or ESP8266 memory-stability testing.
