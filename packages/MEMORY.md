# Firmware memory acceptance

The released universal ESP8266 image uses the verified 64-stream, stock-DRAM
profile. The linked-image checks below remain build evidence; runtime TLS peaks
and fragmentation still require hardware acceptance. ESP32 keeps its existing
capacities.

## Changes and compatibility

- Values share numeric/text storage; safety bounds contain numeric data only.
  Integer safety comparisons retain all 64 bits. Keys have their own 49-byte
  storage, supporting the wire contract's 48 bytes plus terminator.
- Binding uses at most nine keys per request, with correlation, generation,
  cross-batch uniqueness and timeout checks. The existing Engine supports
  independent binding requests; no CDDL or endpoint change is required.
- Configuration records retain encoded bytes only. The installer takes its
  own copy before a shared workspace is reused for typed semantic validation.
  Validation still precedes durable acceptance and ACK.
- Universal setup services exist only during setup. Teardown releases request
  buffers and registered handlers before TLS; application-owned servers are
  untouched. Setup can be recreated after failure.
- ESP8266 CA PEM data lives in flash. Link/OTA own a single TLS client at a time;
  closing it releases the 6,200-byte stock BearSSL secondary stack when its last
  owner exits. Preflight precedes client construction. OTA borrows the stopped
  Link write buffer and destroys HTTP borrowers before the TLS owner.
- Arbitrary endpoints remain 16,384-byte RX / 512-byte TX. The guarded
  bounded-endpoint candidate uses 2,048-byte RX / 512-byte TX while retaining
  CA, hostname and certificate-date validation. Preflight includes contexts,
  record overhead, the secondary stack, a 4 KiB
  safety reserve plus 3 KiB TCP/allocator allowance, and a 4 KiB updater page.
  Optional IRAM exhaustion permits a sufficiently large DRAM fallback.

This changes low-level `Value` layout and the advanced
`FlovaClientLink::decodeStoredConfigurationUnit` interface. Public typed
registration/write/report APIs remain intact. Persisted values, command
results, history metadata and schedule manifests have new magic values;
incompatible old records are not restored. Verified configuration and device
credentials retain their formats. Schedules can be reconstructed from verified
configuration. Previously persisted output values/history/results are not
migrated: installations depending on offline output restoration must account
for this breaking change before adopting the build. No release is published by
these changes.

## Reproducible build evidence

Run:

```sh
pio run -e universal-esp32 -e universal-esp8266
python3 scripts/check_flova_memory.py universal-esp32 universal-esp8266
python3 scripts/check_flova_memory.py --stock-dram universal-esp8266
```

The final command is expected to fail until the product memory target is met.
Do not weaken that gate to promote the candidate. Normal invocation enforces
reviewed linked-image ceilings from `scripts/flova_memory_budgets.json`.
Changing those ceilings requires reviewing the actual RAM/flash delta. ESP8266
figures include `.noinit`, unlike PlatformIO's RAM summary.

Measured with PlatformIO espressif8266 4.2.1 / Arduino core 3.1.2 and
espressif32 7.0.1. Revalidate the context/secondary-stack costs on core updates.
The original PlatformIO static-RAM summaries were 57,896 bytes for ESP8266
(four streams, shared IRAM) and 123,664 bytes for ESP32 (64 streams).

Current linked images (bytes):

| Environment | Static DRAM including `.noinit` | IRAM | Firmware image |
| --- | ---: | ---: | ---: |
| `universal-esp8266` | 46,776 | 29,015 | 565,376 |
| `universal-esp32` | 73,016 | 84,714 | 1,044,672 |

Compared with the first reduction pass, the 64-stream universal image sheds
18,860 static DRAM bytes and the SDK probe sheds 18,732. Some storage moved to
phase-owned heap objects; these static savings are not equivalent to heap
headroom. Flash grew by roughly 3–4 KiB for lifecycle/storage handling. Reviewed
image ceilings were rounded up to the next 4 KiB boundary for that tradeoff.

## Resident state and phase ledger

ESP8266 descriptors (keys, numeric bounds, retention policy) use verified
`stream:<slot>` storage records and one bounded cache. Register streams from
`setup()`, after board services exist; registration mounts storage and can fail.
Callbacks and all live values remain resident. Slot handles do not point into
replaceable metadata. Other portable integrations retain resident descriptors
unless `FLOVA_STREAM_DESCRIPTORS_IN_STORAGE=1` is selected; storage-backed ports
must implement `Link::bindDatastreamKeys` with the bounded key-reader callback.
The old pointer-array binding seam remains available for resident descriptors.

