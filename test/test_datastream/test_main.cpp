#include <Arduino.h>
#include <FlovaDevice.h>
#include <FlovaConfiguration.h>
#include <FlovaLinkCodec.h>
#include <unity.h>

class TestTransport : public FlovaTransport {
 public:
  bool online = true;
  uint16_t relayId = 1;
  uint16_t temperatureId = 2;
  uint32_t generation = 7;
  uint16_t statePublishes = 0;
  uint16_t commandResultPublishes = 0;
  uint16_t configurationReportPublishes = 0;
  FlovaLinkStateBatch lastState = {};
  FlovaLinkCommandResult lastCommandResult = {};
  FlovaLinkConfigurationReport lastConfigurationReport = {};

  bool begin() override { return true; }
  bool connected() override { return online; }
  bool connect(const char*, const char*) override { online = true; return true; }
  bool datastreamIdForKey(const char* key, uint16_t& id) const override {
    if (key && strcmp(key, "relay") == 0) { id = relayId; return true; }
    if (key && strcmp(key, "temperature") == 0) { id = temperatureId; return true; }
    return false;
  }
  bool datastreamKeyForId(uint32_t requestedGeneration, uint16_t id, char* out,
                          size_t outSize) const override {
    if (requestedGeneration != generation || !out || !outSize) return false;
    const char* key = id == relayId ? "relay" : id == temperatureId ? "temperature" : nullptr;
    if (!key || strlen(key) >= outSize) return false;
    strcpy(out, key);
    return true;
  }
  void setConfigurationGeneration(uint32_t value) override { generation = value; }
  bool publishState(const FlovaLinkStateBatch& message) override {
    if (!online) return false;
    lastState = message;
    ++statePublishes;
    return true;
  }
  bool publishCommandResult(const FlovaLinkCommandResult& message) override {
    if (!online) return false;
    lastCommandResult = message;
    ++commandResultPublishes;
    return true;
  }
  bool publishHeartbeat(const FlovaLinkHeartbeat&) override { return online; }
  bool publishConfigurationReport(const FlovaLinkConfigurationReport& message) override {
    if (!online) return false;
    lastConfigurationReport = message;
    ++configurationReportPublishes;
    return true;
  }
  bool publishConfigurationState(const FlovaLinkConfigurationState&) override { return online; }
  bool publishOtaReport(const FlovaLinkOtaReport&) override { return online; }
  bool publishScheduleStatus(const FlovaLinkScheduleStatus&) override { return online; }
  bool publishScheduleRenew(const FlovaLinkScheduleStatus&) override { return online; }
  bool publishTimeRequest(const FlovaLinkTimeRequest&) override { return online; }
  void setCallback(FlovaMessageCallback callback) override { callback_ = callback; }
  void loop() override {}

 private:
  FlovaMessageCallback callback_ = nullptr;
};

class TestStorage : public FlovaStorage {
 public:
  bool getString(const char*, char*, size_t) override { return false; }
  bool setString(const char*, const char*) override { return true; }
  bool remove(const char*) override { return true; }
  void clear() override {}
};

class TestClock : public FlovaClock {
 public:
  uint32_t now = 100;
  uint32_t millisNow() override { return now++; }
};

class TestLogger : public FlovaLogger {
 public:
  void info(const char*) override {}
  void error(const char*) override {}
};

class TestConfigurationDevice : public FlovaDevice {
 public:
  using FlovaDevice::FlovaDevice;

  uint8_t recordsApplied = 0;
  uint8_t unitsApplied = 0;
  uint8_t configurationCommits = 0;

  void drainPendingConfiguration() { applyPendingConfiguration(); }
  void deferRuntime() { deferConfigurationRuntime(true); }

 protected:
  bool applyConfigurationRecord(const FlovaLinkConfigurationRecord&) override {
    ++recordsApplied;
    return true;
  }

  bool applyConfigurationUnit(const flova::config::Unit&) override {
    ++unitsApplied;
    return true;
  }

  void onConfigurationCommitted(uint32_t) override { ++configurationCommits; }
};

