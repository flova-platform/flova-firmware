# Flova Firmware SDK

Flova Firmware is a bounded, local-first device runtime for constrained boards.
The portable SDK keeps datastream state, safety rules, persistence hooks,
offline delivery, scheduling, provisioning, and board-independent protocol
contracts separate from Arduino and ESP implementation details.

This repository is currently an MVP/development codebase. Applications use an
explicit `FlovaEsp32` or `FlovaEsp8266`; board ports compose the same
`flova::Device` runtime without a second SDK layer or platform-selection macros.

## Quickstart

```cpp
#include <ESP8266WiFi.h>
#include <FlovaEsp8266.h>

FlovaEsp8266 flova;
auto relay = flova.datastream<bool>("relay");

void setup() {
  WiFi.begin("your-wifi", "your-password");
  relay.onWrite([](bool enabled) {
    digitalWrite(LED_BUILTIN, enabled ? LOW : HIGH);
  });
  flova.begin();
}

void loop() { flova.run(); }
```

`begin()` restores Flova's private identity when present and otherwise enters
`AwaitingProvisioning`; it never changes Wi-Fi, starts or stops a server,
touches GPIO, or reboots the board. Deliver a phone or factory handoff with
`flova.provision(flova::ProvisioningHandoff(linkUrl, token))`, or attach Flova's
two bounded routes to an existing Arduino web server. Universal firmware is a
separate full-device composition that owns SoftAP, Wi-Fi, GPIO, OTA, and
automatic restart.

## Architecture

```text
flova-device-sdk   portable C++11 runtime and domain contracts
        ↓
flova-arduino      Arduino services and Device Link adapter
        ↓
flova-esp32/8266   passive SDK facades plus explicit universal compositions
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
- `examples/custom-arduino-client`: Blynk-like explicit board-class integration
  for ESP32/ESP8266 Arduino applications.
- `examples/custom-arduino-provisioning`: explicit board-class phone provisioning with
  no hardcoded Wi-Fi credentials.
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
auto relay = device.datastream<bool>("relay");

static flova::WriteResult writeRelay(bool enabled) {
  // `writeBoardRelay` belongs to the board HAL, not the portable SDK.
  return writeBoardRelay(enabled) ? flova::accept()
                                  : flova::reject("hardware_write_failed");
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

For an existing ESP32/ESP8266 Arduino application, use the board API
example:

```sh
pio run -e custom-arduino-client-esp32
pio run -e custom-arduino-client-esp8266
```

It includes the matching `<FlovaEsp32.h>` or `<FlovaEsp8266.h>`, keeps hardware
ownership in the sketch, and supports both simple and context-aware typed
callbacks. See the
[custom Arduino client](examples/custom-arduino-client/README.md).

For phone provisioning, use the matching board package and the
[custom Arduino provisioning example](examples/custom-arduino-provisioning/README.md).

## Local-first datastream semantics

`value()` reads only the local cache. `report()` records an observation or a
hardware change the application performed itself. `write()` uses the same
handler as remote commands and updates state only after acceptance. Both work
offline; installed delivery policy controls later synchronization, not whether
local application code can run.

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
