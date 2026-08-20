#include <assert.h>
#include <string.h>
#include <FlovaDevice.h>
#include <FlovaFactoryResetGesture.h>
#include <FlovaProvisioning.h>
#include <FlovaScheduler.h>
#include <FlovaScheduleRuntime.h>

class TestLink : public flova::Link {
 public:
  bool online = true; int sends = 0, stateSends = 0; double stateValues[64]; flova::Message last; flova::MessageReceiver receiver = 0; void* context = 0;
  bool begin() override { return true; }
  bool connected() const override { return online; }
  bool send(const flova::Message& message) override { sends++; last = message; if (message.kind == flova::MessageKind::StateUpdate) { if (message.value.type == flova::ValueType::Float) stateValues[stateSends] = message.value.scalar.floating; stateSends++; } return online; }
  bool bindDatastreams(const char* const*, size_t count, DatastreamId* ids) override { boundCount = count; for (size_t i = 0; i < count; ++i) ids[i] = static_cast<DatastreamId>(i + 1); return count <= FLOVA_MAX_ACTIVE_DATASTREAMS; }
  void poll() override {}
  void setReceiver(flova::MessageReceiver callback, void* value) override { receiver = callback; context = value; }
  uint32_t messageNonce() const override { return 0x12345678UL; }
  void inject(const flova::Message& message) { receiver(context, message); }
  void acknowledge(uint64_t messageId) {
    flova::Message message;
    message.kind = flova::MessageKind::Acknowledgement;
    message.messageId = messageId;
    inject(message);
  }
  size_t boundCount = 0;
};
class InvalidBindingLink : public TestLink { public: bool bindDatastreams(const char* const*, size_t count, DatastreamId* ids) override { if (count) ids[0] = FLOVA_INVALID_DATASTREAM_ID; return true; } };
class DuplicateBindingLink : public TestLink { public: bool bindDatastreams(const char* const*, size_t count, DatastreamId* ids) override { for (size_t i = 0; i < count; ++i) ids[i] = 1; return true; } };
class AsyncBindingLink : public TestLink {
 public:
  bool bindDatastreams(const char* const*, size_t count, DatastreamId* ids) override {
    boundCount = count;
    for (size_t i = 0; i < count; ++i) { ids[i] = FLOVA_INVALID_DATASTREAM_ID; pendingIds[i] = static_cast<DatastreamId>(i + 10); }
    return count <= FLOVA_MAX_ACTIVE_DATASTREAMS;
  }
  bool bindingReady() const override { return ready; }
  bool readDatastreamBinding(DatastreamId* ids, size_t count) const override {
    if (!ready || count != boundCount) return false;
    for (size_t i = 0; i < count; ++i) ids[i] = pendingIds[i];
    return true;
  }
  void poll() override { ready = true; }
  bool ready = false;
  DatastreamId pendingIds[FLOVA_MAX_ACTIVE_DATASTREAMS] = {};
};
class TestStorage : public flova::Storage { public: bool read(const char*, void*, size_t) override { return false; } bool write(const char*, const void*, size_t) override { return true; } bool remove(const char*) override { return true; } flova::StorageCapabilities capabilities() const override { flova::StorageCapabilities value; value.usableBytes = 65536; value.availableBytes = 65536; value.persistent = true; return value; } };
class TestClock : public flova::Clock { public: uint64_t now = 1, utc = 0; uint64_t milliseconds() const override { return now; } bool utcValid() const override { return utc != 0; } uint64_t utcMilliseconds() const override { return utc + now; } void setUtc(uint64_t value, uint64_t) override { utc = value - now; } };
class TestLogger : public flova::Logger { public: void log(const char*) override {} };

static int writes;
static int numericWrites;
static int voidWrites;
static bool reject;
static bool noChange;
static flova::WriteResult writeRelay(bool) { writes++; return reject ? flova::WriteResult::reject("safety_lock") : noChange ? flova::WriteResult::noChange() : flova::WriteResult::accept(); }
static flova::WriteResult writeSetpoint(double) { numericWrites++; return flova::WriteResult::accept(); }
static void writeCounter(int64_t) { voidWrites++; }

