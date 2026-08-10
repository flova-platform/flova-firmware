#include <Arduino.h>
#include <FlovaEsp32.h>

FlovaEsp32 device;
auto temperature = device.datastream<float>("temperature").mode(FlovaDatastreamMode::Sample).offline(FlovaOfflinePolicy::Drop);
auto relay = device.datastream<bool>("relay").persist(FlovaPersistencePolicy::Persistent);
auto cookMode = device.datastream<String>("cook_mode").mode(FlovaDatastreamMode::Command).offline(FlovaOfflinePolicy::Reject);

static FlovaReadResult<float> readTemperature() { return FlovaReadResult<float>::success(25.0f); }
static FlovaWriteResult writeRelay(bool enabled) {
  if (enabled && digitalRead(34) == LOW) return FlovaWriteResult::reject("insufficient_water");
  digitalWrite(2, enabled ? HIGH : LOW);
  return FlovaWriteResult::accept();
}
static FlovaWriteResult setCookMode(String mode) {
  return mode == "start" || mode == "pause" || mode == "resume" || mode == "cancel"
             ? FlovaWriteResult::accept()
             : FlovaWriteResult::reject("mode_not_supported");
}

void setup() {
  pinMode(2, OUTPUT);
  pinMode(34, INPUT);
  temperature.onRead(readTemperature);
  relay.onWrite(writeRelay);
  cookMode.onWrite(setCookMode);
  device.begin();
}

void loop() {
  device.loop();
  temperature.refresh();
  if (temperature.read() > 30.0f) relay.write(false);
}
