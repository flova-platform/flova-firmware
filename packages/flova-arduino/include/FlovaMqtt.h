#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <string.h>

// Optional JSON adapter for application-owned MQTT/TLS sockets. This is not
// the Flova Link codec or the universal firmware runtime.
class FlovaMqtt {
 public:
  static const size_t kMaxPayload = 2048;
  static const size_t kMaxKeys = 8;
  typedef void (*MessageHandler)(void*, const char*, JsonObjectConst);

  explicit FlovaMqtt(Client& socket) : mqtt_(socket) {}
  FlovaMqtt(const FlovaMqtt&) = delete;
  FlovaMqtt& operator=(const FlovaMqtt&) = delete;

  // Credentials and host must remain alive until end(). TLS and networking
  // remain application-owned; use a certificate-verifying secure socket.
  bool begin(const char* deviceId, const char* secret,
             const char* host = "mqtt.flova.ir", uint16_t port = 8883) {
    if (!validDeviceId(deviceId) || !secret || !*secret || !host || !*host) return false;
    end();
    if (!mqtt_.setBufferSize(kMaxPayload + sizeof(topic_) + 8)) return false;
    deviceId_ = deviceId;
    secret_ = secret;
    mqtt_.setServer(host, port);
    mqtt_.setKeepAlive(30);
    mqtt_.setSocketTimeout(3);
    mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
      receive(topic, payload, length);
    });
    attempted_ = false;
    return true;
  }

  void end() { mqtt_.disconnect(); deviceId_ = nullptr; secret_ = nullptr; pending_ = false; }
  bool connected() { return mqtt_.connected(); }
  void onMessage(MessageHandler handler, void* context = nullptr) {
    handler_ = handler; context_ = context;
  }

  // Register exact keys before connecting. No wildcard subscription or heap
  // growth per stream; reconnect restores this fixed registry and config.
  bool subscribe(const char* key) {
    if (!validKey(key)) return false;
    for (size_t i = 0; i < keyCount_; ++i) if (!strcmp(keys_[i], key)) return true;
    if (keyCount_ == kMaxKeys) return false;
    if (connected()) return false;
    strcpy(keys_[keyCount_++], key);
    return true;
  }

  // Call frequently on the application loop. Socket callbacks only copy one
  // bounded message; hardware/configuration work runs after mqtt_.loop().
  // A connection attempt can block up to the socket/TLS transport timeout.
  void run(bool networkReady = true) {
    if (!deviceId_) return;
    if (!networkReady) { mqtt_.disconnect(); pending_ = false; return; }
    if (!connected()) {
      uint32_t now = millis();
      if (attempted_ && static_cast<uint32_t>(now - lastAttempt_) < 5000) return;
      attempted_ = true; lastAttempt_ = now;
      if (!mqtt_.connect(deviceId_, deviceId_, secret_)) return;
      if (!makeTopic("down/config") || !mqtt_.subscribe(topic_, 1)) {
        mqtt_.disconnect(); return;
      }
      for (size_t i = 0; i < keyCount_; ++i) {
        if (!makeTopic("down/datastreams/", keys_[i]) || !mqtt_.subscribe(topic_, 1)) {
          mqtt_.disconnect(); return;
        }
      }
    }
    mqtt_.loop();
    if (!pending_) return;
    pending_ = false;
    document_.clear();
    if (deserializeJson(document_, incoming_, incomingSize_, DeserializationOption::NestingLimit(8)) ||
        !document_.is<JsonObject>()) return;
    if (handler_) handler_(context_, incomingPath_, document_.as<JsonObjectConst>());
  }

  template <typename T> bool report(const char* key, const T& value) {
    if (!validKey(key)) return false;
    StaticJsonDocument<256> valueDocument;
    valueDocument["value"] = value;
    if (valueDocument.overflowed()) return false;
    return publish("up/datastreams/", valueDocument.as<JsonObjectConst>(), key);
  }

  bool acknowledgeConfig(JsonObjectConst acknowledgement) {
    return publish("up/config/ack", acknowledgement);
  }
  bool heartbeat(JsonObjectConst metadata) { return publish("up/presence", metadata); }

 private:
  static bool validDeviceId(const char* id) {
    if (!id || strlen(id) != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
      if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != '-') return false; }
      else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    }
    return true;
  }
  static bool validKey(const char* key) {
    return key && *key && strlen(key) <= 128 && !strpbrk(key, "/#+");
  }
  bool makeTopic(const char* path, const char* key = "") {
    int length = snprintf(topic_, sizeof(topic_), "flova/v1/devices/%s/%s%s", deviceId_, path, key);
    return length > 0 && static_cast<size_t>(length) < sizeof(topic_);
  }
  bool publish(const char* path, JsonObjectConst payload, const char* key = "") {
    if (!connected() || !makeTopic(path, key)) return false;
    size_t length = measureJson(payload);
    if (length > kMaxPayload) return false;
    if (serializeJson(payload, outgoing_, sizeof(outgoing_)) != length) return false;
    // PubSubClient publishes at QoS 0. A successful return means written to
    // the socket, not an Engine ingestion acknowledgement.
    return mqtt_.publish(topic_, reinterpret_cast<const uint8_t*>(outgoing_), length, false);
  }
  void receive(const char* topic, const uint8_t* payload, size_t length) {
    if (pending_ || length > kMaxPayload || !makeTopic("down/")) return;
    size_t prefixLength = strlen(topic_);
    if (strncmp(topic, topic_, prefixLength)) return;
    const char* path = topic + prefixLength;
    bool allowed = !strcmp(path, "config");
    if (!strncmp(path, "datastreams/", 12)) {
      for (size_t i = 0; i < keyCount_; ++i)
        if (!strcmp(path + 12, keys_[i])) allowed = true;
    }
    if (!allowed || strlen(path) >= sizeof(incomingPath_)) return;
    strcpy(incomingPath_, path);
    memcpy(incoming_, payload, length);
    incoming_[length] = '\0'; incomingSize_ = length; pending_ = true;
  }

  PubSubClient mqtt_;
  const char* deviceId_ = nullptr;
  const char* secret_ = nullptr;
  char keys_[kMaxKeys][129] = {};
  size_t keyCount_ = 0;
  char topic_[224] = {};
  char incomingPath_[144] = {};
  char incoming_[kMaxPayload + 1] = {};
  char outgoing_[kMaxPayload + 1] = {};
  StaticJsonDocument<3072> document_;
  size_t incomingSize_ = 0;
  bool pending_ = false;
  bool attempted_ = false;
  uint32_t lastAttempt_ = 0;
  MessageHandler handler_ = nullptr;
  void* context_ = nullptr;
};
