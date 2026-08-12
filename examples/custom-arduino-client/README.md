# Custom Arduino client

This is the recommended starting point for an existing ESP32 or ESP8266
Arduino application. It keeps Wi-Fi, GPIO, and application logic in the sketch
and uses the board package for Flova-private storage, TLS, UTC bootstrap, and
the bounded Device Link/datastream contract.

The mental model is intentionally simple:

```text
datastream("relay")
  ├─ onWrite(...)  <- user/cloud/automation/schedule commands
  ├─ write(...)    <- local application commands
  ├─ report(...)   <- sensor or externally changed hardware observations
  └─ value()      <- local cache; never a network request
```

The public API is one board-specific header:

```cpp
#include <FlovaEsp32.h>   // use FlovaEsp8266.h on ESP8266
```

Declare a datastream before `begin()`, attach a typed handler with a context
pointer, and call `client.run()` from the normal Arduino loop. Remote commands
are applied from that loop, so the handler can safely update the application's
own hardware state. There is no device UUID or secret in source: the same
binary can be flashed to every unit. A new unit waits until the selected setup
channel supplies its short-lived Engine Link URL/token handoff, then generates
its secret locally.

Replace the application's Wi-Fi credentials in `src/main.cpp`. Build from the
repository root with:

```sh
pio run -e custom-arduino-client-esp32
pio run -e custom-arduino-client-esp8266
```

For a non-Arduino board, use `examples/custom-board-basic` and implement the
four portable `flova::` services directly.

`FlovaEsp32` and `FlovaEsp8266` are passive SDK facades. They do not start a
SoftAP, replace an existing web server, control GPIO, or reboot the board. Use
`FlovaUniversalEsp32` or `FlovaUniversalEsp8266` when Flova should own the
complete no-code device.
