#define F(value) value
#define print println
#include <FlovaArduino.h>
#undef print
#undef F

#include <assert.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

HardwareSerial Serial;
uint32_t millis() { return 1; }
unsigned long micros() { return 1; }
void delay(unsigned long) {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return 0; }
int analogRead(uint8_t) { return 0; }
void analogWrite(uint8_t, int) {}

namespace {

class TestStorage final : public flova::Storage {
 public:
  bool read(const char* key, void* output, size_t length) override {
    auto found = records.find(key);
    if (found == records.end() || found->second.size() != length) return false;
    memcpy(output, found->second.data(), length);
    return true;
  }
  bool write(const char* key, const void* input, size_t length) override {
    const auto* bytes = static_cast<const uint8_t*>(input);
    records[key] = std::vector<uint8_t>(bytes, bytes + length);
    return true;
  }
  bool remove(const char* key) override { records.erase(key); return true; }
  bool clear() override { records.clear(); return true; }
  std::map<std::string, std::vector<uint8_t>> records;
};

class TestClock final : public flova::Clock {
 public:
  uint64_t milliseconds() const override { return 1; }
};

class TestLogger final : public flova::Logger {
 public:
  void log(const char*) override {}
};

class TestEntropy final : public FlovaEntropySource {
 public:
  uint8_t byte() override { return 1; }
};

class TestIdentity final : public FlovaBoardIdentity {
 public:
  bool hardwareId(char* output, size_t capacity) const override {
    static const char id[] = "test-board";
    if (capacity < sizeof(id)) return false;
    memcpy(output, id, sizeof(id));
    return true;
  }
  const char* firmwareTarget() const override { return "test-target"; }
};

class TestNetwork final : public FlovaNetworkRuntime {
 public:
  bool connected() const override { return online; }
  bool online = false;
};

class TestTlsClock final : public FlovaTlsClockBootstrap {
 public:
  bool ready() const override { return clockReady; }
  bool clockReady = false;
};

class TestProvisioning final : public FlovaProvisioningAdapter {
 public:
  bool begin(FlovaProvisioningHandler, void*) override { return true; }
};

class TestHardware final : public flova::Hardware {
 public:
  void attach(flova::Device&) override {}
  bool apply(const flova::config::Unit&) override { return true; }
  void run() override {}
  void setConnected(bool value) override { connected = value; }
  bool connected = false;
};

class TestLink final : public FlovaClientLink {
 public:
  bool begin() override { return true; }
  bool connected() const override { return online; }
  bool send(const flova::Message&) override { return online; }
  void poll() override {}
  void setReceiver(flova::MessageReceiver receiver, void* context) override {
    receiver_ = receiver;
    context_ = context;
  }
  uint32_t messageNonce() const override { return 1; }
  bool configure(const char*, const char*, const char*) override { return true; }
  bool beginBootstrap(const char*, const char*, const char*, const char*,
                      const char*) override { return true; }
  void pollBootstrap() override {}
  bool takeBootstrapCommitted(FlovaLinkBootstrapCommitted&) override {
    return false;
  }
  bool takeBootstrapError(char*, size_t) override { return false; }
  bool takeConfigurationRecord(FlovaLinkConfigurationRecord& output) override {
    if (!configurationPending) return false;
    output = configuration;
    configurationPending = false;
    return true;
  }
  bool publishConfigurationReport(
      const FlovaLinkConfigurationReport& report) override {
    lastReport = report; ++reports; return true;
  }
  bool publishConfigurationState(
      const FlovaLinkConfigurationState&) override { return true; }
  bool publishHeartbeat(const FlovaLinkHeartbeat&) override { return true; }
  bool publishScheduleStatus(const FlovaLinkScheduleStatus&) override {
    return true;
  }
  bool publishScheduleRenew(const FlovaLinkScheduleStatus&) override {
    return true;
  }
  bool takeOtaOffer(FlovaLinkOtaOffer&) override { return false; }
  bool publishOtaReport(const FlovaLinkOtaReport&) override { return true; }
  flova::OtaInstallResult installOta(const FlovaLinkOtaOffer&) override {
    return flova::OtaInstallResult::DownloadFailed;
  }
  bool decodeStoredConfigurationUnit(
      const uint8_t* payload, size_t length, flova::config::Unit& output) override {
    assert(length == 16);
    // Writing the typed alternative must not destroy the installer's copy.
    output = flova::config::Unit();
    for (size_t i = 0; i < length; ++i) assert(payload[i] == 0x5a);
    output.kind = flova::config::UnitKind::Datastream;
    output.data.datastream.id = decoderAllowed ? 1 : 0;
    output.data.datastream.valueType = 0;
    memcpy(output.data.datastream.key, "relay", 6);
    return true;
  }
  void setConfigurationGeneration(uint32_t generation) override {
    generation_ = generation;
  }
  uint32_t configurationGeneration() const override { return generation_; }
  void setHardwareCapabilities(
      const flova::HardwareCapabilities&) override {}
  void disconnect() override { online = false; }

