#include <assert.h>
#include <string.h>
#include <FlovaCore.h>
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
  void poll() override {}
  void setReceiver(flova::MessageReceiver callback, void* value) override { receiver = callback; context = value; }
  void inject(const flova::Message& message) { receiver(context, message); }
};
class TestStorage : public flova::Storage { public: bool read(const char*, void*, size_t) override { return false; } bool write(const char*, const void*, size_t) override { return true; } bool remove(const char*) override { return true; } flova::StorageCapabilities capabilities() const override { flova::StorageCapabilities value; value.usableBytes = 65536; value.availableBytes = 65536; value.persistent = true; return value; } };
class TestClock : public flova::Clock { public: uint64_t now = 1, utc = 0; uint64_t milliseconds() const override { return now; } bool utcValid() const override { return utc != 0; } uint64_t utcMilliseconds() const override { return utc + now; } void setUtc(uint64_t value, uint64_t) override { utc = value - now; } };
class TestLogger : public flova::Logger { public: void log(const char*) override {} };

static int writes;
static bool reject;
static flova::WriteResult writeRelay(bool) { writes++; return reject ? flova::WriteResult::reject("safety_lock") : flova::WriteResult::accept(); }
static flova::ReadResult<float> readTemperature() { return flova::ReadResult<float>::success(31.5f); }

class SessionStorage : public flova::Storage { public: flova::ProvisioningSession session; bool present = false; bool read(const char* key, void* out, size_t size) override { if (!present || strcmp(key, "session") || size != sizeof(session)) return false; memcpy(out, &session, size); return true; } bool write(const char* key, const void* value, size_t size) override { if (!strcmp(key, "session") && size == sizeof(session)) { memcpy(&session, value, size); present = true; } return true; } bool remove(const char*) override { return true; } };
class TestEngine : public flova::EngineClient { public: bool beginRedeem(const flova::ProvisioningRequest&) override { return true; } flova::ProvisioningPoll poll(flova::ProvisioningSession& out) override { out.schemaVersion = 1; flova::Value::copy(out.deviceId, "device-1"); flova::Value::copy(out.host, "mqtt.example"); flova::Value::copy(out.username, "user"); flova::Value::copy(out.password, "secret"); out.port = 8883; out.serverUtcMs = 100000; return flova::ProvisioningPoll::Complete; } };
class TestWallClock : public flova::WallClock { public: bool valid = false; flova::LocalTime now = {2026, 7, 15, 3, 20, 0}; bool synchronized() const override { return valid; } bool localTime(const char*, flova::LocalTime& out) const override { out = now; return true; } };
static int scheduledRuns;
static void scheduledWrite() { scheduledRuns++; }
static int manifestRuns, renewals;
static flova::WriteResult applySchedule(const char*, const flova::ScheduleAction&, uint64_t) { manifestRuns++; return flova::WriteResult::accept(); }
static void renewSchedule(uint32_t, uint64_t) { renewals++; }
static void scheduleStatus(const char*, uint32_t, uint64_t) {}

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
  relay.onWrite(writeRelay); temperature.mode(flova::Mode::Sample).onRead(readTemperature); assert(device.begin());

  assert(!temperature.hasValue() && temperature.read() == 0.0f);
  assert(temperature.refresh().ok && temperature.read() == 31.5f);
  assert(relay.write(false).accepted()); reject = true; assert(!relay.write(true).accepted() && relay.read() == false);

  reject = false; flova::Message command; command.kind = flova::MessageKind::WriteRequest; flova::Value::copy(command.key, "relay"); flova::Value::copy(command.commandId, "cmd-1"); command.value = flova::Value::from(true); command.revision = 1; command.origin = flova::Origin::UserCommand;
  int before = writes; link.inject(command); assert(writes == before + 1 && relay.read()); assert(link.last.kind == flova::MessageKind::Acknowledgement);
  link.inject(command); assert(writes == before + 1);  // stale retry is acknowledged without hardware.
  command.revision = 0; link.inject(command); assert(writes == before + 1);  // command ID also deduplicates imperative writes.
  flova::Value::copy(command.key, "unknown"); flova::Value::copy(command.commandId, "cmd-2"); link.inject(command); assert(link.last.kind == flova::MessageKind::Error && strcmp(link.last.reason, "unknown_datastream") == 0);

  link.online = false; assert(relay.write(false).accepted()); assert(relay.write(true).accepted()); int sends = link.sends;
  link.online = true; device.run(); assert(link.sends == sends + 2 && link.last.value.scalar.boolean);  // time request, then dirty state

  flova::HistoryRetentionPolicy retention; retention.maximumRecords = 2;
  temperature.offline(flova::OfflinePolicy::StoreHistory).retention(retention); link.online = false; temperature.report(5.0f); temperature.report(10.0f); temperature.report(20.0f); assert(device.diagnostics().droppedHistory == 1); int historical = link.stateSends; link.online = true; device.run(); assert(link.stateSends == historical + 2 && link.stateValues[historical] == 10.0 && link.stateValues[historical + 1] == 20.0);
  flova::Message time; time.kind = flova::MessageKind::TimeResponse; time.timestamp = 200000; flova::Value::copy(time.commandId, "time-1"); link.inject(time); assert(clock.utcValid());

  SessionStorage sessionStorage; TestEngine engine; flova::Provisioner provisioner(engine, sessionStorage, clock); flova::ProvisioningRequest request; flova::Value::copy(request.engineUrl, "https://engine.example"); flova::Value::copy(request.token, "one-time-token"); flova::Value::copy(request.hardwareId, "stm32-123"); assert(provisioner.begin(request)); provisioner.run(); flova::ProvisioningSession restored; assert(provisioner.status() == flova::ProvisioningStatus::Ready && provisioner.load(restored) && !strcmp(restored.deviceId, "device-1") && clock.utcValid());
  TestWallClock wallClock; flova::Scheduler scheduler(wallClock); assert(scheduler.daily(20, 0, "IRST-3:30", scheduledWrite)); scheduler.run(); assert(scheduledRuns == 0); wallClock.valid = true; scheduler.run(); scheduler.run(); assert(scheduledRuns == 1); wallClock.now.day++; scheduler.run(); assert(scheduledRuns == 2);
  flova::ScheduleRuntime scheduleRuntime(storage, clock); scheduleRuntime.handlers(applySchedule, renewSchedule, scheduleStatus); flova::ScheduleManifest manifest; manifest.revision = 1; manifest.generatedAt = 1000; manifest.validUntil = 1000 + 90ULL * 24 * 60 * 60 * 1000; manifest.renewBefore = manifest.validUntil - 14ULL * 24 * 60 * 60 * 1000; manifest.scheduleCount = 1; flova::Value::copy(manifest.schedules[0].id, "light-off"); flova::Value::copy(manifest.schedules[0].action.key, "relay"); manifest.schedules[0].action.value = flova::Value::from(false); manifest.schedules[0].enabled = true; manifest.schedules[0].occurrenceCount = 1; manifest.schedules[0].occurrences[0] = 2000; manifest.checksum = flova::ScheduleRuntime::checksum(manifest); assert(scheduleRuntime.install(manifest)); clock.utc = 2500 - clock.now; scheduleRuntime.run(); scheduleRuntime.run(); assert(manifestRuns == 1); clock.utc = manifest.validUntil - 10ULL * 24 * 60 * 60 * 1000 - clock.now; scheduleRuntime.run(); assert(renewals == 1);
  return 0;
}
