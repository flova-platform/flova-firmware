# Custom Arduino client

This is the recommended starting point for an existing ESP32 or ESP8266
Arduino application. It keeps Wi-Fi, GPIO, and application logic in the
sketch and uses Flova only for the bounded Device Link/datastream contract.

The public API is one board-specific header:

```cpp
#include <FlovaEsp32.h>   // use FlovaEsp8266.h on ESP8266
```

Declare a datastream before `begin()`, attach a typed handler with a context
pointer, and call `client.run()` from the normal Arduino loop. Remote commands
are applied from that loop, so the handler can safely update the application's
own hardware state. This example keeps Wi-Fi and device credentials in the
application for the static-credentials path. For phone provisioning without
hardcoded Wi-Fi credentials, build `custom-arduino-provisioning`.

Replace the Wi-Fi credentials, device UUID, secret, and WSS URL in
`src/main.cpp`. Build from the repository root with:

```sh
pio run -e custom-arduino-client-esp32
pio run -e custom-arduino-client-esp8266
```

For a non-Arduino board, use `examples/custom-board-basic` and implement the
four portable `flova::` services directly.
