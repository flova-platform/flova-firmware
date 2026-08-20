# Custom boards and industrial targets

`FlovaDevice.h` is the canonical header-only C++11 runtime with no Arduino, ESP, Wi-Fi, WebSocket, exception, or RTTI dependency. It can run on an STM32 HAL/FreeRTOS application, another MCU SDK, an industrial Linux controller, or a PLC runtime that supports C++ integration.

New board code includes `FlovaDevice.h` and composes the portable
`flova::Device` with its own services. There is no compatibility runtime or
transport base class to inherit.

Implement four bounded services:

- `flova::Link`: moves structured `flova::Message` envelopes, supplies a fresh
  non-zero hardware-random boot nonce, and calls the registered receiver with
  its context.
- `flova::Storage`: reads and writes fixed-size records using NVS, EEPROM, flash, a file, or PLC retained memory.
- `flova::Clock`: supplies monotonic milliseconds.
- `flova::Logger`: maps SDK diagnostics to the target's bounded logger.

The board application owns the concrete device object and calls
`device.run()` from its main loop. A Link implementation must copy or queue
bounded inbound records from its transport callback and let `run()` apply
hardware writes; it must not call GPIO or other hardware directly from a
socket callback.

Custom board profiles define the `FLOVA_*_CAPACITY` build values from real RAM,
persistent storage, and transport constraints. The SDK advertises those values;
Engine applies deployment policy. Do not copy ESP capacity values blindly.

Custom ESP8266 PlatformIO profiles that use Flova's verified HTTPS or WSS
adapters must also select
`PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED`. The supplied
universal profile includes it already. The official Link endpoint uses a
certified 2,048-byte receive profile; shared IRAM preserves headroom for its separate
verified 16 KiB OTA profile and larger endpoint-certified deployments.

`Storage::capabilities()` reports usable bytes, maximum record size, erase
geometry, write granularity, persistence, and wear sensitivity. Configure a
`ResourceBudget` array before `Device::begin()`; history receives no implicit
budget. This makes unsupported storage fail closed instead of filling flash.

The portable core does not own Wi-Fi or provisioning. The normal Arduino ESP
facade observes application-managed connectivity and stores only Flova-private
identity/configuration. The full-device universal composition separately owns
SoftAP, its local HTTP server, Wi-Fi credential storage, GPIO, OTA, and restart
policy. Every alternate setup channel ultimately supplies the same bounded
`ProvisioningHandoff`, so no Wi-Fi fields leak into the channel-neutral SDK.

## Arduino ESP client

For an ESP32 or ESP8266 Arduino project that owns its GPIO and application logic,
include one public header:

```cpp
#include <FlovaEsp32.h>

FlovaEsp32 flova;
auto led = flova.datastream<bool>("LED");

static flova::WriteResult writeLed(void* context, bool value) {
  digitalWrite(*static_cast<uint8_t*>(context), value ? HIGH : LOW);
  return flova::accept();
}

void setup() {
  WiFi.begin("your-wifi", "your-password");
  static uint8_t pin = LED_BUILTIN;
  pinMode(pin, OUTPUT);
  led.onWrite(writeLed, &pin);
  flova.begin();
}

void loop() { flova.run(); }
```

`FlovaEsp32` and `FlovaEsp8266` are explicit board compositions over the same
typed application API. Include the matching board header; shared code does not
inspect `ESP32` or `ESP8266` macros. Datastream names are declared by the
application and resolved to the Engine's numeric IDs during the authenticated
binding handshake. The handler receives a context pointer, runs from
`flova.run()`, and may safely own the board's GPIO or application state.
`begin()` restores persisted Flova identity or enters `AwaitingProvisioning`.
It never opens a setup AP. `startProvisioning()` remains available for a
deliberate factory reset, but the application decides how its setup channel and
network behave.

