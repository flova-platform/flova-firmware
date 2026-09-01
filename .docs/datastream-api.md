# Datastream API

Create a typed handle from the explicit board object:

```cpp
FlovaEsp8266 flova;
auto temperature = flova.datastream<float>("temperature");
auto relay = flova.datastream<bool>("relay");
auto status = flova.datastream<flova::Text>("status");
```

Supported types are `bool`, `int64_t`, `float`, `double`, and bounded
`flova::Text`. Text longer than the configured capacity is rejected with
`text_too_long`; it is never silently truncated. Keys are resolved to Engine-assigned numeric IDs during
configuration or binding; normal runtime traffic uses only those IDs.

## Write, report, and read

`write(value)` requests a state change through `onWrite`. Local, remote,
scheduled, and mapped writes use this same path. Cache, revision, persistence,
and outbound state change only after the handler accepts the operation.

```cpp
relay.onWrite([](bool enabled) {
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
});

relay.write(true);
```

A `void` handler is accepted automatically. Return a result when application
validation can reject the operation:

```cpp
relay.onWrite([](bool enabled) -> flova::WriteResult {
  if (enabled && emergencyStop()) return flova::reject("emergency_stop");
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
  return flova::accept();
});
```

`report(value)` records an observation without invoking `onWrite`. Use it for
sensors, physical inputs, external controllers, or hardware controlled directly
by application code:

```cpp
temperature.report(readTemperature());

digitalWrite(RELAY_PIN, HIGH);
relay.report(true, flova::Origin::PhysicalInput);
```

`value()` returns the latest cached value. Use `hasValue()` when an initialized
value must be distinguished from the type's default. `snapshot()` adds origin,
quality, revision, update time, and unsynchronized-state metadata. `bound()`
reports whether the key has a numeric runtime binding.

Remote writes return an `applied` command result automatically after the
handler accepts. Developers do not send acknowledgements themselves. If
hardware later observes a different value, call `report()` on the same
datastream and Flova reconciles cloud state to the device report.

## Offline and custom logic

Local `write()` and `report()` calls normally continue to work without
connectivity. The installed delivery policy decides whether state is
synchronized later. `OfflinePolicy::Reject` is the explicit exception and
returns `offline_delivery_required` before changing hardware or cached state.

An unbound datastream remains usable as local state but produces no cloud
traffic. Configuration and inbound runtime values with the wrong type are
rejected before application or hardware handlers run. Application variables
and pins need no datastream unless they should be visible to Flova.

Power-cycle restoration is explicit. Add
`.persist(flova::PersistencePolicy::Persistent)` to actuator state that should
survive reboot. After stored configuration supplies its stable runtime ID, the
SDK restores and applies that value before waiting for cloud binding. Persistence
is not the default because restoring an actuator can be unsafe and every change
also consumes flash endurance.

`begin()` establishes local storage/configuration startup; it does not promise
cloud connectivity. Use `runtimeReady()` for local-runtime readiness and
`ready()` only when code specifically needs authenticated, bound Device Link.

Advanced portable integrations may set local mode, offline retention, history,
and persistence policies on `flova::Datastream<T>`. Normal ESP applications
should use the configuration installed by Engine and only call `write()`,
`report()`, and `onWrite()`.

## Hardware mappings

Universal ESP32 and ESP8266 firmware binds template mappings for
`digital_input`, `digital_output`, `analog_input`, and `pwm_output`. The
universal firmware configures those pins and routes their reads and writes
automatically. Custom applications keep ownership of their HAL and connect
their own hardware with `onWrite()` or `report()`.

The Link bootstrap advertises this distinction automatically. Universal
firmware reports its bounded input/output mapping slots; custom facades report
zero slots, so Engine removes template mappings and status-indicator GPIO
configuration from new custom transfers. Older mapped configurations remain
bootable on custom firmware because the manual adapter accepts and ignores
those records. Advanced board ports can implement `flova::Hardware` directly
and advertise their own mapping profile when they want automatic mapping.

```cpp
pinMode(RELAY_PIN, OUTPUT);
relay.onWrite([](bool enabled) {
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
});
```

- Analog inputs publish the board's raw ADC count at `sample_interval_ms`.
- PWM outputs accept the configured numeric range and map it to native duty.
- Out-of-range commands are rejected without changing hardware or cached state.

Complete compile contracts live in `examples/datastream-api-esp32` and
`examples/datastream-api-esp8266`.
