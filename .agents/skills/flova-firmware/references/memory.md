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

## ESP8266 acceptance and resource lifetime

Use `packages/MEMORY.md` and `scripts/check_flova_memory.py` for the current
build evidence and ceilings. Keep rejected default-MMU capacity candidates
separate from shipped defaults. Do not enable a larger advertised capacity or
remove installation requirements just because compilation passes.

Stock ESP8266 core 3.1.2 allocates a 6,200-byte DRAM secondary stack when the first
secure client is constructed; this can abort on allocation failure. Preflight
must run before construction and include that stack unless another client
already owns it. Count RX/TX record overhead, both TLS/X509 contexts, TCP and
allocator overhead, and the OTA updater page. Recheck these costs on core upgrades.
Keep the 16,384-byte receive profile unless the actual TLS endpoint guarantees
smaller records. A 512-byte application frame is not that guarantee.
The guarded ESP8266 bounded-endpoint profile uses 2,048-byte RX / 512-byte TX
while retaining CA, hostname and certificate-date validation. Enable it only
with `FLOVA_ESP8266_BOUNDED_TLS_RECORDS` after handshake, Link and OTA record
captures prove the endpoint contract. A library default or successful build is
not proof of server record size.

A single bounded board-owned setup server may be constructed at setup entry and
destroyed on exit. A single TLS owner may be constructed after preflight and
released on connection closure. These are deliberate resource-lifetime
boundaries, not permission to allocate per telemetry message or hardware write.
Do not destroy a setup server from inside its own handler, or retire an
application-owned server. HTTP borrowers must die before their TLS owner.
After the station associates, destroy the setup server before disabling SoftAP,
verify the AP mode bit cleared, and return to the board loop before TLS.

Use a tagged union for mutually exclusive numeric/text values. Numeric safety
limits must not reserve text storage. Preserve exact int64 comparisons and
version persisted layouts when changing their representation. Reuse encoded
configuration and typed decode storage only after the installer owns the bytes;
never reuse pending ACK or active-configuration storage.

Default-MMU acceptance includes worst-case text values/keys, a separate custom
application RAM reservation, contiguous-block checks, and runtime low-water
measurements. A passing static budget is necessary, not hardware acceptance.

## Phase ownership regression rules

`FlovaPhaseStorage<T>` owns one compile-time-sized Arduino lifecycle object.
Create only at boot/restore, transfer start, or after TLS handshake; never allocate
per telemetry message. Include these heap objects in the phase budget: moving
a buffer out of BSS is not itself a reduction in peak memory. The stock core
frees X509 after handshake but retains the TLS secondary stack.

ESP8266 stream descriptors are verified storage records with one bounded cache.
Keep callbacks and live values resident; registration belongs in setup after
board services exist. Missing descriptors must fail closed. Slot handles and
revision-aware ACKs must survive cache eviction and delayed responses.
