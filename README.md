# Flova Firmware MVP

SDK-style PlatformIO firmware for the MVP vertical slice.

- Core SDK: `packages/flova-device-sdk`
- Arduino adapter: `packages/flova-arduino`
- ESP8266 wrapper: `packages/flova-esp8266`
- ESP32 wrapper: `packages/flova-esp32`
- Universal firmware: `examples/universal-esp8266`, `examples/universal-esp32`
- Typed API examples: `examples/datastream-api-esp8266`, `examples/datastream-api-esp32`
- Portable C++11 custom-board example: `examples/custom-board-basic`

Start with the [.docs index](.docs/README.md) for architecture, API semantics, MQTT behavior, and the validation matrix.

For STM32, RTOS, PLC, gateway, Ethernet, cellular, BLE, or LoRaWAN integrations, include `FlovaCore.h` and provide the four small link/storage/clock/logger services described in the [custom-board guide](.docs/custom-boards.md).

Use `FlovaProvisioning.h` for shared Engine redemption and session storage. Clock synchronization and offline policy behavior are documented in [clock and offline data](.docs/clock-and-offline.md).

On first boot the ESP starts a `Flova-*` SoftAP. The app posts Wi-Fi credentials and the Engine provision token to `http://192.168.4.1/provision`; firmware redeems the token with Engine and stores the returned MQTT credentials.

Build:

```sh
pio run -e universal-esp32
pio run -e universal-esp8266
```

ESP32 transactional recovery is optional: build `universal-esp32-ab-4m` or
`universal-esp32-ab-8m` to use the supplied A/B layouts and health-gated rollback.

OTA releases are managed by Engine and delivered as authenticated MQTT offers on `ota/desired`. Universal ESP32/ESP8266 firmware downloads the immutable binary directly, verifies its SHA-256, installs it outside the MQTT callback, reboots, and reports progress on `ota/reported`. See [`.docs/ota.md`](.docs/ota.md).

## Local-first datastreams

`FlovaDevice::datastream<T>(key)` keeps a fixed-size local snapshot. `read()` only reads that cache; it never reads hardware or the network. `refresh()` calls the registered reader and reports its result. `report()` records an observed value without running an actuator handler. `write()` runs the same local handler used by MQTT writes, then updates and synchronizes the accepted state.

```cpp
auto relay = flova.datastream<bool>("relay")
  .mode(FlovaDatastreamMode::State)
  .offline(FlovaOfflinePolicy::KeepLatest)
  .persist(FlovaPersistencePolicy::Persistent);

relay.onWrite([](bool enabled) {
  if (enabled && tankEmpty()) return FlovaWriteResult::reject("dry_run_protection");
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
  return FlovaWriteResult::accept();
});
```

State written while disconnected is applied locally and the newest value is sent after reconnect. MQTT commands use this exact handler and publish a structured command result only after acceptance or rejection. Scheduled actions use the same handler and safety checks.
