#pragma once
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <FlovaTransport.h>

class ArduinoMqttTransport : public FlovaTransport {
 public:
  ArduinoMqttTransport(const char* host = "", uint16_t port = 1883) : host_(host), port_(port), mqtt_(wifi_) {}
  void configure(const String& host, uint16_t port) {
    hostValue_ = host;
    host_ = hostValue_.c_str();
    port_ = port;
    mqtt_.setServer(host_, port_);
    Serial.println("[flova] mqtt configured host=" + hostValue_ + " port=" + String(port_));
  }
  bool begin() override {
    mqtt_.setServer(host_, port_);
    mqtt_.setCallback([](char* topic, byte* payload, unsigned int length) {
      String body;
      for (unsigned int i = 0; i < length; i++) body += char(payload[i]);
      Serial.println("[flova] mqtt recv topic=" + String(topic) + " len=" + String(length));
      if (callbackSlot()) callbackSlot()(String(topic), body);
    });
    return true;
  }
  bool connected() override { return mqtt_.connected(); }
  bool connect(const char* clientId, const char* username, const char* password) override {
    Serial.println("[flova] mqtt connect client_id=" + String(clientId) + " username_len=" + String(strlen(username)));
    bool ok = mqtt_.connect(clientId, username, password);
    Serial.println("[flova] mqtt connect result=" + String(ok ? "ok" : "fail") + " state=" + String(mqtt_.state()));
    return ok;
  }
  bool publish(const char* topic, const String& payload) override {
    bool ok = mqtt_.publish(topic, payload.c_str());
    Serial.println("[flova] mqtt publish topic=" + String(topic) + " len=" + String(payload.length()) + " result=" + String(ok ? "ok" : "fail") + " state=" + String(mqtt_.state()));
    return ok;
  }
  bool subscribe(const char* topic) override {
    bool ok = mqtt_.subscribe(topic);
    Serial.println("[flova] mqtt subscribe topic=" + String(topic) + " result=" + String(ok ? "ok" : "fail") + " state=" + String(mqtt_.state()));
    return ok;
  }
  void setCallback(FlovaMessageCallback callback) override { callbackSlot() = callback; }
  void loop() override { mqtt_.loop(); }

 private:
  static FlovaMessageCallback& callbackSlot() {
    static FlovaMessageCallback callback = nullptr;
    return callback;
  }

  const char* host_;
  uint16_t port_;
  String hostValue_;
  WiFiClient wifi_;
  PubSubClient mqtt_;
};
