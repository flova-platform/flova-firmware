# Testing and release checks

The repository currently uses PlatformIO compile contracts because both public wrappers depend on Arduino framework types and board SDKs. Run:

```sh
cmake -S . -B /tmp/flova-core-build
cmake --build /tmp/flova-core-build
ctest --test-dir /tmp/flova-core-build --output-on-failure
scripts/check_flova_link_contract.sh
pio run -e universal-esp32 -e universal-esp8266
scripts/check_esp8266_stack_usage.py
pio run -e datastream-api-esp32 -e datastream-api-esp8266
pio test -e test-esp32 -e test-esp8266 --without-uploading --without-testing
pio test -e test-bootstrap-esp8266 --without-uploading --without-testing
git diff --check
```

The native C++11 suite executes cache-only reads, refresh/report behavior,
rejection preserving state, cloud/local handler convergence, acknowledgements,
stale revision suppression, and KeepLatest reconnect coalescing. It also covers
fixed frame boundaries and the streamed configuration installer's ordering,
checksum, read-back, A/B promotion, interruption, and lost-final-ACK behavior.
The board examples compile the typed APIs on ESP32 and ESP8266;
`--without-uploading` verifies Unity test binaries, while connected boards
execute them.

`scripts/check_flova_link_contract.sh` is the stable combined entry point. It
requires the depth-four, bounded, non-recursive CDDL profile and scans the
explicit Link/configuration paths for ArduinoJson, runtime JSON, heap
allocation, dynamic containers, generic CBOR trees, TLV, and legacy value
decoders. Its focused subchecks remain available in `scripts/` when diagnosing
a failure. Both checks must pass before claiming Link v1 conformance.

The universal ESP8266 build emits compiler stack-usage records.
`scripts/check_esp8266_stack_usage.py` rejects regressions in boot restore,
provisioning, state batching, and OTA frames before they can consume the 4 KiB
continuation stack on hardware.

Before a release, test one physical ESP32 and ESP8266 for provisioning, WSS reconnect, an accepted remote relay write, a rejected unsafe write, offline local state followed by reconnect, duplicate command delivery, factory reset, and persistent cache restoration. Record flash and RAM output from PlatformIO for both universal targets.

Provisioning validation must include a multi-record configuration transaction,
an interrupted transaction followed by retry, a duplicate record/final ACK,
a reboot from the active generation, and recovery after corrupting the inactive
generation. Confirm that serial diagnostics report only bounded status and heap
state, never device credentials or complete payloads.

For schedules, also verify capability heartbeat values, retained manifest
installation, `schedules/reported`, execution while disconnected, reboot
restoration without replaying missed occurrences, renewal publication, and
safe expiry on both physical targets.
On ESP8266, install a near-limit schedule/configuration record and reboot
without erasing flash. Confirm WSS connects without a watchdog reset and record
free heap, largest free block, and fragmentation before and after TLS setup.
Keep WSS connected through network interruptions, deliver a 512-byte frame,
reject a 513-byte frame, then begin OTA. Confirm Link releases its TLS workload
before HTTPS begins, no Link/configuration hot-path operation allocates after
transport setup, and failed reconnects do not leak memory. Release builds must
use the 2,048/512 Link and 16,384/512 OTA profiles with the 16 KiB cache,
48 KiB IRAM shared-second-heap option. Diagnostics must show DRAM and IRAM; a
build without the required second heap must fail closed before connecting.

The CMake host suite executes the portable runtime, configuration installer,
protocol codec/fuzz contracts, WebSocket framing, and a custom-board compile
contract. Keep Arduino `String` and platform APIs outside that include closure.