Four delivery slots replace per-stream message timers. ACKs retire only the
revision actually transmitted. History and command results retain their durable
payloads in storage, with bounded RAM indexes/workspaces. Storage adapters must
actually support read-after-write; an adapter which discards successful writes
cannot provide these services.

`FlovaPhaseStorage<T>` owns compile-time-sized Arduino phase objects. Measured
ESP8266 allocation payloads are 2,248 bytes for configuration, 1,648 for schedules,
2,528 for transport framing, and 984 for pending control records. These are
additional heap allocations, not hidden static savings. Control/frame buffers
are allocated after TLS handshake. Borrowed callback records remain alive until
dispatch returns. Configuration buffers are released before opening TLS.

The stock core 3.1.2 source frees its 3,064-byte X509 context after handshake,
but retains the 6,200-byte TLS secondary stack. For the guarded 2,048-byte
bounded-endpoint candidate, the checker therefore reports:

| Phase | Additional minimum heap, including safety/TCP reserves |
| --- | ---: |
| Link handshake | 22,810 |
| Connected runtime | 24,906 |
| Configuration transfer | 27,154 |
| OTA | 26,906 |

These optimistic bounds exclude pre-existing Wi-Fi, filesystem, trust-anchor
and allocator heap. They use the actual 80 KiB allocator region, ending at
`0x3fffc000`, not the whole address range up to `0x40000000`.
The universal 64-stream candidate passes this theoretical linked-image bound by
7,922 bytes at the configuration phase. That estimate excludes existing Wi-Fi,
filesystem, trust-anchor and fragmentation costs, so the product target remains
unaccepted until endpoint captures and physical runtime measurements pass. The
custom SDK probe retains the full-record profile and its 8 KiB application
reservation.

Handshake and OTA maintenance pause local stream processing and schedules,
without driving outputs to a different value. Schedule progress is checkpointed
and verified before releasing its workspace. Restoration requires the expected
manifest revision and valid progress; it must not silently reset progress and
replay actions. Status snapshots expose `maintenance` and emit
`MaintenanceChanged`. Writes/reports during a pause fail with `maintenance`.
OTA currently keeps live stream values resident; it releases schedules and Link
workspaces. No claim is made that all runtime RAM is reclaimed during OTA.

A 48-byte text capacity means at most 47 text bytes plus a terminator, not 48
text streams. Current resident payloads still reserve the maximum value size for
every stream. A typed payload pool could reduce predominantly numeric workloads,
but that is not implemented here and would require revisiting the all-64-text
worst-case requirement before claiming a smaller guaranteed budget.

The custom-SDK probe contains a retained 8 KiB application reservation and
registers 64 maximum-length text values with 48-byte keys. The universal probe
reserves 64 runtime entries; a matching 64-stream server configuration is still
required to exercise all of them. Probe builds are not production firmware.

## Validation boundary

Host tests cover storage-backed 64-stream values, binding, maintenance rejection,
schedule checkpoint restoration, stale ACKs, delivery fairness, exact integer safety rejection,
configuration workspace reuse, TLS budget boundaries, optional IRAM fallback,
and existing parser/reconnect/persistence behavior. Target builds establish
compilation, static layout and stack frames only.

All eleven host tests pass, including a Clang AddressSanitizer/UndefinedBehaviorSanitizer
build. Universal ESP32, ESP32 BLE and ESP8266, both datastream examples, all three
memory probes, embedded test compile targets, and exported SDK builds pass.
The exported SDK was compiled for ESP8266 with both shared IRAM and stock MMU.
Protocol, public-surface, version, stack-usage and linked-image ceiling checks
must pass together; the stock-DRAM gate is only a theoretical prerequisite.

Per the requested code/build-only validation boundary, provisioning, TLS
handshake peaks, sustained traffic, long reconnect stress, OTA interruption,
and power-loss/fragmentation acceptance remain unverified. No additional device
operations are part of these checks. Once hardware testing is requested,
measure free heap, largest block, fragmentation and stack headroom throughout
all those workloads with the custom application reservation retained. At least
4 KiB must remain at workload peaks. Increase the reservation in 4 KiB increments
only after the 8 KiB target passes; publish only a measured allowance.

The universal ESP8266 example emits periodic loop samples.
These samples do not observe allocations inside a blocking TLS call and must
not be described as the absolute handshake low-water mark.
