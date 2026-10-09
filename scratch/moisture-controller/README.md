# Temporary ESP8266 moisture controller

This scratch application targets a NodeMCU ESP8266 and uses FlovaSDK 0.3.10.
It joins the hardcoded `home_wifi` Wi-Fi network, reports the production
`test10` template's `MOISTURE` datastream as a `double`, and publishes through
Flova MQTT using the existing `test10` device identity.

## Wiring

| Part | NodeMCU |
| --- | --- |
| ST7789 VCC | 3V3 |
| ST7789 BLK | 3V3 |
| ST7789 GND | GND |
| ST7789 SCL | D5 |
| ST7789 SDA | D7 |
| ST7789 RES | D2 |
| ST7789 DC | D1 |
| Moisture VCC | 3V3 |
| Moisture GND | GND |
| Moisture AO | A0 |

The display and sensor share the one 3V3 pin through a breadboard rail or a
splitter. The built-in LED is the dry-state indicator. It is not a pump output.
This seven-pin panel has no external CS connection; leave D0 unconnected.

## Build and upload

From this folder:

```sh
pio run
pio run --target upload
pio device monitor
```

The firmware declares these integer template settings:

| Key | Initial value | Meaning |
| --- | ---: | --- |
| `wet_adc` | 280 | Sensor raw value in water |
| `dry_adc` | 608 | Sensor raw value in dry air |

Create or edit the same settings in the template’s **تنظیمات فریم‌ور** CRUD
and set their initial values if the device should receive them from Flova.
The temporary MQTT credentials are kept in the ignored `src/credentials.h`
file and are not committed.
