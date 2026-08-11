# Memory Architecture

## Goal

Flova firmware must have deterministic and bounded memory behavior on constrained targets, especially ESP8266.

The relevant number is not only static RAM usage. Runtime stability depends on:

- minimum observed free heap
- largest contiguous free heap block
- fragmentation
- stack headroom
- temporary TLS and networking peaks
- repeated lifecycle behavior

## Runtime allocation

Flova-owned protocol, transport, telemetry, configuration, RPC, state, and device SDK hot paths must not depend on arbitrary runtime heap allocation after initialization.

Avoid in runtime paths:

```cpp
malloc()
calloc()
realloc()
free()
new
delete
```

Avoid dynamically growing containers in constrained hot paths:

```cpp
std::vector
std::list
std::map
std::unordered_map
std::string
std::shared_ptr
```

Arduino `String` must not be used for repeated mutation or concatenation in core protocol and networking code.

External Wi-Fi, TLS, and WebSocket libraries may allocate internally. Flova code must not add avoidable allocation pressure around them.

## Fixed-capacity storage

Use compile-time bounds and fixed-capacity storage.

Examples:

```cpp
constexpr size_t MAX_FRAME_SIZE = ...;
constexpr size_t MAX_STRING_LENGTH = ...;
constexpr size_t MAX_ARRAY_ITEMS = ...;
constexpr size_t MAX_MAP_ITEMS = ...;
constexpr size_t MAX_NESTING_DEPTH = ...;
constexpr size_t MAX_PENDING_MESSAGES = ...;
```

Never allocate based directly on network-controlled lengths.

Reject oversized input before expensive processing.

## Stack safety

Do not place large buffers on the stack.

Bad:

```cpp
void handleMessage() {
    uint8_t payload[2048];
    char temp[1024];
}
```

Large reusable buffers belong in explicitly owned long-lived storage or a bounded shared workspace.

Keep stack locals small.

Do not use recursion.

Nested parsing must use a fixed-size iterative context stack.

Example:

```cpp
ParseContext contexts[MAX_NESTING_DEPTH];
```

## Shared workspace

Mutually exclusive operations may reuse one bounded workspace.

Example:

```cpp
class Workspace {
public:
    std::array<uint8_t, WORKSPACE_SIZE> buffer;
};
```

Potential users:

- configuration decoding
- provisioning parsing
- diagnostics serialization
- bounded protocol processing

Ownership must be explicit and concurrent reuse must be impossible.

## Ownership

Prefer composition and references.

Example:

```cpp
class Protocol {
public:
    Protocol(Transport& transport, ProtocolBuffers& buffers);

private:
    Transport& transport_;
    ProtocolBuffers& buffers_;
};
```

The root device object owns long-lived components.

Dependencies receive non-owning references.

Do not hide ownership behind internal `new`.

## TLS and networking

TLS is one of the highest-memory operations on ESP8266.

Reuse expensive TLS-related state and trust-anchor representations.

Keep one primary device connection and one deliberate RX/TX strategy.

Do not maintain multiple large TLS sessions simultaneously during normal operation.

Provisioning, WSS, and OTA must have explicit lifecycle boundaries.

Suspend other memory-heavy network work while OTA owns the constrained network-memory budget.

## Queues

All queues must be fixed capacity.

Use ring buffers or equivalent bounded structures.

Every full queue has an explicit policy.

Recommended behavior:

- telemetry: coalesce or drop replaceable old samples
- state: retain newest state
- command/RPC response: preserve until timeout or delivery policy resolves it
- critical durable event: move to bounded persistent storage

Network loss must never cause unlimited RAM growth.

## Durable storage

RAM is not durable backlog storage.

Use a bounded flash journal or flash ring when offline persistence is required.

Define:

- maximum retained records
- overwrite behavior
- expiration behavior
- flash write-rate control

## Strings

Do not build protocol messages using repeated string concatenation.

Prefer:

- CBOR-native fields
- fixed buffers
- bounded `snprintf`
- flash-resident literals
- string views or non-owning references where appropriate

## Memory instrumentation

Track at minimum:

```text
current free heap
minimum free heap
largest free block
heap fragmentation
stack headroom
minimum observed stack headroom
```

Capture snapshots around:

```text
boot
Wi-Fi connect
before TLS handshake
after TLS handshake
WSS connect
authentication
configuration processing
reconnect
before OTA
during OTA
after OTA
```

The low-water mark is part of the firmware acceptance criteria.

## Build-time budgets

CI must track:

```text
flash usage
static RAM
IRAM
firmware binary size
OTA slot compatibility
```

Expose deltas against the previous accepted build.

Unexpected RAM or flash growth must be investigated and justified.
