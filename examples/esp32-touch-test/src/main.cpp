#include <Arduino.h>
#include <FlovaArduino.h>
#include <FlovaEsp32.h>
#include <FlovaEsp32Provisioning.h>
#include <FlovaEsp32Services.h>
#include <adapters/ArduinoFlovaLink.h>

// ESP32 counterpart of the explicit ESP8266 touch/LED offline test.
//
// Hardware used by this example:
//   - external LED (with resistor)            -> D2 / GPIO2
//   - capacitive touch module digital output -> D4 / GPIO4
//
// This test owns the application GPIO behavior while the ESP32 provisioning
// adapter owns the temporary SoftAP and its local HTTP setup server.

namespace {

const uint8_t TOUCH_PIN = 4;
const uint8_t LED_PIN = 2;
const uint8_t TOUCH_ACTIVE_LEVEL = HIGH;
const uint8_t LED_ON_LEVEL = HIGH;
const uint8_t LED_OFF_LEVEL = LOW;

const char* LED_DATASTREAM_KEY = "LED";
const char* TOUCH_DATASTREAM_KEY = "TOUCH_SENSOR";
const uint32_t TOUCH_DEBOUNCE_MS = 60;

class TouchTestHardware final : public flova::Hardware {
 public:
  void attach(flova::Device&) override {}
  bool validate(const flova::config::Unit&) override { return true; }
  bool apply(const flova::config::Unit&) override { return true; }
  void run() override {}
  void setConnected(bool) override {}

  void failSafe() override { digitalWrite(LED_PIN, LED_OFF_LEVEL); }
};

FlovaEsp32Entropy entropy;
ArduinoFlovaLink link(entropy);
FlovaEsp32Storage storage;
ArduinoFlovaClock clockSource;
ArduinoFlovaLogger logger;
FlovaEsp32Provisioning provisioning(storage);
TouchTestHardware hardware;
FlovaClient client(link, provisioning, storage, clockSource, logger, entropy,
                  hardware);

flova::Datastream<bool> led =
    client.device().datastream<bool>(LED_DATASTREAM_KEY);
flova::Datastream<bool> touch =
    client.device().datastream<bool>(TOUCH_DATASTREAM_KEY);

bool touchInitialized = false;
bool rawTouch = false;
bool stableTouch = false;
uint32_t rawChangedAt = 0;

flova::WriteResult writeLed(void*, bool enabled) {
  digitalWrite(LED_PIN, enabled ? LED_ON_LEVEL : LED_OFF_LEVEL);
  return flova::WriteResult::accept();
}

bool readTouch() {
  return digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
}

void pollTouch() {
  const uint32_t now = millis();
  const bool currentRaw = readTouch();

  if (!touchInitialized) {
    touchInitialized = true;
    rawTouch = currentRaw;
    stableTouch = currentRaw;
    rawChangedAt = now;
    touch.report(stableTouch, flova::Origin::PhysicalInput);
    return;
  }

  if (currentRaw != rawTouch) {
    rawTouch = currentRaw;
    rawChangedAt = now;
  }

  if (rawTouch == stableTouch || now - rawChangedAt < TOUCH_DEBOUNCE_MS)
    return;

  stableTouch = rawTouch;
  touch.report(stableTouch, flova::Origin::PhysicalInput);
  if (!stableTouch) return;

  const bool nextValue = !led.hasValue() || !led.value();
  const flova::WriteResult result = led.write(nextValue);
  Serial.printf("[touch] LED=%u result=%s\n", nextValue ? 1U : 0U,
                result.accepted() ? "accepted" : result.reason);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp32 touch test boot");

  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF_LEVEL);

  led.onWrite(writeLed, nullptr);
  led.offline(flova::OfflinePolicy::KeepLatest);
  touch.offline(flova::OfflinePolicy::KeepLatest);

  client.setFirmwareTarget("touch-test-esp32");
  if (!client.begin(true)) {
    Serial.println("[flova] touch test startup failed");
    return;
  }

  Serial.println("[flova] touch test ready");
  Serial.println("[flova] touch: D4 / GPIO4");
  Serial.println("[flova] LED: D2 / GPIO2");
  Serial.printf("[flova] datastreams: led=%s touch=%s\n",
                LED_DATASTREAM_KEY, TOUCH_DATASTREAM_KEY);
}

void loop() {
  client.run();
  pollTouch();
  yield();
}
