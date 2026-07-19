#include <Arduino.h>
#include <FlovaConfiguration.h>
#include <FlovaDevice.h>
#include <unity.h>

class TestTransport : public FlovaTransport {
 public:
  bool online = true;
  int publishes = 0;
  String lastTopic, lastPayload;
  bool begin() override { return true; }
  bool connected() override { return online; }
  bool connect(const char*, const char*, const char*) override { online = true; return true; }
  bool publish(const char* topic, const String& payload) override { publishes++; lastTopic = topic; lastPayload = payload; return online; }
  bool subscribe(const char*) override { return true; }
  void setCallback(FlovaMessageCallback callback) override { callback_ = callback; }
  void loop() override {}
 private:
  FlovaMessageCallback callback_ = nullptr;
};

class TestStorage : public FlovaStorage {
 public:
  bool getString(const char*, String&) override { return false; }
  bool getString(const char* key, char* out, size_t maxLen) override {
    if (String(key) != key_ || !value_.length()) return false;
    value_.toCharArray(out, maxLen); return true;
  }
  bool setString(const char* key, const char* value) override { key_ = key; value_ = value; return true; }
  bool remove(const char*) override { value_ = ""; return true; }
  void clear() override { key_ = value_ = ""; }
 private:
  String key_, value_;
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

static int writes;
static bool rejectWrites;
static int reads;
static FlovaWriteResult handleRelay(bool) {
  writes++;
  return rejectWrites ? FlovaWriteResult::reject("safety_lock") : FlovaWriteResult::accept();
}
static FlovaReadResult<float> readTemperature() { reads++; return FlovaReadResult<float>::success(31.5f); }

static TestTransport transport;
static TestStorage storage;
static TestClock clockSource;
static TestLogger logger;
static FlovaDevice device(transport, storage, clockSource, logger);
static FlovaDevice::Datastream<bool> relay = device.datastream<bool>("relay");
static FlovaDevice::Datastream<float> temperature = device.datastream<float>("temperature").mode(FlovaDatastreamMode::Sample);

void setUp() {
  writes = reads = 0; rejectWrites = false; transport.online = true; transport.publishes = 0;
}
void tearDown() {}

static void test_read_is_cache_only() {
  TEST_ASSERT_FALSE(temperature.hasValue());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, temperature.read());
  TEST_ASSERT_EQUAL(0, reads);
}

static void test_refresh_reports_without_write_handler() {
  temperature.onRead(readTemperature);
  TEST_ASSERT_TRUE(temperature.refresh().ok);
  TEST_ASSERT_EQUAL(1, reads);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 31.5f, temperature.read());
  TEST_ASSERT_EQUAL(0, writes);
}

static void test_rejection_preserves_authoritative_state() {
  relay.onWrite(handleRelay);
  TEST_ASSERT_TRUE(relay.write(false).accepted());
  rejectWrites = true;
  FlovaWriteResult result = relay.write(true);
  TEST_ASSERT_EQUAL((int)FlovaWriteStatus::Rejected, (int)result.status);
  TEST_ASSERT_FALSE(relay.read());
  TEST_ASSERT_EQUAL(2, writes);
}

static void test_cloud_uses_same_handler_and_acknowledges() {
  relay.onWrite(handleRelay);
  device.handleMessage("flova/v1/devices/test/commands", "{\"command_id\":\"cmd-1\",\"behavior\":\"command\",\"key\":\"relay\",\"value\":true}");
  TEST_ASSERT_EQUAL(1, writes);
  TEST_ASSERT_TRUE(relay.read());
  TEST_ASSERT_TRUE(transport.lastTopic.endsWith("/ack"));
}

static void test_offline_state_coalesces_latest_value() {
  relay.onWrite(handleRelay).offline(FlovaOfflinePolicy::KeepLatest);
  transport.online = false;
  TEST_ASSERT_TRUE(relay.write(false).accepted());
  TEST_ASSERT_TRUE(relay.write(true).accepted());
  int beforeReconnect = transport.publishes;
  transport.online = true;
  device.loop();
  TEST_ASSERT_EQUAL(beforeReconnect + 1, transport.publishes);
  TEST_ASSERT_TRUE(transport.lastPayload.indexOf("true") >= 0);
}

