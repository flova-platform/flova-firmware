#pragma once
#include "FlovaClock.h"
#include "FlovaLogger.h"
#include "FlovaStorage.h"
#include "FlovaTransport.h"
#include "FlovaTypes.h"

class FlovaDevice {
 public:
  FlovaDevice(FlovaTransport& transport, FlovaStorage& storage, FlovaClock& clock, FlovaLogger& logger)
      : transport_(transport), storage_(storage), clock_(clock), logger_(logger) {}

  void configure(const FlovaConfig& config);
  bool begin();
  void loop();
  void onWrite(const char* key, FlovaWriteHandler handler);
  void addDigitalOutput(const char* key, uint8_t pin, bool activeHigh = true, uint32_t minOutputIntervalMs = 300);
  void addDigitalInput(const char* key, uint8_t pin, bool activeHigh = true, uint32_t debounceMs = 50, uint8_t mode = INPUT);
  bool write(const char* key, bool value);
  bool write(const char* key, double value);
  bool write(const char* key, const char* value);
  void setStatusLed(uint8_t pin, bool activeLow);
  void setFactoryResetButton(uint8_t pin, bool activeLow, uint32_t holdMs = 10000);
  bool ack(const String& commandId, const String& status, const String& resultJson);
  bool isOtaCapable() const { return config_.otaCapable; }
  void handleOtaOffer(const String&) {}
  void factoryReset();
  void handleMessage(const String& topic, const String& payload);

 private:
  struct Handler {
    String key;
    FlovaWriteHandler handler = nullptr;
  };
  struct DigitalOutput {
    String key;
    uint8_t pin = 255;
    bool activeHigh = true;
    bool value = false;
    bool pending = false;
    bool pendingValue = false;
    String pendingCommandId;
    String pendingCorrelationId;
    uint32_t minOutputIntervalMs = 300;
    uint32_t lastAppliedMs = 0;
    uint32_t lastAppliedDesiredVersion = 0;
  };
  struct DigitalInput {
    String key;
    uint8_t pin = 255;
    bool activeHigh = true;
    uint32_t debounceMs = 50;
    bool lastRaw = false;
    bool lastSent = false;
    uint32_t changedAt = 0;
  };

  String topic(const char* suffix) const;
  String datastreamTopic(const char* key, const char* suffix) const;
  String heartbeatPayload() const;
  bool publishHeartbeat();
  bool reconnect();
  String jsonValue(const char* key, const String& payload) const;
  String jsonScalar(const String& value) const;
  bool datastreamAllowed(const char* key) const;
  bool handleMappedWrite(const String& commandId, const String& correlationId, const String& key, const String& value, const String& desiredVersion);
  void applyDigitalOutput(DigitalOutput& output, bool value);
  void ackDigitalOutput(const DigitalOutput& output, const String& commandId, const String& correlationId);
  void flushDigitalOutputs();
  void pollDigitalInputs();

  FlovaTransport& transport_;
  FlovaStorage& storage_;
  FlovaClock& clock_;
  FlovaLogger& logger_;
  FlovaConfig config_;
  Handler handlers_[8];
  uint8_t handlerCount_ = 0;
  DigitalOutput outputs_[8];
  uint8_t outputCount_ = 0;
  DigitalInput inputs_[8];
  uint8_t inputCount_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t lastReconnectMs_ = 0;
  uint32_t reconnectDelayMs_ = 1000;
  uint8_t statusLedPin_ = 255;
  bool statusLedActiveLow_ = false;
  uint8_t resetButtonPin_ = 255;
  bool resetButtonActiveLow_ = false;
  uint32_t resetHoldMs_ = 10000;
  uint32_t resetStartedMs_ = 0;
};
