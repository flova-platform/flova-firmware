# Clock synchronization and offline data

`flova::Clock` always supplies monotonic milliseconds. A platform RTC, NTP, GPS, cellular network, or PLC clock may additionally seed UTC through `setUtc()`. Once connected, `flova::Device` requests Engine time and refreshes it every six hours. The clock receives the server epoch plus a round-trip uncertainty estimate; failed requests time out after 30 seconds and increment diagnostics.

Device Link maps `TimeRequest` and `TimeResponse` to binary message types. Other links carry the same structured messages through their gateway. If UTC is unavailable, offline records retain monotonic time and publish UTC as zero so Engine can use receive time without mistaking it for trusted device time.

Offline policies:

- `KeepLatest`: cache one dirty state and publish it after reconnect.
- `StoreHistory`: retain 32 ordered telemetry/event records by default; override `FLOVA_HISTORY_CAPACITY` at compile time.
- `Drop`: update local cache without retaining a network publication.
- `Reject`: reject operations whose delivery requirement cannot be met offline.

History is disabled until Engine supplies a byte budget. The portable core uses
`ResourceManager` to protect feature reservations and allows elastic borrowing
only when it cannot consume another feature's guarantee. Retention can bound
bytes, records, age, and sampling interval; overflow drops oldest by default or
newest when explicitly configured. Reconnect synchronizes time, publishes dirty
state, then removes history oldest-first only after transport acceptance. Cloud
commands are never stored or replayed. After a heartbeat uptime reset, Engine
may re-dispatch the latest durable desired value for state datastreams as
reconciliation; one-shot command datastreams are not replayed.

Storage adapters report usable application bytes after credentials, filesystem
metadata, and safety headroom. Raw flash size is never treated as available
history capacity. Universal wrappers advertise zero history bytes until their
durable history adapter is fully wired.