class SessionStorage : public flova::Storage { public: flova::ProvisioningSession session; bool present = false; bool read(const char* key, void* out, size_t size) override { if (!present || strcmp(key, "session") || size != sizeof(session)) return false; memcpy(out, &session, size); return true; } bool write(const char* key, const void* value, size_t size) override { if (!strcmp(key, "session") && size == sizeof(session)) { memcpy(&session, value, size); present = true; } return true; } bool remove(const char*) override { return true; } };
class TestEngine : public flova::LinkBootstrapClient { public: bool beginBootstrap(const flova::ProvisioningRequest&) override { return true; } flova::ProvisioningPoll poll(flova::ProvisioningSession& out) override { out.schemaVersion = 1; flova::Value::copy(out.deviceId, "device-1"); flova::Value::copy(out.linkUrl, "wss://link.example/api/device-link"); flova::Value::copy(out.secret, "device-secret"); out.serverUtcMs = 100000; return flova::ProvisioningPoll::Complete; } };
class TestWallClock : public flova::WallClock { public: bool valid = false; flova::LocalTime now = {2026, 7, 15, 3, 20, 0}; bool synchronized() const override { return valid; } bool localTime(const char*, flova::LocalTime& out) const override { out = now; return true; } };
static int scheduledRuns;
static void scheduledWrite() { scheduledRuns++; }
static int manifestRuns, renewals;
static flova::WriteResult applySchedule(void*, const char*, const flova::ScheduleAction&, uint64_t) { manifestRuns++; return flova::WriteResult::accept(); }
static void renewSchedule(void*, uint32_t, uint64_t) { renewals++; }
static void scheduleStatus(void*, const char*, uint32_t, uint64_t) {}

