# Architecture

## Product boundary

Flova Cloud runs end-user automations, schedules, notifications, and cross-device workflows. Firmware contains developer-authored product logic and safety rules. The device does not download or execute user-authored rules, scripts, or automation bytecode.

Datastreams are the shared contract across firmware, Cloud, mobile experiences, and operations dashboards. A physical change, local condition, direct user action, and cloud automation all resolve to a datastream value, but firmware remains authoritative about whether hardware can safely accept a write.

## Local-first lifecycle

Each registered datastream owns a bounded in-memory snapshot: value, origin, quality, revision, update time, and dirty state.

```text
local write or MQTT write
  -> validate key and value type
  -> reject non-writable semantics
  -> invoke the registered hardware handler
  -> on acceptance only: update cache and optional persistence
  -> publish device-originated state, or mark KeepLatest state dirty offline
  -> return locally, or publish one cloud command result with applied value
```

`read()` never touches hardware or the network. `refresh()` reads hardware and enters the `report()` path. `report()` records an observed value and never calls the actuator handler.

## Portable core

`FlovaCore.h` owns transport-independent typed state and accepts structured envelopes through `flova::Link`. MQTT topics, JSON, GPIO, provisioning, restart behavior, and network credentials belong to adapters. This permits Ethernet, cellular, BLE, LoRaWAN, serial, CAN, gateway, STM32, and PLC integrations without changing datastream semantics.

The portable C++11 core supports 12 registered datastreams and 96-byte bounded identifiers/text values by default. It uses fixed arrays, explicit callback types, no exceptions, and no RTTI. It is the only datastream engine.

## Execution and bounds

`flova::Device::run()` polls the injected link and flushes dirty state. Board adapters own connectivity and hardware polling. Limits are 12 cached datastreams, 8 compiled schedules, 96 occurrences per schedule, and 4 recent command IDs. KeepLatest uses the snapshot as the single coalesced pending value.

ESP32 persists its versioned device configuration as current and previous
single-blob snapshots in Preferences/NVS. ESP8266 stores the same configuration
contract in LittleFS with verified current and backup files. ESP8266 retains its
4 KiB emulated EEPROM layout for bounded runtime state, including eight
224-byte hashed datastream snapshot slots. Records include their datastream key;
a hash collision can discard an older snapshot but cannot restore it into the
wrong datastream.

## Deliberate MVP limits

Rate/deadband configuration, schema min/max/enum metadata, and hardware restoration policies beyond cache restore remain follow-up work. Commands are delivered by Cloud with expiry and are never queued by firmware for later execution.