The normal application path stops at the board header. Advanced Arduino ports
that need to provide their own Link or provisioning adapter may include
`<FlovaArduino.h>` and compose `FlovaClient` directly. The individual transport,
TLS, codec, and adapter headers are implementation seams for that advanced path,
not beginner entry points.

To replace the setup channel, implement the small `FlovaProvisioningAdapter`
callbacks and use the borrowed-service runtime:

```cpp
FlovaEsp32Storage storage;
FlovaEsp32Entropy entropy;
ArduinoFlovaLink link(entropy);
ArduinoFlovaClock clock;
ArduinoFlovaLogger logger;
ArduinoFlovaHardware hardware;
MyProvisioningAdapter provisioning;
FlovaClient flova(link, provisioning, storage, clock, logger, entropy, hardware);
```

The adapter calls the supplied `FlovaProvisioningHandler` after decoding its
channel into `flova::ProvisioningHandoff`. Channel-specific runtime data stays
inside that adapter rather than being interpreted by the core.

`FlovaClient` owns no default Link, SoftAP, or provisioning implementation.
The application initializes its storage service before `flova.begin()` and
keeps every borrowed service alive for the lifetime of the client. A custom
Link implements `FlovaClientLink`; no second transport abstraction is required.

Restart and OTA handling are explicit in this tier. Register
`setRestartHandler(...)` to perform a platform reset automatically, or poll
`restartRequired()` and `restartReason()` and reset at an application-safe
time. OTA installation is disabled unless the application calls
`enableOta()`. Universal firmware enables OTA and installs its automatic
restart handler.

Factory tooling and custom setup channels pass the same product-independent
handoff; device identity and secret are not compiled into the binary:

```cpp
flova::ProvisioningHandoff handoff(
    "wss://engine.example/api/device-link", shortLivedToken);
flova.provision(handoff);
```

Custom ports set the required `FLOVA_*_CAPACITY` values in their build profile.
Those macros describe fixed memory layout only; runtime behavior belongs in
composed service classes.

Install `Flova ESP32` or `Flova ESP8266` for a board-specific package; both
expose the same typed API through explicit board classes. `flova-embedded-sdk` remains the portable option
for non-Arduino boards, where the application supplies its own four services.

For a production custom board, implement `flova::LinkBootstrapClient` and use
`flova::Provisioner`; this keeps Link v1 bootstrap, session validation, atomic
replacement, and UTC seeding identical across setup channels. Commit streamed
configuration records before reporting bootstrap completion. See
`provisioning.md`.

An Ethernet or cellular adapter can carry the Device Link binary frames over WSS. BLE, LoRaWAN, serial, CAN, Modbus, or another fieldbus normally sends a compact envelope to a gateway, which translates it to Flova Cloud. Those adapters must preserve command IDs, revisions, origins, acknowledgements, and expiry decisions.

See `examples/custom-board-basic`. Build and test it without an embedded toolchain:

```sh
cmake -S . -B /tmp/flova-core-build
cmake --build /tmp/flova-core-build
ctest --test-dir /tmp/flova-core-build --output-on-failure
```

All new board ports use `flova::Device` directly. Board packages supply the required adapters and must not create a second datastream engine.

Custom firmware owns its factory-reset input. Universal template
`firmware_system.factory_reset` settings are not applied automatically by a
custom board port; call `setFactoryResetButton(...)` explicitly in Arduino
ports or implement the same deliberate boot gesture in the board runtime.

## Template hardware identifiers

For a custom board, a template hardware mapping's `pin` is an opaque identifier
owned by that board's firmware, such as `PWM_A`, `ADC_BATTERY`, or `relay:1`.
Flova validates only that the identifier is non-empty and bounded. Unlike the
universal ESP32 and ESP8266 targets, the custom-board runtime does not
automatically call GPIO APIs.

Use `onWrite()` to bind the typed datastream to the board HAL, or control the
HAL independently and call `report()` afterward. This
keeps the cloud contract numeric or boolean while the port controls native ADC
resolution, PWM resolution, safety checks, and actual peripherals.
