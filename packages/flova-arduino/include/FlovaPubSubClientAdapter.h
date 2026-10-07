#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <string.h>
#include <FlovaCustomCode.h>

// Internal board-facade adapter. Applications pass PubSubClient to FlovaEsp32
// or FlovaEsp8266 directly; they do not construct this type.
class FlovaPubSubClientAdapter {
 public:
  static const size_t kMaxPayload = 2048;
  static const size_t kMaxKeys = 8;
  typedef void (*MessageHandler)(void*, const char*, JsonObjectConst);
  typedef void (*SimpleMessageHandler)(const char*, JsonObjectConst);

  explicit FlovaPubSubClientAdapter(PubSubClient* client = nullptr)
      : client_(client) {}

  ~FlovaPubSubClientAdapter() {
    if (client_ && configured_) {
      client_->disconnect();
      client_->setCallback(ignoreMessage);
    }
  }
  FlovaPubSubClientAdapter(const FlovaPubSubClientAdapter&) = delete;
  FlovaPubSubClientAdapter& operator=(const FlovaPubSubClientAdapter&) = delete;

  bool begin(const char* deviceId, const char* secret,
             const char* host = "mqtt.flova.ir", uint16_t port = 8883) {
    if (!client_ || !validDeviceId(deviceId) || !secret || !*secret ||
        !host || !*host || !client_->setBufferSize(kMaxPayload + 240))
      return false;
    client_->setServer(host, port);
    client_->setKeepAlive(45);
    client_->setSocketTimeout(3);
    deviceId_ = deviceId;
    secret_ = secret;
    client_->setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
      receive(topic, payload, length);
    });
    configured_ = true;
    attempted_ = false;
    return true;
  }

  bool configured() const { return configured_; }
  bool connected() const { return client_ && client_->connected(); }

  void onMessage(MessageHandler handler, void* context = nullptr) {
    handler_ = handler;
    simpleHandler_ = nullptr;
    context_ = context;
  }

  void onMessage(SimpleMessageHandler handler) {
    simpleHandler_ = handler;
    handler_ = nullptr;
    context_ = nullptr;
  }

  bool subscribe(const char* key) {
    if (!validKey(key)) return false;
    for (size_t i = 0; i < keyCount_; ++i)
      if (!strcmp(keys_[i], key)) return true;
    if (keyCount_ == kMaxKeys || connected()) return false;
    strcpy(keys_[keyCount_++], key);
    return true;
  }

  bool onDatastream(const char* key, flova::ValueType valueType,
                    FlovaDatastreamCommandHandler handler, void* context) {
    if (!validKey(key) || !handler || !subscribe(key)) return false;
    for (size_t i = 0; i < keyCount_; ++i) {
      if (!strcmp(keys_[i], key)) {
        datastreamTypes_[i] = valueType;
        datastreamHandlers_[i] = handler;
        datastreamContexts_[i] = context;
        return true;
      }
    }
    return false;
  }

  void run(bool networkReady = true) {
    if (!configured_ || !client_) return;
    if (!networkReady) {
      client_->disconnect();
      pending_ = false;
      return;
    }
    if (!connected()) {
      const uint32_t now = millis();
      if (attempted_ && static_cast<uint32_t>(now - lastAttempt_) < 5000) return;
      attempted_ = true;
      lastAttempt_ = now;
      if (!client_->connect(deviceId_, deviceId_, secret_, willTopic(), 1, false,
                            offlinePresence(), true))
        return;
      if (!subscribeTopic("down/config")) {
        client_->disconnect();
        return;
      }
      for (size_t i = 0; i < keyCount_; ++i) {
        if (!makeTopic("down/datastreams/", keys_[i]) ||
            !client_->subscribe(topic_, 1)) {
          client_->disconnect();
          return;
        }
      }
      publishPresence(true);
    }
    client_->loop();
    if (!pending_) return;
    pending_ = false;
    document_.clear();
    if (deserializeJson(document_, incoming_, incomingSize_,
                        DeserializationOption::NestingLimit(8)) ||
        !document_.is<JsonObject>()) return;
    if (dispatchDatastream()) return;
    if (handler_)
      handler_(context_, incomingPath_, document_.as<JsonObjectConst>());
    else if (simpleHandler_)
      simpleHandler_(incomingPath_, document_.as<JsonObjectConst>());
  }

  template <typename T>
  bool report(const char* key, const T& value) {
    if (!connected() || !validKey(key) || !makeTopic("up/datastreams/", key))
      return false;
    StaticJsonDocument<256> payload;
    payload["value"] = value;
    if (payload.overflowed()) return false;
    return publish(payload.as<JsonObjectConst>());
  }

  bool heartbeat(JsonObjectConst payload) {
    return connected() && makeTopic("up/presence") && publish(payload);
  }

  bool info(JsonObjectConst payload) {
    return connected() && makeTopic("up/info") && publish(payload);
  }

  template <typename T>
  bool acknowledgeDatastream(const char* key, const T& value,
                             const char* commandId, uint32_t desiredVersion) {
    if (!connected() || !validKey(key) || !commandId || !*commandId ||
        !makeAckTopic(key))
      return false;
    StaticJsonDocument<256> payload;
    payload["value"] = value;
    payload["command_id"] = commandId;
    payload["desired_version"] = desiredVersion;
    if (payload.overflowed()) return false;
    return publish(payload.as<JsonObjectConst>());
  }

  bool rejectDatastream(const char* key, const char* commandId,
                        const char* error) {
    if (!connected() || !validKey(key) || !commandId || !*commandId ||
        !error || !*error || !makeAckTopic(key))
      return false;
    StaticJsonDocument<192> payload;
    payload["command_id"] = commandId;
    payload["error"] = error;
    if (payload.overflowed()) return false;
    return publish(payload.as<JsonObjectConst>());
  }

  bool acknowledgeConfig(JsonObjectConst payload) {
    return connected() && makeTopic("up/config/ack") && publish(payload);
  }

 private:
  static void ignoreMessage(char*, uint8_t*, unsigned int) {}

  static bool validDeviceId(const char* id) {
    if (!id || strlen(id) != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
      if (i == 8 || i == 13 || i == 18 || i == 23) {
        if (id[i] != '-') return false;
      } else if (!((id[i] >= '0' && id[i] <= '9') ||
                   (id[i] >= 'a' && id[i] <= 'f') ||
                   (id[i] >= 'A' && id[i] <= 'F'))) {
        return false;
      }
    }
    return true;
  }

  static bool validKey(const char* key) {
    return key && *key && strlen(key) <= 128 && !strpbrk(key, "/#+");
  }

  bool makeTopic(const char* path, const char* suffix = "") {
    if (!deviceId_) return false;
    const int length = snprintf(topic_, sizeof(topic_),
                                "flova/v1/devices/%s/%s%s", deviceId_, path,
                                suffix);
    return length > 0 && static_cast<size_t>(length) < sizeof(topic_);
  }

  bool makeAckTopic(const char* key) {
    if (!deviceId_ || !validKey(key)) return false;
    const int length = snprintf(topic_, sizeof(topic_),
                                "flova/v1/devices/%s/up/datastreams/%s/ack",
                                deviceId_, key);
    return length > 0 && static_cast<size_t>(length) < sizeof(topic_);
  }

  const char* willTopic() {
    makeTopic("up/presence");
    return topic_;
  }

  static const char* offlinePresence() { return "{\"online\":false}"; }

  void publishPresence(bool online) {
    if (!makeTopic("up/presence")) return;
    StaticJsonDocument<64> payload;
    payload["online"] = online;
    publish(payload.as<JsonObjectConst>());
  }

  bool subscribeTopic(const char* path) {
    return makeTopic(path) && client_->subscribe(topic_, 1);
  }

  bool publish(JsonObjectConst payload) {
    const size_t length = measureJson(payload);
    if (length > kMaxPayload ||
        serializeJson(payload, outgoing_, sizeof(outgoing_)) != length)
      return false;
    return client_->publish(topic_, reinterpret_cast<const uint8_t*>(outgoing_),
                            length, false);
  }

  bool dispatchDatastream() {
    if (strncmp(incomingPath_, "datastreams/", 12)) return false;
    const char* key = incomingPath_ + 12;
    for (size_t i = 0; i < keyCount_; ++i) {
      if (strcmp(key, keys_[i]) || !datastreamHandlers_[i]) continue;
      FlovaDatastreamCommand command = {};
      command.key = key;
      command.commandId = document_["command_id"] | "";
      command.desiredVersion = document_["desired_version"] | 0;
      JsonVariant value = document_["value"];
      switch (datastreamTypes_[i]) {
        case flova::ValueType::Boolean:
          if (!value.is<bool>()) {
            rejectDatastream(key, command.commandId, "invalid_value");
            return true;
          }
          command.value = flova::Value::from(value.as<bool>());
          break;
        case flova::ValueType::Int64:
          if (!value.is<long long>()) {
            rejectDatastream(key, command.commandId, "invalid_value");
            return true;
          }
          command.value = flova::Value::from(static_cast<int64_t>(value.as<long long>()));
          break;
        case flova::ValueType::Float:
          if (!value.is<float>() && !value.is<double>()) {
            rejectDatastream(key, command.commandId, "invalid_value");
            return true;
          }
          command.value = flova::Value::from(static_cast<float>(value.as<double>()));
          break;
        case flova::ValueType::Double:
          if (!value.is<float>() && !value.is<double>()) {
            rejectDatastream(key, command.commandId, "invalid_value");
            return true;
          }
          command.value = flova::Value::from(value.as<double>());
          break;
        case flova::ValueType::Text:
          if (!value.is<const char*>()) {
            rejectDatastream(key, command.commandId, "invalid_value");
            return true;
          }
          command.value = flova::Value::from(value.as<const char*>());
          break;
      }
      const flova::WriteResult result =
          datastreamHandlers_[i](datastreamContexts_[i], command);
      if (result.accepted()) {
        acknowledgeDatastreamValue(key, command.value, command.commandId,
                                   command.desiredVersion);
      } else {
        rejectDatastream(key, command.commandId,
                         result.reason && result.reason[0] ? result.reason : "rejected");
      }
      return true;
    }
    return false;
  }

  bool acknowledgeDatastreamValue(const char* key, const flova::Value& value,
                                  const char* commandId, uint32_t desiredVersion) {
    if (!connected() || !validKey(key) || !commandId || !*commandId ||
        !makeAckTopic(key)) return false;
    StaticJsonDocument<256> payload;
    if (value.type == flova::ValueType::Boolean) payload["value"] = value.scalar.boolean;
    else if (value.type == flova::ValueType::Int64) payload["value"] = value.scalar.integer;
    else if (value.type == flova::ValueType::Float) payload["value"] = value.scalar.floating;
    else if (value.type == flova::ValueType::Double) payload["value"] = value.scalar.number;
    else payload["value"] = value.text;
    payload["command_id"] = commandId;
    payload["desired_version"] = desiredVersion;
    if (payload.overflowed()) return false;
    return publish(payload.as<JsonObjectConst>());
  }

  void receive(const char* topic, const uint8_t* payload, size_t length) {
    if (pending_ || !topic || !payload || length > kMaxPayload ||
        !makeTopic("down/")) return;
    const size_t prefixLength = strlen(topic_);
    const size_t topicLength = strlen(topic);
    if (topicLength <= prefixLength ||
        memcmp(topic, topic_, prefixLength) != 0) return;
    const char* path = topic + prefixLength;
    bool allowed = !strcmp(path, "config");
    if (!strncmp(path, "datastreams/", 12)) {
      for (size_t i = 0; i < keyCount_; ++i)
        if (!strcmp(path + 12, keys_[i])) allowed = true;
    }
    if (!allowed || strlen(path) >= sizeof(incomingPath_)) return;
    strcpy(incomingPath_, path);
    memcpy(incoming_, payload, length);
    incoming_[length] = 0;
    incomingSize_ = length;
    pending_ = true;
  }

  PubSubClient* client_ = nullptr;
  const char* deviceId_ = nullptr;
  const char* secret_ = nullptr;
  char keys_[kMaxKeys][129] = {};
  flova::ValueType datastreamTypes_[kMaxKeys] = {};
  FlovaDatastreamCommandHandler datastreamHandlers_[kMaxKeys] = {};
  void* datastreamContexts_[kMaxKeys] = {};
  size_t keyCount_ = 0;
  char topic_[224] = {};
  char incomingPath_[144] = {};
  char incoming_[kMaxPayload + 1] = {};
  char outgoing_[kMaxPayload + 1] = {};
  StaticJsonDocument<3072> document_;
  size_t incomingSize_ = 0;
  bool configured_ = false;
  bool pending_ = false;
  bool attempted_ = false;
  uint32_t lastAttempt_ = 0;
  MessageHandler handler_ = nullptr;
  SimpleMessageHandler simpleHandler_ = nullptr;
  void* context_ = nullptr;
};
