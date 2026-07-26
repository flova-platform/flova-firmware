# Custom boards and industrial targets

`FlovaCore.h` is a header-only C++11 runtime with no Arduino, ESP, Wi-Fi, MQTT, exception, or RTTI dependency. It can run on an STM32 HAL/FreeRTOS application, another MCU SDK, an industrial Linux controller, or a PLC runtime that supports C++ integration.

Implement four bounded services:

- `flova::Link`: moves structured `flova::Message` envelopes and calls the registered receiver with its context.
- `flova::Storage`: reads and writes fixed-size records using NVS, EEPROM, flash, a file, or PLC retained memory.
- `flova::Clock`: supplies monotonic milliseconds.
- `flova::Logger`: maps SDK diagnostics to the target's bounded logger.

Custom board profiles define the `FLOVA_*_CAPACITY` build values from real RAM,
persistent storage, and transport constraints. The SDK advertises those values;
Engine applies deployment policy. Do not copy ESP capacity values blindly.

`Storage::capabilities()` reports usable bytes, maximum record size, erase
geometry, write granularity, persistence, and wear sensitivity. Configure a
`ResourceBudget` array before `Device::begin()`; history receives no implicit
budget. This makes unsupported storage fail closed instead of filling flash.

The application supplies credentials and initializes its network before or inside `Link::begin()`. Provisioning is not required by the core. ESP SoftAP provisioning remains a board-wrapper feature.

For a production custom board, implement `flova::EngineClient` and use `flova::Provisioner`; this keeps token redemption, session validation, atomic replacement, and UTC seeding identical across setup channels. See `provisioning.md`.

An Ethernet or cellular adapter may encode envelopes as the existing MQTT JSON/topic contract. BLE, LoRaWAN, serial, CAN, Modbus, or another fieldbus normally sends a compact envelope to a gateway, which translates it to Flova Cloud. Those adapters must preserve command IDs, revisions, origins, acknowledgements, and expiry decisions.

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

Use the typed datastream API to bind the identifier to the board HAL. This
keeps the cloud contract numeric or boolean while the port controls native ADC
resolution, PWM resolution, safety checks, and actual peripherals.