int main() {
  FlovaFactoryResetGesture resetGesture;
  resetGesture.configure();
  uint32_t gestureNow = 100;
  assert(resetGesture.update(false, gestureNow) == FlovaFactoryResetGesture::None);
  gestureNow += 500;
  assert(resetGesture.update(false, gestureNow) == FlovaFactoryResetGesture::Armed);
  for (int tap = 0; tap < 3; tap++) {
    gestureNow += 10; resetGesture.update(true, gestureNow);
    gestureNow += 50; resetGesture.update(true, gestureNow);
    gestureNow += 10; resetGesture.update(false, gestureNow);
    gestureNow += 50;
    assert(resetGesture.update(false, gestureNow) == FlovaFactoryResetGesture::TapAccepted);
  }
  gestureNow += 10; resetGesture.update(true, gestureNow);
  gestureNow += 50;
  assert(resetGesture.update(true, gestureNow) == FlovaFactoryResetGesture::HoldStarted);
  gestureNow += 10000;
  assert(resetGesture.update(true, gestureNow) == FlovaFactoryResetGesture::ReleaseRequested);
  gestureNow += 10; resetGesture.update(false, gestureNow);
  gestureNow += 50;
  assert(resetGesture.update(false, gestureNow) == FlovaFactoryResetGesture::Confirmed);

  FlovaFactoryResetGesture stuckGesture;
  stuckGesture.configure();
  assert(stuckGesture.update(true, 100) == FlovaFactoryResetGesture::None);
  assert(stuckGesture.update(true, 60100) == FlovaFactoryResetGesture::WindowClosed);

  FlovaFactoryResetGesture wrapGesture;
  wrapGesture.configure();
  uint32_t wrapNow = 0xFFFFFF00UL;
  wrapGesture.update(false, wrapNow);
  wrapNow += 500;
  assert(wrapGesture.update(false, wrapNow) == FlovaFactoryResetGesture::Armed);

  flova::ResourceManager resourceTest;
  flova::ResourceBudget resourceBudgets[static_cast<size_t>(flova::ResourceKind::Count)];
  resourceBudgets[static_cast<size_t>(flova::ResourceKind::History)].maximumBytes = 100;
  resourceBudgets[static_cast<size_t>(flova::ResourceKind::History)].elastic = true;
  resourceBudgets[static_cast<size_t>(flova::ResourceKind::Schedules)].reservedBytes = 60;
  resourceBudgets[static_cast<size_t>(flova::ResourceKind::Schedules)].maximumBytes = 60;
  resourceTest.configure(100, resourceBudgets, static_cast<size_t>(flova::ResourceKind::Count));
  assert(!resourceTest.reserve(flova::ResourceKind::History, 50));
  assert(resourceTest.reserve(flova::ResourceKind::History, 40));
  assert(resourceTest.reserve(flova::ResourceKind::Schedules, 60));

  TestLink link; TestStorage storage; TestClock clock; TestLogger logger; flova::Device device(link, storage, clock, logger);
  flova::ResourceBudget budgets[static_cast<size_t>(flova::ResourceKind::Count)];
  budgets[static_cast<size_t>(flova::ResourceKind::History)].reservedBytes = 4096;
  budgets[static_cast<size_t>(flova::ResourceKind::History)].maximumBytes = 4096;
  device.resourcePlan(budgets, static_cast<size_t>(flova::ResourceKind::Count));
  flova::Datastream<bool> relay = device.datastream<bool>("relay");
  flova::Datastream<float> temperature = device.datastream<float>("temperature");
  flova::Datastream<double> setpoint = device.datastream<double>("setpoint");
  flova::Datastream<int64_t> counter = device.datastream<int64_t>("counter");
  flova::Datastream<flova::Text> status = device.datastream<flova::Text>("status");
  setpoint.onWrite(writeSetpoint);
  counter.onWrite(writeCounter);
  relay.onWrite(writeRelay);
  assert(!relay.bound());
  assert(temperature.report(31.5f).accepted() && link.sends == 0);
  assert(device.begin());
  assert(relay.bound() && counter.bound());

  assert(temperature.hasValue() && temperature.value() == 31.5f);
  assert(counter.write(INT64_MAX).accepted() && counter.value() == INT64_MAX && voidWrites == 1);
  assert(status.report("ready").accepted() && !strcmp(status.value().c_str(), "ready"));
  char longText[flova::kMaxText + 8]; memset(longText, 'x', sizeof(longText)); longText[sizeof(longText) - 1] = 0;
  assert(!status.report(longText).accepted());
  assert(relay.write(false).accepted()); reject = true; assert(!relay.write(true).accepted() && relay.value() == false);
  reject = false; noChange = true; assert(relay.write(true).accepted() && relay.value()); noChange = false; assert(relay.write(false).accepted());

  flova::Message command; command.kind = flova::MessageKind::WriteRequest; command.datastreamId = 1; flova::Value::copy(command.commandId, "cmd-1"); command.value = flova::Value::from(true); command.revision = 1; command.origin = flova::Origin::UserCommand;
  int before = writes; link.inject(command); assert(writes == before + 1 && relay.value()); assert(link.last.kind == flova::MessageKind::Acknowledgement);
  link.inject(command); assert(writes == before + 1);  // stale retry is acknowledged without hardware.
  command.revision = 0; link.inject(command); assert(writes == before + 1);  // command ID also deduplicates imperative writes.
  command.datastreamId = 99; flova::Value::copy(command.commandId, "cmd-2"); link.inject(command); assert(link.last.kind == flova::MessageKind::Error && strcmp(link.last.reason, "unknown_datastream") == 0);
  command.datastreamId = 1; command.revision = 2; command.value = flova::Value::from(1.0f); flova::Value::copy(command.commandId, "cmd-wrong-type"); link.inject(command); assert(writes == before + 1 && link.last.kind == flova::MessageKind::Error && strcmp(link.last.reason, "type_mismatch") == 0);

  link.online = false; relay.offline(flova::OfflinePolicy::Reject); assert(!relay.write(false).accepted()); assert(!relay.write(true).accepted()); int sends = link.sends;
  assert(relay.value());
  link.online = true; device.run(); assert(link.sends > sends);
  link.acknowledge(link.last.messageId);

  flova::HistoryRetentionPolicy retention; retention.maximumRecords = 2;
  temperature.offline(flova::OfflinePolicy::StoreHistory).retention(retention); link.online = false; temperature.report(5.0f); temperature.report(10.0f); temperature.report(20.0f); assert(device.diagnostics().droppedHistory == 1); int historical = link.stateSends; link.online = true; device.run(); assert(link.stateSends == historical + 1 && link.stateValues[historical] == 10.0); link.acknowledge(link.last.messageId); device.run(); assert(link.stateSends == historical + 2 && link.stateValues[historical + 1] == 20.0); link.acknowledge(link.last.messageId);
  flova::Message time; time.kind = flova::MessageKind::TimeResponse; time.timestamp = 200000; flova::Value::copy(time.commandId, "time-1"); link.inject(time); assert(clock.utcValid());

  TestLink maximumLink; TestStorage maximumStorage; TestClock maximumClock; TestLogger maximumLogger;
  flova::Device maximumDevice(maximumLink, maximumStorage, maximumClock, maximumLogger);
  char maximumKeys[FLOVA_MAX_ACTIVE_DATASTREAMS][8] = {};
  for (size_t i = 0; i < FLOVA_MAX_ACTIVE_DATASTREAMS; ++i) { snprintf(maximumKeys[i], sizeof(maximumKeys[i]), "d%u", static_cast<unsigned>(i)); maximumDevice.datastream<bool>(maximumKeys[i]); }
  assert(maximumDevice.begin() && maximumLink.boundCount == FLOVA_MAX_ACTIVE_DATASTREAMS);
  DatastreamId overLimitIds[FLOVA_MAX_ACTIVE_DATASTREAMS + 1] = {};
  assert(!maximumLink.bindDatastreams(nullptr, FLOVA_MAX_ACTIVE_DATASTREAMS + 1, overLimitIds));
  InvalidBindingLink invalidLink; flova::Device invalidDevice(invalidLink, maximumStorage, maximumClock, maximumLogger); invalidDevice.datastream<bool>("invalid"); assert(!invalidDevice.begin());
  DuplicateBindingLink duplicateLink; flova::Device duplicateDevice(duplicateLink, maximumStorage, maximumClock, maximumLogger); duplicateDevice.datastream<bool>("duplicate"); duplicateDevice.datastream<bool>("duplicate-two"); assert(!duplicateDevice.begin());
  AsyncBindingLink asyncLink; flova::Device asyncDevice(asyncLink, maximumStorage, maximumClock, maximumLogger); flova::Datastream<bool> asyncDatastream = asyncDevice.datastream<bool>("async"); asyncDatastream.onWrite(writeRelay); assert(asyncDevice.begin() && !asyncDevice.ready()); asyncDevice.run(); assert(asyncDevice.ready() && asyncDatastream.write(true).accepted());

  TestLink defaultHistoryLink; TestStorage defaultHistoryStorage;
  TestClock defaultHistoryClock; TestLogger defaultHistoryLogger;
  flova::Device defaultHistoryDevice(defaultHistoryLink, defaultHistoryStorage,
                                     defaultHistoryClock,
                                     defaultHistoryLogger);
  flova::Datastream<float> defaultHistory =
      defaultHistoryDevice.datastream<float>("default-history");
  defaultHistory.offline(flova::OfflinePolicy::StoreHistory);
  assert(defaultHistoryDevice.begin());
  defaultHistoryLink.online = false;
  assert(defaultHistory.report(42.0f).accepted());
  assert(defaultHistoryDevice.diagnostics().droppedHistory == 0);
  assert(defaultHistoryDevice.diagnostics().storageFailures == 0);

  flova::config::Unit configuredDatastream;
  configuredDatastream.kind = flova::config::UnitKind::Datastream;
  configuredDatastream.data.datastream.id = 9;
  configuredDatastream.data.datastream.valueType = 3;
  flova::Value::copy(configuredDatastream.data.datastream.key, "setpoint");
  assert(device.applyConfigurationUnit(configuredDatastream));
  flova::config::Unit safety;
  safety.kind = flova::config::UnitKind::Safety;
  safety.data.safety.datastreamId = 9;
  safety.data.safety.policy = flova::config::SafetyPolicy::Range;
  safety.data.safety.hasMinimum = true; safety.data.safety.minimum.kind = flova::config::ValueKind::Float64; safety.data.safety.minimum.data.float64 = 10;
  safety.data.safety.hasMaximum = true; safety.data.safety.maximum.kind = flova::config::ValueKind::Float64; safety.data.safety.maximum.data.float64 = 20;
  assert(device.applyConfigurationUnit(safety));
  assert(!setpoint.write(5).accepted() && numericWrites == 0);
  assert(setpoint.write(15).accepted() && numericWrites == 1);
  flova::Message expired = command; expired.datastreamId = 1; expired.value = flova::Value::from(true); expired.expiresAtUtcMs = clock.utcMilliseconds() - 1; flova::Value::copy(expired.commandId, "cmd-expired"); link.inject(expired); assert(link.last.kind == flova::MessageKind::Error && strcmp(link.last.reason, "command_expired") == 0);

  SessionStorage sessionStorage; TestEngine engine; flova::Provisioner provisioner(engine, sessionStorage, clock); flova::ProvisioningRequest request; flova::Value::copy(request.engineUrl, "https://engine.example"); flova::Value::copy(request.token, "one-time-token"); flova::Value::copy(request.hardwareId, "stm32-123"); assert(provisioner.begin(request)); provisioner.run(); flova::ProvisioningSession restored; assert(provisioner.status() == flova::ProvisioningStatus::Ready && provisioner.load(restored) && !strcmp(restored.deviceId, "device-1") && clock.utcValid());
  TestWallClock wallClock; flova::Scheduler scheduler(wallClock); assert(scheduler.daily(20, 0, "IRST-3:30", scheduledWrite)); scheduler.run(); assert(scheduledRuns == 0); wallClock.valid = true; scheduler.run(); scheduler.run(); assert(scheduledRuns == 1); wallClock.now.day++; scheduler.run(); assert(scheduledRuns == 2);
  flova::ScheduleRuntime scheduleRuntime(storage, clock); scheduleRuntime.handlers(applySchedule, renewSchedule, scheduleStatus, 0); flova::ScheduleManifest manifest; manifest.revision = 1; manifest.generatedAt = 1000; manifest.validUntil = 1000 + 90ULL * 24 * 60 * 60 * 1000; manifest.renewBefore = manifest.validUntil - 14ULL * 24 * 60 * 60 * 1000; manifest.scheduleCount = 1; flova::Value::copy(manifest.schedules[0].id, "light-off"); manifest.schedules[0].action.datastreamId = 1; manifest.schedules[0].action.value = flova::Value::from(false); manifest.schedules[0].enabled = true; manifest.schedules[0].occurrenceCount = 1; manifest.schedules[0].occurrences[0] = 2000; manifest.checksum = flova::ScheduleRuntime::checksum(manifest); assert(scheduleRuntime.install(manifest)); clock.utc = 2500 - clock.now; scheduleRuntime.run(); scheduleRuntime.run(); assert(manifestRuns == 1); clock.utc = manifest.validUntil - 10ULL * 24 * 60 * 60 * 1000 - clock.now; scheduleRuntime.run(); assert(renewals == 1);
  flova::ScheduleManifest compiled;
  flova::ScheduleChunkCompiler compiler(compiled);
  assert(compiler.begin(2, 1000, 9000, 8000, 1));
  flova::config::Unit scheduleUnit = {}; scheduleUnit.kind = flova::config::UnitKind::Schedule; scheduleUnit.data.schedule.id = 42; scheduleUnit.data.schedule.enabled = true; scheduleUnit.data.schedule.actionCount = 1; scheduleUnit.data.schedule.actions[0].offsetMs = 500; scheduleUnit.data.schedule.actions[0].datastreamId = 1; scheduleUnit.data.schedule.actions[0].value.kind = flova::config::ValueKind::Boolean; scheduleUnit.data.schedule.actions[0].value.data.boolean = false;
  assert(compiler.addSchedule(scheduleUnit.data.schedule));
  flova::config::Unit chunk = {}; chunk.kind = flova::config::UnitKind::ScheduleOccurrences; chunk.data.occurrences.scheduleId = 42; chunk.data.occurrences.chunkIndex = 0; chunk.data.occurrences.chunkCount = 2; chunk.data.occurrences.occurrenceCount = 1; chunk.data.occurrences.occurrences[0] = 3000; assert(compiler.addOccurrences(chunk.data.occurrences));
  chunk.data.occurrences.chunkIndex = 1; chunk.data.occurrences.occurrences[0] = 4000; assert(compiler.addOccurrences(chunk.data.occurrences));
  assert(compiler.finish()); assert(compiled.schedules[0].occurrenceCount == 2 && compiled.schedules[0].actionCount == 1 && compiled.schedules[0].actions[0].offsetMs == 500);
  return 0;
}