static flova::DeviceConfiguration validConfiguration() {
  flova::DeviceConfiguration config;
  config.wifiSsid = "home_wifi";
  config.wifiPassword = "secret";
  config.deviceId = "device-1";
  config.mqttHost = "mqtt.local";
  config.mqttPort = 1883;
  config.mqttUsername = "device";
  config.mqttPassword = "password";
  config.datastreamKeys = "relay,temperature";
  config.runtimeJson = "{\"limits\":{},\"datastreams\":[]}";
  config.templateVersionId = "version-1";
  config.checksum = "checksum";
  return config;
}

static void test_configuration_snapshot_round_trip() {
  flova::DeviceConfiguration expected = validConfiguration();
  String snapshot;
  TEST_ASSERT_TRUE(flova::encodeConfiguration(expected, snapshot));
  TEST_ASSERT_LESS_THAN(flova::kConfigurationJsonBytes, snapshot.length());

  flova::DeviceConfiguration actual;
  TEST_ASSERT_TRUE(flova::decodeConfiguration(snapshot, actual));
  TEST_ASSERT_EQUAL_STRING(expected.deviceId.c_str(), actual.deviceId.c_str());
  TEST_ASSERT_EQUAL_STRING(expected.runtimeJson.c_str(), actual.runtimeJson.c_str());
  TEST_ASSERT_EQUAL(expected.mqttPort, actual.mqttPort);
}

static void test_configuration_rejects_oversized_runtime() {
  flova::DeviceConfiguration config = validConfiguration();
  config.runtimeJson.reserve(flova::kRuntimeJsonBytes + 1);
  while (config.runtimeJson.length() < flova::kRuntimeJsonBytes) config.runtimeJson += "x";
  String snapshot;
  TEST_ASSERT_FALSE(flova::encodeConfiguration(config, snapshot));
}

static void test_filtered_response_ignores_large_metadata() {
  String response =
      "{\"device_id\":\"device-1\",\"mqtt\":{\"host\":\"mqtt.local\",\"port\":1883,"
      "\"username\":\"device\",\"password\":\"password\"},\"datastreams\":["
      "{\"key\":\"relay\",\"hardware_mapping\":{\"kind\":\"digital_output\",\"pin\":\"GPIO5\"}}],"
      "\"metadata\":\"";
  while (response.length() < 5000) response += "x";
  response += "\"}";

  DynamicJsonDocument filter(1024);
  flova::provisioningResponseFilter(filter);
  DynamicJsonDocument runtime(4096);
  TEST_ASSERT_FALSE(deserializeJson(runtime, response, DeserializationOption::Filter(filter)));
  String compact, keys;
  TEST_ASSERT_TRUE(flova::compactRuntime(runtime, compact, keys));
  TEST_ASSERT_EQUAL_STRING("relay", keys.c_str());
  TEST_ASSERT_LESS_THAN(flova::kRuntimeJsonBytes, compact.length());
}

void setup() {
  FlovaConfig config; config.deviceId = "test"; config.datastreamKeys = "relay,temperature"; config.heartbeatIntervalMs = 0xffffffff;
  device.configure(config); relay.onWrite(handleRelay); temperature.onRead(readTemperature); device.begin();
  UNITY_BEGIN();
  RUN_TEST(test_read_is_cache_only);
  RUN_TEST(test_refresh_reports_without_write_handler);
  RUN_TEST(test_rejection_preserves_authoritative_state);
  RUN_TEST(test_cloud_uses_same_handler_and_acknowledges);
  RUN_TEST(test_offline_state_coalesces_latest_value);
  RUN_TEST(test_configuration_snapshot_round_trip);
  RUN_TEST(test_configuration_rejects_oversized_runtime);
  RUN_TEST(test_filtered_response_ignores_large_metadata);
  UNITY_END();
}

void loop() {}
