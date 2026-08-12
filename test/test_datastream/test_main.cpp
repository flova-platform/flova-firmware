#include <Arduino.h>
#include <FlovaCore.h>
#include <FlovaWifiProvisioning.h>
#include <unity.h>

class TestLink final : public flova::Link {
 public:
  bool online = true;
  uint16_t sends = 0;
  flova::Message last;

  bool begin() override { return true; }
  bool connected() const override { return online; }
  bool send(const flova::Message& message) override {
    if (!online) return false;
    last = message;
    ++sends;
    return true;
  }
  void poll() override {}
  void setReceiver(flova::MessageReceiver receiver, void* context) override {
    receiver_ = receiver;
    context_ = context;
  }
  uint32_t messageNonce() const override { return 0x12345678UL; }
  bool bindDatastreams(const char* const* keys, size_t count,
                       DatastreamId* ids) override {
    if ((count && (!keys || !ids)) || count > FLOVA_DATASTREAM_CAPACITY)
      return false;
    for (size_t i = 0; i < count; ++i) {
      if (!keys[i] || !keys[i][0]) return false;
      ids[i] = static_cast<DatastreamId>(i + 1);
    }
    return true;
  }
  void inject(const flova::Message& message) {
    if (receiver_) receiver_(context_, message);
  }

 private:
  flova::MessageReceiver receiver_ = nullptr;
  void* context_ = nullptr;
};

class TestStorage final : public flova::Storage {
 public:
  bool read(const char*, void*, size_t) override { return false; }
  bool write(const char*, const void*, size_t) override { return true; }
  bool remove(const char*) override { return true; }
};

class TestClock final : public flova::Clock {
 public:
  uint64_t now = 100;
  uint64_t utc = 100000;
  uint64_t milliseconds() const override { return now; }
  bool utcValid() const override { return true; }
  uint64_t utcMilliseconds() const override { return utc + now; }
};

class TestLogger final : public flova::Logger {
 public:
  void log(const char*) override {}
};

static uint8_t hardwareWrites = 0;

static flova::WriteResult writeRelay(bool value) {
  ++hardwareWrites;
  return value ? flova::WriteResult::accept()
               : flova::WriteResult::reject("safety_lock");
}

void setUp() { hardwareWrites = 0; }
void tearDown() {}

static void test_typed_state_uses_bound_compact_id() {
  TestLink link;
  TestStorage storage;
  TestClock clock;
  TestLogger logger;
  flova::Device device(link, storage, clock, logger);
  flova::Datastream<float> temperature = device.datastream<float>("temperature");
  TEST_ASSERT_TRUE(device.begin());
  TEST_ASSERT_TRUE(temperature.report(31.5f).accepted());
  TEST_ASSERT_EQUAL_UINT16(1, link.last.datastreamId);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(flova::ValueType::Float),
                          static_cast<uint8_t>(link.last.value.type));
}

static void test_rejected_hardware_write_does_not_change_state() {
  TestLink link;
  TestStorage storage;
  TestClock clock;
  TestLogger logger;
  flova::Device device(link, storage, clock, logger);
  flova::Datastream<bool> relay = device.datastream<bool>("relay");
  relay.onWrite(writeRelay);
  TEST_ASSERT_TRUE(device.begin());
  TEST_ASSERT_TRUE(relay.write(true).accepted());
  TEST_ASSERT_FALSE(relay.write(false).accepted());
  TEST_ASSERT_TRUE(relay.value());
  TEST_ASSERT_EQUAL_UINT8(2, hardwareWrites);
}

