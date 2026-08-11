# Protocol and CBOR Rules

## Goal

Flova device protocol must remain standards-based, compact, bounded, easy to parse, and cheap in RAM and CPU.

The transport is long-lived WSS.

The application encoding is CBOR.

## Decoder

Do not construct a generic dynamic CBOR DOM on constrained targets.

Avoid:

```text
CBOR bytes
→ generic value tree
→ dynamic maps/arrays
→ typed application object
```

Use:

```text
CBOR bytes
→ bounded typed decoder
→ validated fields
```

Process fields directly where practical.

Unknown fields may be skipped for forward compatibility.

Required fields must be validated.

Malformed or duplicate fields must have deterministic handling.

## Encoder

Encode directly from typed application state into a bounded output buffer or transport writer.

Avoid unnecessary intermediate representations and payload copies.

Preferred path:

```text
typed state
→ CBOR encoder
→ bounded TX storage
→ WSS
```

## Bounds

Define explicit limits for:

- complete application frame size
- text-string length
- byte-string length
- array length
- map length
- CBOR nesting depth
- command argument count
- RPC argument count
- configuration chunk size
- telemetry batch size
- pending messages
- identifiers

Reject oversized input before expensive processing.

## Large payloads

Large configuration payloads are chunked.

Use a bounded lifecycle such as:

```text
CONFIG_BEGIN
CONFIG_CHUNK
CONFIG_CHUNK
CONFIG_COMMIT
```

Every chunk fits comfortably inside the normal memory budget.

OTA is streamed over HTTPS.

Never buffer an entire OTA image in RAM.

## Runtime schema

Use compact numeric opcodes and positional or compact numeric fields in hot-path messages.

Example:

```text
[WRITE, 17, 23.7]
```

Avoid repeatedly transmitting verbose field names or datastream strings.

The protocol remains normal standards-compliant CBOR; Flova owns the application schema.

## WSS

Keep the primary WSS connection long-lived.

Do not reconnect unnecessarily.

Application-level frame limits must remain bounded regardless of WebSocket library capabilities.

Avoid redundant copies between:

```text
WebSocket receive buffer
CBOR buffer
message object
application buffer
```

Parse directly from bounded received data where the library safely permits it.

## Lifecycle

Connection behavior must use explicit states.

Example:

```cpp
enum class ConnectionState : uint8_t {
    Offline,
    WifiConnecting,
    TlsConnecting,
    Authenticating,
    Online,
    Backoff
};
```

Use explicit state machines for:

- connection
- authentication
- provisioning
- configuration update
- reconnect/backoff
- OTA

Resource acquisition and release must be obvious at state boundaries.

## Errors

Prefer compact structured error codes.

Example:

```cpp
enum class ErrorCode : uint16_t {
    InvalidFrame,
    FrameTooLarge,
    InvalidCbor,
    MissingField,
    InvalidState,
    QueueFull,
    TransportFailure
};
```

Do not generate large dynamic diagnostic strings in low-memory paths.

## Portability

Keep protocol semantics independent from ESP8266-specific APIs.

Hardware-dependent transport, storage, clocks, TLS integration, and platform hooks belong behind narrow target adapters.

The same core protocol logic must remain reusable across future targets.
