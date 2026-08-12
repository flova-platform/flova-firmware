# Datastream API

Create a typed handle from an explicit board wrapper:

```cpp
auto temperature = flova.datastream<float>("temperature");
auto relay = flova.datastream<bool>("relay");
```

Supported codecs are `bool`, `float`, `double`, and Arduino `String`.

## Operations

- `read()` returns the latest cached value and returns the type's default value before initialization. Use `hasValue()` when initialization matters.
- `snapshot()` adds origin, quality, revision, update time, stale, and dirty metadata.
- `report(value)` records an observed value without invoking an actuator.
- `refresh()` invokes `onRead`, then reports a successful result.
- `write(value)` invokes `onWrite`; only accepted/no-change results can become authoritative state.
- `publishEvery(milliseconds)` coalesces reports to the latest value until the interval is due.

```cpp
temperature.onRead([]() {
  if (!sensor.available()) return FlovaReadResult<float>::error("sensor_unavailable");
  return FlovaReadResult<float>::success(sensor.readCelsius());
});

relay.onWrite([](bool enabled) {
  if (enabled && emergencyStop()) return FlovaWriteResult::reject("emergency_stop");
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
  return FlovaWriteResult::accept();
});

temperature.publishEvery(5000);
```

Single-reading publication remains the default. Applications with several
readings due together may opt in once per device with
`flova.enableStateBatching(32, 100)`. Batches never exceed 32 readings or the
negotiated Device Link frame-size limit; event-mode datastreams bypass batching.

## Modes

Modes describe delivery meaning, not data type:

- `State`: the latest durable/current value matters.
- `Sample`: any device-observed time-series value; it can be numeric, boolean, or textual.
- `Command`: an imperative operation that must not be replayed later.
- `Event`: a point-in-time occurrence with identity.

`Sample` is intentionally generic. A status string, counter, or sensor value can all be samples.

## Offline and persistence

`KeepLatest` applies writes locally and coalesces the newest unsynchronized state. `Drop` keeps local cache but does not queue network publication. `Reject` and bounded `StoreHistory` are reserved public policies; full history storage is a follow-up.

Persistence is opt-in:

```cpp
auto relay = flova.datastream<bool>("relay")
  .mode(FlovaDatastreamMode::State)
  .offline(FlovaOfflinePolicy::KeepLatest)
  .persist(FlovaPersistencePolicy::Persistent);
```

Persistence restores cache only. It does not energize an actuator at boot. Product firmware must explicitly choose and implement safe hardware restoration.

Complete board examples live in `examples/datastream-api-esp32` and `examples/datastream-api-esp8266`.
## Hardware mappings

Universal ESP32 and ESP8266 firmware automatically binds the template hardware
mapping kinds `digital_input`, `digital_output`, `analog_input`, and
`pwm_output`.

- Analog inputs publish the board's raw ADC count at `sample_interval_ms`.
- PWM outputs accept any numeric range declared by the datastream's
  `min_value` and `max_value`. The default range is 0–100. Firmware maps the
  value linearly to the target's native PWM duty range, so a template may use
  0–255 or 0–1023 when raw duty values are more useful.
- Commands outside the declared PWM range are rejected without changing the
  cached state or hardware output.

Custom-board ports do not receive automatic GPIO bindings. Their pin string is
an opaque, firmware-owned identifier. Bind the typed datastream with `onRead`
or `onWrite` and interpret that identifier in the board port.
