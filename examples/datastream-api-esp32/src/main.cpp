#include <Arduino.h>
#include <ArduinoJson.h>
#include <FlovaEsp32.h>

FlovaEsp32 device;
auto temperature = device.datastream<float>("temperature").mode(FlovaDatastreamMode::Sample).offline(FlovaOfflinePolicy::Drop);
auto relay = device.datastream<bool>("relay").persist(FlovaPersistencePolicy::Persistent);
auto cookProgram = device.objectDatastream("cook_program").mode(FlovaDatastreamMode::Command).offline(FlovaOfflinePolicy::Reject);

static FlovaReadResult<float> readTemperature() { return FlovaReadResult<float>::success(25.0f); }
static FlovaWriteResult writeRelay(bool enabled) {
  if (enabled && digitalRead(34) == LOW) return FlovaWriteResult::reject("insufficient_water");
  digitalWrite(2, enabled ? HIGH : LOW);
  return FlovaWriteResult::accept();
}
static FlovaWriteResult startCookProgram(FlovaObjectValue program) {
  if (!program.commandId.length()) return FlovaWriteResult::reject("command_id_required");
  DynamicJsonDocument json(1024);
  if (deserializeJson(json, program.json)) return FlovaWriteResult::reject("invalid_json");
  const char* operation = json["operation"] | "start";
  if (!strcmp(operation, "pause") || !strcmp(operation, "resume") || !strcmp(operation, "cancel")) {
    // target_command_id is optional only when firmware has one unambiguous active program.
    return FlovaWriteResult::accept();
  }
  if (strcmp(operation, "start") && strcmp(operation, "replace")) return FlovaWriteResult::reject("operation_not_supported");
  JsonArray stages = json["stages"].as<JsonArray>();
  if (!stages.size()) return FlovaWriteResult::reject("stages_required");
  for (JsonObject stage : stages) {
    int temperature = stage["temperature_c"] | 0;
    int duration = stage["duration_seconds"] | 0;
    if (temperature < 40 || temperature > 250 || duration < 1) return FlovaWriteResult::reject("unsafe_stage");
  }
  // Persist the accepted program before returning so execution survives disconnects/reboots.
  return FlovaWriteResult::accept();
}

void setup() {
  pinMode(2, OUTPUT);
  pinMode(34, INPUT);
  temperature.onRead(readTemperature);
  relay.onWrite(writeRelay);
  cookProgram.onWrite(startCookProgram);
  device.begin();
}

void loop() {
  device.loop();
  temperature.refresh();
  if (temperature.read() > 30.0f) relay.write(false);
}
