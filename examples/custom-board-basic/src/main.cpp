#include <FlovaCore.h>

// A board port only supplies these four small services. They may wrap STM32
// HAL, FreeRTOS, a PLC SDK, Ethernet, cellular, BLE, LoRaWAN, or a gateway.
class BoardLink : public flova::Link {
 public:
  bool begin() override { return true; }
  bool connected() const override { return online_; }
  bool send(const flova::Message&) override { return online_; }
  void poll() override {}
  void setReceiver(flova::MessageReceiver receiver, void* context) override { receiver_ = receiver; context_ = context; }
 private:
  bool online_ = true;
  flova::MessageReceiver receiver_ = 0;
  void* context_ = 0;
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

BoardLink link;
BoardStorage storage;
BoardClock clockSource;
BoardLogger logger;
flova::Device flovaDevice(link, storage, clockSource, logger);
flova::Datastream<bool> relay = flovaDevice.datastream<bool>("relay");

static flova::WriteResult setRelay(bool enabled) {
  // Replace with the board's safe hardware operation.
  (void)enabled;
  return flova::WriteResult::accept();
}

int main() {
  // Board ports choose capacities in their build profile; Engine later clamps
  // them to deployment policy. They are physical facts, not product limits.
  flova::ResourceBudget budgets[static_cast<size_t>(flova::ResourceKind::Count)];
  budgets[static_cast<size_t>(flova::ResourceKind::History)].reservedBytes = 4096;
  budgets[static_cast<size_t>(flova::ResourceKind::History)].maximumBytes = 8192;
  budgets[static_cast<size_t>(flova::ResourceKind::History)].elastic = true;
  flovaDevice.resourcePlan(budgets, static_cast<size_t>(flova::ResourceKind::Count));
  relay.onWrite(setRelay).offline(flova::OfflinePolicy::KeepLatest);
  flovaDevice.begin();
  relay.write(true);
  for (;;) flovaDevice.run();
}
