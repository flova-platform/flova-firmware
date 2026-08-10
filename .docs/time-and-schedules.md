# Time and schedules

A board must not advertise offline schedule slots merely because the core was
compiled with schedule arrays. Its board profile enables
`FLOVA_SCHEDULE_RUNTIME_ENABLED` only after Device Link manifest delivery, durable
manifest storage, clock synchronization, and `ScheduleRuntime::run()` are all
wired. Otherwise Engine keeps schedules cloud-only.

Flova stores time as UTC. Engine time synchronization is the default after a
device connects; NTP, an RTC, GPS, cellular time, or a host processor may be
used as board-specific bootstrap sources. A phone provides an IANA timezone
preference, never the authoritative device clock.

Engine owns end-user schedules and developer cloud defaults. It compiles IANA
timezone rules into a renewable 90-day UTC occurrence manifest. The device
stores that bounded cache, executes it offline through the normal datastream
write pipeline, requests renewal with 14 days remaining, and never replays
missed occurrences. An expired cache stops safely until reconnection.

The universal ESP32 and ESP8266 targets receive schedule metadata and ordered
occurrence chunks as bounded integer-keyed `CONFIG_RECORD` units. Metadata and
chunks are independently persisted through the same `CONFIG_BEGIN` /
`CONFIG_END` transaction as every other configuration record. Each occurrence
chunk carries at most 16 UTC millisecond values; revisions and UTC timestamps
are 64-bit. The targets accept at most eight schedules and 128 occurrences per
schedule, while every individual Flova Link frame remains at most 512 bytes.

ESP32 stores the staging/active manifest in Preferences. ESP8266 stores it in
LittleFS because the existing 4 KiB EEPROM layout is reserved for credentials,
runtime configuration, and datastream snapshots. Installation validates the
the bounded compiler validates chunk order and completeness before installing
the active manifest. Progress is persisted before the hardware write;
occurrences missed while powered down are advanced without replay. All due
writes use the existing datastream handler and safety path.
The ESP8266 restore path keeps manifests in bounded fixed-size values and parses directly into
the authoritative bounded schedule array; manifest-sized buffers and duplicate
schedule arrays must never be placed on its constrained stack. A corrupt or
oversized retained manifest is discarded as a replaceable cache so Device Link startup can
continue and Engine can deliver a fresh copy.

Developer-local schedules use `flova::Scheduler` with a board-supplied
`flova::WallClock`. The adapter converts synchronized UTC into local calendar
fields using a developer-supplied POSIX TZ rule. The scheduler is fixed at
eight entries, runs only with a valid clock, and executes once per matching
local date. The callback should normally call a datastream `write()` so local
hardware safety and cloud synchronization remain unified.

```cpp
flova::Scheduler schedules(wallClock);

void turnLightOff() { light.write(false); }

void setup() {
  schedules.daily(20, 0, "EST5EDT,M3.2.0/2,M11.1.0/2", turnLightOff);
}

void loop() {
  device.run();
  schedules.run();
}
```
