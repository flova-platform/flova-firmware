# Datastream API

Create a typed handle from the board wrapper or `FlovaDevice`:

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
```

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