static TestTransport transport;
static TestStorage storage;
static TestClock clockSource;
static TestLogger logger;
static FlovaDevice device(transport, storage, clockSource, logger);
static FlovaDevice::Datastream<bool> relay = device.datastream<bool>("relay");
static FlovaDevice::Datastream<float> temperature = device.datastream<float>("temperature");
static uint8_t writes = 0;

static FlovaWriteResult relayWrite(bool value) {
  ++writes;
  return value ? FlovaWriteResult::accept() : FlovaWriteResult::reject("safety_lock");
}

void setUp() {
  transport.online = true;
  transport.statePublishes = 0;
  transport.commandResultPublishes = 0;
  transport.configurationReportPublishes = 0;
  writes = 0;
}

static void test_typed_state_uses_compact_mapping() {
  TEST_ASSERT_TRUE(temperature.report(31.5f).accepted());
  TEST_ASSERT_EQUAL_UINT16(1, transport.statePublishes);
  TEST_ASSERT_EQUAL_UINT8(1, transport.lastState.count);
  TEST_ASSERT_EQUAL_UINT16(transport.temperatureId, transport.lastState.readings[0].datastreamId);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FlovaLinkValueKind::Float32),
                          static_cast<uint8_t>(transport.lastState.readings[0].value.kind));
}

static void test_rejected_hardware_write_does_not_change_state() {
  relay.onWrite(relayWrite);
  TEST_ASSERT_TRUE(relay.write(true).accepted());
  TEST_ASSERT_FALSE(relay.write(false).accepted());
  TEST_ASSERT_TRUE(relay.read());
  TEST_ASSERT_EQUAL_UINT8(2, writes);
}

static void test_typed_command_routes_by_generation_and_id() {
  relay.onWrite(relayWrite);
  FlovaLinkInboundMessage message = {};
  message.type = FlovaLinkMessageType::Command;
  message.body.command.configurationGeneration = transport.generation;
  message.body.command.datastreamId = transport.relayId;
  message.body.command.isUserCommand = true;
  message.body.command.value.kind = FlovaLinkValueKind::Bool;
  message.body.command.value.data.boolean = true;
  message.body.command.commandId.present = true;
  message.body.command.commandId.bytes[0] = 9;
  device.handleMessage(message);
  TEST_ASSERT_EQUAL_UINT8(1, writes);
  TEST_ASSERT_EQUAL_UINT16(1, transport.commandResultPublishes);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FlovaLinkResultStatus::Ok),
                          static_cast<uint8_t>(transport.lastCommandResult.status));
}

static void test_oversized_frame_is_rejected_before_payload_decode() {
  uint8_t frame[flova::link::kMaximumFrameBytes + 1] = {};
  flova::link::FrameView view = {};
  TEST_ASSERT_EQUAL(static_cast<int>(flova::link::FrameResult::Invalid),
                    static_cast<int>(flova::link::decodeWebSocketBinaryMessage(
                        frame, sizeof(frame), view)));
}

static void test_configuration_is_persisted_without_live_mutation() {
  TestConfigurationDevice configurationDevice(transport, storage, clockSource, logger);
  FlovaLinkInboundMessage record = {};
  record.type = FlovaLinkMessageType::ConfigurationRecord;
  record.body.configuration.phase = FlovaLinkConfigurationPhase::Record;
  record.body.configuration.messageId = 2;
  record.body.configuration.generation = 1;
  record.body.configuration.sequence = 0;
  record.body.configuration.recordLength = 1;
  record.body.configuration.record[0] = 0xA1;
  record.body.configuration.hasTypedUnit = true;

  configurationDevice.handleMessage(record);
  configurationDevice.drainPendingConfiguration();
  TEST_ASSERT_EQUAL_UINT8(1, configurationDevice.recordsApplied);
  TEST_ASSERT_EQUAL_UINT8(0, configurationDevice.unitsApplied);
  TEST_ASSERT_EQUAL_UINT8(0, configurationDevice.configurationCommits);

  record.body.configuration.messageId = 3;
  record.body.configuration.sequence = 1;
  configurationDevice.handleMessage(record);
  configurationDevice.drainPendingConfiguration();
  TEST_ASSERT_EQUAL_UINT8(2, configurationDevice.recordsApplied);
  TEST_ASSERT_EQUAL_UINT8(0, configurationDevice.unitsApplied);
}