  bool online = false;
  bool configurationPending = false;
  bool decoderAllowed = true;
  unsigned reports = 0;
  FlovaLinkConfigurationRecord configuration = {};
  FlovaLinkConfigurationReport lastReport = {};

 private:
  flova::MessageReceiver receiver_ = nullptr;
  void* context_ = nullptr;
  uint32_t generation_ = 0;
};

struct Observer {
  unsigned count = 0;
  FlovaStatusEventKind kinds[8] = {};
  FlovaStatusSnapshot last = {};
};

void observe(void* context, const FlovaStatusEvent& event) {
  Observer& observer = *static_cast<Observer*>(context);
  assert(observer.count < sizeof(observer.kinds) / sizeof(observer.kinds[0]));
  observer.kinds[observer.count++] = event.kind;
  observer.last = event.current;
}

}  // namespace

int main() {
  TestLink link;
  TestStorage storage;
  TestClock clock;
  TestLogger logger;
  TestEntropy entropy;
  TestIdentity identity;
  TestNetwork network;
  TestTlsClock tlsClock;
  TestProvisioning provisioning;
  TestHardware hardware;
  FlovaClient client(link, provisioning, network, tlsClock, identity, storage,
                     clock, logger, entropy, hardware);
  Observer observer;

  client.setStatusListener(observe, &observer);
  assert(client.begin(false));
  client.run();
  assert(observer.count == 1);
  assert(observer.kinds[0] == FlovaStatusEventKind::LifecycleChanged);
  assert(observer.last.lifecycle == FlovaLifecycle::AwaitingProvisioning);
  assert(!observer.last.linkConnected);
  assert(!observer.last.ready);

  network.online = true;
  tlsClock.clockReady = true;
  link.online = true;
  client.run();
  assert(observer.count == 3);
  assert(observer.kinds[1] == FlovaStatusEventKind::NetworkChanged);
  assert(observer.kinds[2] == FlovaStatusEventKind::LinkChanged);
  assert(observer.last.linkConnected);
  assert(!observer.last.ready);

  client.run();
  assert(observer.count == 3);

  link.setConfigurationGeneration(5);
  client.run();
  assert(observer.count == 4);
  assert(observer.kinds[3] == FlovaStatusEventKind::ConfigurationChanged);
  assert(observer.last.configurationGeneration == 5);

  link.online = false;
  client.run();
  assert(observer.count == 5);
  assert(observer.kinds[4] == FlovaStatusEventKind::LinkChanged);
  assert(!observer.last.linkConnected);

  assert(!client.setFirmwareTarget("invalid target"));
  client.run();
  assert(observer.count == 6);
  assert(observer.kinds[5] == FlovaStatusEventKind::ErrorChanged);
  assert(strcmp(observer.last.errorCode, "invalid_firmware_target") == 0);

  FlovaStatusSnapshot snapshot = {};
  client.status(snapshot);
  assert(snapshot.lifecycle == FlovaLifecycle::AwaitingProvisioning);
  assert(strcmp(snapshot.errorCode, "invalid_firmware_target") == 0);
  // Exercise transfer -> typed workspace reuse through the real lifecycle.
  client.setStatusListener(nullptr);
  assert(client.provision(flova::ProvisioningHandoff(
      "wss://engine.example/api/device-link",
      "ttttttttttttttttttttttttttttttttttttttttttt")) == FlovaProvisioningResponse::Accepted);
  network.online = tlsClock.clockReady = link.online = true;
  for (unsigned i = 0; i < 4; ++i) client.run();
  assert(client.lifecycle() == FlovaLifecycle::Bootstrapping);
  link.configuration.phase = FlovaLinkConfigurationPhase::Begin;
  link.configuration.messageId = 41;
  link.configuration.generation = 7;
  link.configuration.recordCount = 1;
  link.configuration.schemaVersion = 1;
  link.configuration.maximumRecordBytes = 448;
  link.configurationPending = true;
  client.run();
  assert(link.reports == 1 && link.lastReport.status == FlovaLinkResultStatus::Ok);
  link.configuration.phase = FlovaLinkConfigurationPhase::Record;
  link.configuration.messageId = 42;
  link.configuration.sequence = 0;
  link.configuration.recordType = 0;
  link.configuration.recordLength = 16;
  memset(link.configuration.record, 0x5a, 16);
  link.configurationPending = true;
  client.run();
  assert(link.reports == 2 && link.lastReport.status == FlovaLinkResultStatus::Ok);
  assert(link.lastReport.messageId == 42 && link.lastReport.generation == 7);
  const auto saved = storage.records;
  link.decoderAllowed = false;
  link.configuration.messageId = 43;
  link.configurationPending = true;
  client.run();
  assert(link.reports == 3 && link.lastReport.status == FlovaLinkResultStatus::Error);
  assert(link.lastReport.messageId == 43 && link.lastReport.generation == 7);
  assert(storage.records == saved);
  return 0;
}