static void test_state_retries_until_matching_ack() {
  TestLink link;
  TestStorage storage;
  TestClock clock;
  TestLogger logger;
  flova::Device device(link, storage, clock, logger);
  flova::Datastream<bool> relay = device.datastream<bool>("relay");
  TEST_ASSERT_TRUE(device.begin());
  TEST_ASSERT_TRUE(relay.report(true).accepted());
  const uint64_t messageId = link.last.messageId;
  TEST_ASSERT_TRUE(messageId != 0);
  TEST_ASSERT_EQUAL_UINT16(1, link.sends);

  clock.now += 4999;
  device.run();
  TEST_ASSERT_EQUAL_UINT16(1, link.sends);
  clock.now += 1;
  device.run();
  TEST_ASSERT_EQUAL_UINT16(2, link.sends);
  TEST_ASSERT_EQUAL_UINT64(messageId, link.last.messageId);

  flova::Message ack;
  ack.kind = flova::MessageKind::Acknowledgement;
  ack.messageId = messageId;
  link.inject(ack);
  clock.now += 6000;
  device.run();
  TEST_ASSERT_EQUAL_UINT16(2, link.sends);
}

static void test_configured_datastream_owns_its_key() {
  TestLink link;
  TestStorage storage;
  TestClock clock;
  TestLogger logger;
  flova::Device device(link, storage, clock, logger);
  flova::config::Unit unit = {};
  unit.kind = flova::config::UnitKind::Datastream;
  unit.data.datastream.id = 7;
  unit.data.datastream.valueType = 0;
  flova::Value::copy(unit.data.datastream.key, "relay");
  TEST_ASSERT_TRUE(device.applyConfigurationUnit(unit));

  // Simulate the configuration decoder reusing the same record workspace.
  flova::Value::copy(unit.data.datastream.key, "sensor");
  unit.data.datastream.id = 8;
  TEST_ASSERT_TRUE(device.applyConfigurationUnit(unit));
  TEST_ASSERT_EQUAL_UINT16(2, device.datastreamCount());
  TEST_ASSERT_TRUE(device.begin());
}

static void test_provisioning_parser_is_bounded_and_rejects_duplicates() {
  const char valid[] =
      "{\"wifi_ssid\":\"lab\",\"wifi_password\":\"secret\","
      "\"link_url\":\"wss://engine.example/link\",\"token\":\"once\"}";
  flova::ProvisioningHandoff input;
  flova::WifiRuntimeData wifi = {};
  TEST_ASSERT_TRUE(
      flova::parseWifiProvisioningHandoff(valid, strlen(valid), input, &wifi));
  TEST_ASSERT_TRUE(flova::validWifiRuntimeData(wifi));
  TEST_ASSERT_EQUAL_STRING("lab", wifi.ssid);
  TEST_ASSERT_EQUAL_STRING("wss://engine.example/link", input.linkUrl);

  const char duplicate[] =
      "{\"wifi_ssid\":\"lab\",\"wifi_ssid\":\"other\","
      "\"link_url\":\"wss://engine.example/link\",\"token\":\"once\"}";
  TEST_ASSERT_FALSE(
      flova::parseWifiProvisioningHandoff(duplicate, strlen(duplicate), input));
  TEST_ASSERT_FALSE(
      flova::parseWifiProvisioningHandoff(valid, strlen(valid) - 1, input));
  const char nested[] =
      "{\"wrapper\":{\"wifi_ssid\":\"lab\"},"
      "\"link_url\":\"wss://engine.example/link\",\"token\":\"once\"}";
  TEST_ASSERT_FALSE(
      flova::parseWifiProvisioningHandoff(nested, strlen(nested), input));
  const char trailing[] =
      "{\"wifi_ssid\":\"lab\",\"link_url\":\"wss://engine.example/link\","
      "\"token\":\"once\",}";
  TEST_ASSERT_FALSE(
      flova::parseWifiProvisioningHandoff(trailing, strlen(trailing), input));
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_typed_state_uses_bound_compact_id);
  RUN_TEST(test_rejected_hardware_write_does_not_change_state);
  RUN_TEST(test_state_retries_until_matching_ack);
  RUN_TEST(test_configured_datastream_owns_its_key);
  RUN_TEST(test_provisioning_parser_is_bounded_and_rejects_duplicates);
  UNITY_END();
}

void loop() {}