static void test_bootstrap_persists_configuration_without_runtime_allocation() {
  TestConfigurationDevice configurationDevice(transport, storage, clockSource, logger);
  configurationDevice.deferRuntime();
  FlovaLinkInboundMessage record = {};
  record.type = FlovaLinkMessageType::ConfigurationRecord;
  record.body.configuration.phase = FlovaLinkConfigurationPhase::Record;
  record.body.configuration.messageId = 2;
  record.body.configuration.generation = 1;
  record.body.configuration.recordLength = 1;
  record.body.configuration.record[0] = 0xA1;
  record.body.configuration.hasTypedUnit = true;

  configurationDevice.handleMessage(record);
  configurationDevice.drainPendingConfiguration();
  TEST_ASSERT_EQUAL_UINT8(1, configurationDevice.recordsApplied);
  TEST_ASSERT_EQUAL_UINT8(0, configurationDevice.unitsApplied);
  TEST_ASSERT_EQUAL_UINT8(0, configurationDevice.configurationCommits);
}

static void test_configuration_end_acknowledges_record_count() {
  TestConfigurationDevice configurationDevice(transport, storage, clockSource, logger);
  FlovaLinkInboundMessage ending = {};
  ending.type = FlovaLinkMessageType::ConfigurationEnd;
  ending.body.configuration.phase = FlovaLinkConfigurationPhase::End;
  ending.body.configuration.messageId = 4;
  ending.body.configuration.generation = 1;
  ending.body.configuration.recordCount = 2;

  configurationDevice.handleMessage(ending);
  configurationDevice.drainPendingConfiguration();

  TEST_ASSERT_EQUAL_UINT16(1, transport.configurationReportPublishes);
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(transport.lastConfigurationReport.messageId));
  TEST_ASSERT_EQUAL_UINT32(1, transport.lastConfigurationReport.generation);
  TEST_ASSERT_EQUAL_UINT32(2, transport.lastConfigurationReport.sequence);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FlovaLinkResultStatus::Ok),
                          static_cast<uint8_t>(transport.lastConfigurationReport.status));
  TEST_ASSERT_EQUAL_UINT8(1, configurationDevice.configurationCommits);
}

static void test_committed_credentials_always_select_runtime() {
  using flova::ProvisioningBootMode;
  TEST_ASSERT_EQUAL(
      static_cast<int>(ProvisioningBootMode::Runtime),
      static_cast<int>(flova::provisioningBootMode(true, false, false)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(ProvisioningBootMode::Runtime),
      static_cast<int>(flova::provisioningBootMode(true, true, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(ProvisioningBootMode::Bootstrap),
      static_cast<int>(flova::provisioningBootMode(false, true, false)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(ProvisioningBootMode::Setup),
      static_cast<int>(flova::provisioningBootMode(false, false, false)));
}

void setup() {
  FlovaConfig config;
  config.deviceId = "00000000-0000-0000-0000-000000000001";
  config.linkSecret = "test-secret";
  config.heartbeatIntervalMs = 0xffffffff;
  config.limits.commandDedup = FLOVA_COMMAND_DEDUP_CAPACITY;
  device.configure(config);
  TEST_ASSERT_TRUE(device.begin());

  UNITY_BEGIN();
#if defined(FLOVA_BOOTSTRAP_ONLY)
  RUN_TEST(test_configuration_end_acknowledges_record_count);
  RUN_TEST(test_committed_credentials_always_select_runtime);
#else
  RUN_TEST(test_typed_state_uses_compact_mapping);
  RUN_TEST(test_rejected_hardware_write_does_not_change_state);
  RUN_TEST(test_typed_command_routes_by_generation_and_id);
  RUN_TEST(test_oversized_frame_is_rejected_before_payload_decode);
  RUN_TEST(test_configuration_is_persisted_without_live_mutation);
  RUN_TEST(test_bootstrap_persists_configuration_without_runtime_allocation);
  RUN_TEST(test_configuration_end_acknowledges_record_count);
  RUN_TEST(test_committed_credentials_always_select_runtime);
#endif
  UNITY_END();
}

void loop() {}
