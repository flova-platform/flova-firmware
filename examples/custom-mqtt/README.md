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

Use the short callback form when no callback context is needed:

```cpp
device.subscribe("relay");
device.onMessage([](const char* path, JsonObjectConst payload) {
  // Handle "config" or "datastreams/relay" here.
});
```

The custom example keeps the application code small: it applies the relay
command, acknowledges it, and reports a temperature. Presence and reconnect
handling stay inside the adapter.

MQTT uses `mqtt.flova.ir:8883`, the device UUID as username, and the device
secret as password. It supports bounded datastream reports, presence, device
info, config delivery, and config acknowledgments through the documented JSON
topics.
