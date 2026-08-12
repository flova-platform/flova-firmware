# Firmware codebase map

The repository is a collection of reusable libraries plus selected firmware
applications. The root `src/` directory is only a documented PlatformIO
source anchor; it contains no firmware implementation. Environments select an
application under `examples/*/src` with `build_src_filter`.

```text
packages/flova-device-sdk   portable C++11 domain runtime
packages/flova-arduino      Arduino services and Device Link adapter
packages/flova-esp32        ESP32 provisioning, storage, and board composition
packages/flova-esp8266      ESP8266 provisioning, storage, and board composition
examples/                   firmware applications and compile contracts
protocol/                   CDDL authority, generated codecs, and vectors
third_party/                vendored protocol dependencies
test/host/                  CMake host tests for the portable runtime/protocol
test/test_datastream/       PlatformIO Unity tests for the portable runtime
scripts/                    generators and bounded-contract checks
.docs/                      maintained architecture and contributor contracts
```

The package ownership table is also available in
[`packages/README.md`](../packages/README.md).

## Dependency direction

```text
portable SDK core
        ^
        | domain services and bounded records
Arduino adapters
        ^
        | Wi-Fi, TLS, WebSocket, storage, clock, OTA
ESP32 / ESP8266 board packages
        ^
        | pins, provisioning lifecycle, board boot policy
selected example application
```

`FlovaCore.h` and the `flova::` namespace are the canonical portable SDK
surface. Arduino applications select `FlovaEsp32` or `FlovaEsp8266` explicitly;
board-specific composition owns platform APIs and the portable core remains
unchanged.

Portable core code must not include Arduino, ESP, GPIO, Wi-Fi, WebSocket, TLS,
or filesystem headers. A board supplies `Link`, `Storage`, `Clock`, and
`Logger`, then optionally composes provisioning, configuration installation,
OTA, scheduling, and hardware-mapping services.

Generated zcbor and third-party files are protocol/build inputs, not hand-edited
SDK source. Keep protocol schema changes in `protocol/flova-link-v1.cddl` and
regenerate the checked-in artifacts and vectors together.
