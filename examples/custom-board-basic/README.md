# Custom board

This example compiles with a normal C++11 compiler and has no Arduino, ESP, or Wi-Fi dependency. Implement `flova::Link`, `flova::Storage`, `flova::Clock`, and `flova::Logger` using your board or PLC SDK, then construct `flova::Device`.

The link exchanges structured bounded `flova::Message` values. Internet-facing
adapters use verified WSS plus the SDK-owned `FlovaLinkCodec.h` frame and generated CDDL CBOR
primitives. Gateway transports such
as BLE, LoRaWAN, serial, CAN, or fieldbus may map the structured messages into
their bounded carrier protocol.

Build from the repository root with CMake or the documented direct compiler command.

## Datastream flow

Declare a stream with a human-readable key, then choose the direction:

```cpp
auto relay = device.datastream<bool>("relay");
relay.onWrite(setRelay);       // incoming user/cloud/automation commands
relay.write(true);             // local application command
relay.report(true);            // external observation
bool current = relay.value();  // local cache; no network request
```

`onWrite()` is the safe hardware boundary. A rejected result prevents the
cached value, revision, persistence, and outbound state from changing. A
server-side automation is evaluated by Engine and arrives at the device as a
normal write command, so custom boards do not need a separate automation API.

Call `Device::run()` frequently. Link callbacks only queue bounded messages;
the board loop is where remote commands and hardware writes are applied.
