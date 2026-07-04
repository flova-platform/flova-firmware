# Flova Firmware MVP

SDK-style PlatformIO firmware for the MVP vertical slice.

- Core SDK: `packages/flova-device-sdk`
- Arduino adapter: `packages/flova-arduino`
- ESP8266 wrapper: `packages/flova-esp8266`
- ESP32 wrapper: `packages/flova-esp32`
- Examples: `examples/esp8266-touch-led`, `examples/esp32-touch-led`

On first boot the ESP starts a `Flova-*` SoftAP. The app posts Wi-Fi credentials and the Engine provision token to `http://192.168.4.1/provision`; firmware redeems the token with Engine and stores the returned MQTT credentials.

Build:

```sh
pio run -e esp32-touch-led
pio run -e esp8266-touch-led
```

OTA install/campaign logic is intentionally not implemented. The SDK only reports OTA metadata and exposes `handleOtaOffer` as a future hook.
