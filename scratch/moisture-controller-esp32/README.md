# Temporary ESP32 moisture controller

This scratch application targets a classic ESP32 DevKit (`esp32dev`). It has
one hardware input: the moisture sensor's analog output, and controls the
board's built-in LED through the `led` boolean datastream. It reports the
`MOISTURE` datastream as a percentage through Flova MQTT. The LED datastream is
read/write, so a PWA toggle command is applied to the hardware and the SDK
reports the accepted state back to Engine.

## Wiring

| Moisture sensor pin | ESP32 DevKit |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `GND` |
| `AO` / analog output | `GPIO32` (`ADC1_CH4`) |
| `DO` / digital output | Leave unconnected |

Use `3V3` for the sensor supply. Do not feed a 5 V analog output into GPIO32.
The sensor and ESP32 must share ground. There are no display connections in
this version. The built-in LED does not need external wiring.

GPIO32 is an ADC1 input, so it remains available while the ESP32 uses Wi-Fi.
The firmware samples it at 12-bit resolution and averages eight readings.

## Calibration

The initial ESP32 raw calibration values are:

| Key | Initial value | Meaning |
| --- | ---: | --- |
| `wet_adc` | 1200 | Sensor raw value when wet |
| `dry_adc` | 3000 | Sensor raw value when dry |

These values depend on the sensor and soil. Adjust the same settings in the
template's **تنظیمات فریم‌ور** CRUD, or replace the defaults in `src/main.cpp`.
The `MOISTURE` and `led` keys must exist in the device's published template;
`MOISTURE` is numeric/read-only and `led` is boolean/read/write.

## Build and upload

From this folder:

```sh
pio run -e esp32dev
pio run -e esp32dev --target upload
pio device monitor -b 115200
```

The temporary MQTT credentials belong in the ignored `src/credentials.h` file.
