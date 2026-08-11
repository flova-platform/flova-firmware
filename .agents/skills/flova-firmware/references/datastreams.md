# Datastream Architecture

## Locked model

Flova datastreams use a human-readable string key at the SDK, API, configuration, and UI layers and a stable server-assigned `uint16_t` identifier on the device runtime protocol.

This is the only datastream identity model used by firmware and SDK.

## Canonical datastream

Every datastream has:

```text
id
key
display_name
type
metadata
```

Example:

```text
id:           17
key:          "living_room.temperature"
display_name: "Living Room Temperature"
type:         float
```

Responsibilities:

```text
id
→ protocol identity

key
→ SDK/API identity

display_name
→ UI identity
```

The numeric ID never depends on the key or display name.

## Numeric ID

Use:

```cpp
using DatastreamId = uint16_t;
```

Namespace:

```text
0
reserved as invalid or unresolved

1–65535
valid datastream IDs
```

The server assigns the ID when the datastream is created.

The ID remains stable for the lifetime of the datastream.

Renaming a key or display name does not change the ID.

Deleted IDs are retired and are not immediately recycled.

## Datastream key

The key is UTF-8 and unique within the device or template namespace.

Examples:

```text
temperature
humidity
relay
motor.speed
living_room.temperature
```

Lock:

```cpp
constexpr size_t MAX_DATASTREAM_KEY_LENGTH = 48;
```

The key exists for:

```text
SDK declarations
developer-facing API
configuration
server database
dashboard configuration
diagnostics
initial binding
```

The key is not repeatedly transmitted in normal runtime telemetry.

## Display name

The display name is UI metadata only.

It is never used for runtime identity, routing, protocol dispatch, or lookup.

## ESP8266 runtime representation

Use numeric runtime entries.

Example:

```cpp
struct DatastreamRuntime {
    DatastreamId id;
    uint8_t type;
    uint8_t flags;
};
```

Do not keep fixed-size copies of all datastream keys in RAM.

Do not maintain a runtime string-to-ID hash table.

Do not use hashes as identity.

Do not use CBOR string-reference tables as identity.

Do not use connection-scoped aliases as identity.

The server mapping is authoritative.

## Active datastream limit

Lock ESP8266 to:

```cpp
constexpr size_t MAX_ACTIVE_DATASTREAMS = 64;
```

The firmware stores only active runtime entries, not the entire 16-bit namespace.

## SDK declaration

Keep developer-facing declarations name-based.

Example:

```cpp
FLOVA_DATASTREAM(temperature, "temperature");
FLOVA_DATASTREAM(humidity, "humidity");
FLOVA_DATASTREAM(relay, "relay");
```

On ESP8266, constant keys should remain in flash rather than being copied into normal RAM.

The runtime handle stores the resolved numeric ID.

Conceptually:

```cpp
class DatastreamHandle {
private:
    DatastreamId id_;
};
```

## Binding

Datastream keys are resolved during connection or configuration.

Device:

```text
BIND
[
    "temperature",
    "humidity",
    "relay"
]
```

Server mapping:

```text
temperature → 17
humidity    → 18
relay       → 19
```

Response preserves request order:

```text
BOUND
[
    17,
    18,
    19
]
```

After binding:

```text
temperature handle → 17
humidity handle    → 18
relay handle       → 19
```

Normal runtime communication uses numeric IDs only.

## Universal firmware

Server-provisioned universal firmware receives numeric IDs in its configuration.

Example:

```text
[
    [17, TYPE_FLOAT, ...],
    [18, TYPE_FLOAT, ...],
    [19, TYPE_BOOL,  ...]
]
```

Runtime:

```cpp
DatastreamRuntime streams[MAX_ACTIVE_DATASTREAMS];
```

The ESP8266 does not keep a full datastream-name database in heap.

## Runtime writes

Developer-facing code:

```cpp
temperature.write(23.7);
```

Internal path:

```text
temperature
→ id 17
→ CBOR
→ WSS
```

Runtime CBOR:

```text
[WRITE, 17, 23.7]
```

The key is not transmitted with every update.

## Runtime reads and commands

Inbound traffic uses numeric IDs.

Example:

```text
[UPDATE, 19, true]
```

Dispatch uses the numeric ID.

Do not use `strcmp()` chains or heap-based maps.

Use a fixed-capacity handler table.

Example:

```cpp
struct DatastreamHandler {
    DatastreamId id;
    Callback callback;
};
```

## Final identity rule

```text
Human/API identity
        ↓
UTF-8 datastream key
        ↓
server resolves
        ↓
stable uint16_t DatastreamId
        ↓
CBOR numeric runtime messages
        ↓
WSS
```

Strings belong to developer interaction and configuration.

Numeric IDs belong to runtime communication.

The ESP8266 hot path never uses string names as datastream identities.
