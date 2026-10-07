# Custom MQTT example

This opt-in example uses `<FlovaMqtt.h>` with PubSubClient and ArduinoJson.
It is for application-owned code that chooses MQTT instead of Flova Link.

Before flashing, replace the Wi-Fi values and the device UUID/secret in
`src/main.cpp`. The secret is the one-time value returned by the Console or
the MQTT credential rotation API. Store it in the application’s protected
storage in a real product.

Build either target:

```sh
pio run -e custom-mqtt-esp32
pio run -e custom-mqtt-esp8266
```

The example uses `mqtts://mqtt.flova.ir:8883`, publishes bounded JSON to
`up/datastreams/{key}`, reports presence on `up/presence`, and subscribes to
its own `down/config` and `down/datastreams/{key}` topics. It accepts only
exactly registered datastream keys and payloads up to 2,048 bytes.

Use `client.report`, `client.heartbeat`, and `client.acknowledgeConfig` from
your application loop. Apply and persist a received configuration before
acknowledging its generation and checksum.
