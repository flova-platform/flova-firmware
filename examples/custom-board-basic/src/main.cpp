#include <FlovaCore.h>

// Portable custom-board contract. Replace the four services below with your
// MCU HAL, RTOS, PLC SDK, Ethernet/cellular modem, or gateway transport.

// A board port only supplies these four small services. They may wrap STM32
// HAL, FreeRTOS, a PLC SDK, Ethernet, cellular, BLE, LoRaWAN, or a gateway.
class BoardLink : public flova::Link {
 public:
  explicit BoardLink(uint32_t bootNonce) : bootNonce_(bootNonce) {}
  bool begin() override { return true; }
  bool connected() const override { return online_; }
  bool send(const flova::Message&) override { return online_; }
  void poll() override {}
  void setReceiver(flova::MessageReceiver receiver, void* context) override { receiver_ = receiver; context_ = context; }
  uint32_t messageNonce() const override { return bootNonce_; }
  bool bindDatastreams(const char* const* keys, size_t count, DatastreamId* ids) override {
    // A real server-backed Link resolves every declared key to a stable
    // numeric ID. This compile example supports one stream and assigns ID 1.
    if (!keys || !ids || count > 1 || (count && !keys[0])) return false;
    if (count) ids[0] = 1;
    return true;
  }
 private:
  bool online_ = true;
  flova::MessageReceiver receiver_ = 0;
  void* context_ = 0;
  uint32_t bootNonce_;
};

class BoardStorage : public flova::Storage {
 public:
  bool read(const char*, void*, size_t) override { return false; }
  bool write(const char*, const void*, size_t) override { return true; }
  bool remove(const char*) override { return true; }
  flova::StorageCapabilities capabilities() const override {
    flova::StorageCapabilities value;
    value.usableBytes = 32 * 1024;  // Excludes credentials and filesystem reserve.
    value.availableBytes = value.usableBytes;
    value.maxRecordBytes = 1024;
    value.eraseBlockBytes = 4096;
    value.writeGranularity = 4;
    value.persistent = true;
    value.wearSensitive = true;
    return value;
  }
};

class BoardClock : public flova::Clock { public: uint64_t milliseconds() const override { return 0; } };
class BoardLogger : public flova::Logger { public: void log(const char*) override {} };

// A production board supplies a fresh non-zero hardware-random value on every
// boot. This fixed value keeps the host compile contract deterministic.
BoardLink link(0x43555354UL);
BoardStorage storage;
BoardClock clockSource;
BoardLogger logger;
flova::Device flovaDevice(link, storage, clockSource, logger);
flova::Datastream<bool> relay = flovaDevice.datastream<bool>("relay");

static flova::WriteResult setRelay(void* context, bool enabled) {
  // This is the command path for local writes, user commands, schedules, and
  // cloud automations. Replace it with the board's safe hardware operation.
  (void)context;
  (void)enabled;
  return flova::WriteResult::accept();
}

int main() {
  // Budgets describe physical capacity. They bound history and make storage
  // pressure deterministic instead of allowing an unbounded backlog.
  // Board ports choose capacities in their build profile; Engine later clamps
  // them to deployment policy. They are physical facts, not product limits.
  flova::ResourceBudget budgets[static_cast<size_t>(flova::ResourceKind::Count)];
  budgets[static_cast<size_t>(flova::ResourceKind::History)].reservedBytes = 4096;
  budgets[static_cast<size_t>(flova::ResourceKind::History)].maximumBytes = 8192;
  budgets[static_cast<size_t>(flova::ResourceKind::History)].elastic = true;
  flovaDevice.resourcePlan(budgets, static_cast<size_t>(flova::ResourceKind::Count));
  // onWrite() receives commands. report() would be used for sensor readings
  // or hardware changes that happened outside this callback.
  relay.onWrite(setRelay, nullptr).offline(flova::OfflinePolicy::KeepLatest);

  // begin() binds the human-readable key to the transport's numeric ID,
  // restores bounded persistent state, and starts the supplied Link.
  if (!flovaDevice.begin()) return 1;

  // This is a local application write. Engine automation can issue an
  // equivalent remote write and will use setRelay() above.
  relay.write(true);

  // Device::run() is the ownership boundary where queued transport work is
  // applied. Never perform GPIO writes from a socket callback.
  for (;;) flovaDevice.run();
}
