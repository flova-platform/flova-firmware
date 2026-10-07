# Custom MQTT transport

The board facade uses Flova Link by default:

```cpp
FlovaEsp32 device;
```

To use MQTT, pass the application-owned PubSubClient directly:

```cpp
PubSubClient mqtt(socket);
FlovaEsp32 device(mqtt);

void setup() {
  device.begin(DEVICE_ID, DEVICE_SECRET);
}
```

The application owns Wi-Fi, TLS certificates, and the MQTT client. The board
facade owns bounded topic handling, reconnects, presence, the Last Will, and
the JSON callback queue. `device.run()` is enough to maintain the connection.

Datastream code is transport-neutral. The same helper calls work with the
default Flova Link constructor and with the MQTT constructor:

```cpp
auto temperature = FLOVA_DATASTREAM(device, float, "temperature");
auto relay = FLOVA_DATASTREAM(device, bool, "relay");

void writeRelay(bool enabled) {
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
}

void setup() {
  FLOVA_ON_WRITE(relay, writeRelay);
}

void loop() {
  FLOVA_REPORT(temperature, 23.4f);
}
```

`FLOVA_ON_WRITE` subscribes to the MQTT command topic and acknowledges the
typed callback result automatically. `FLOVA_REPORT` publishes structured JSON
with a `value` field. Presence and reconnect handling stay inside the adapter.

MQTT uses `mqtt.flova.ir:8883`, the device UUID as username, and the device
secret as password. It supports bounded datastream reports, presence, device
info, config delivery, and config acknowledgments through the documented JSON
topics.
