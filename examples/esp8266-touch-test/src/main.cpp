#include <Arduino.h>
#include <FlovaArduino.h>
#include <FlovaEsp8266.h>
#include <FlovaEsp8266Provisioning.h>
#include <FlovaEsp8266Services.h>
#include <adapters/ArduinoFlovaLink.h>

// -----------------------------------------------------------------------------
// Flova ESP8266 offline touch/LED test
//
// Hardware used by this example:
//   - capacitive touch module digital output -> D1 / GPIO5
//   - external LED (with resistor)            -> D2 / GPIO4
//
// This is a test composition, not the no-code universal composition. It still
// owns the SoftAP provisioning lifecycle so the mobile Wi-Fi provisioning flow
// can be tested, but it deliberately owns the D1/D2 application behavior here.
// Template hardware mappings are accepted and ignored by TouchTestHardware;
// therefore GPIO2/GPIO16 mappings from the seeded demo cannot silently change
// this test's wiring.
// -----------------------------------------------------------------------------

namespace {

const uint8_t TOUCH_PIN = D1;
const uint8_t LED_PIN = D2;

// Most TTP223-style modules drive HIGH while touched. Change this value to LOW
// for an active-low module; no SDK or protocol change is required.
const uint8_t TOUCH_ACTIVE_LEVEL = HIGH;

// D2 is assumed to drive an external LED from GPIO to GND. Change these two
// values if the LED circuit is active-low.
const uint8_t LED_ON_LEVEL = HIGH;
const uint8_t LED_OFF_LEVEL = LOW;

// These are the exact keys used by the device template:
//   LED         = read/write boolean
//   TOUCH_SENSOR = read-only boolean
// If your own template uses different keys, change only these two strings to
// match the Engine datastream keys exactly.
const char* LED_DATASTREAM_KEY = "LED";
const char* TOUCH_DATASTREAM_KEY = "TOUCH_SENSOR";

const uint32_t TOUCH_DEBOUNCE_MS = 60;

class TouchTestHardware final : public flova::Hardware {
 public:
  void attach(flova::Device&) override {}

  // This test handles D1/D2 in application code. Returning true lets the
  // Engine configuration contain mappings without making those mappings own
  // the pins or replacing the LED onWrite() callback.
  bool validate(const flova::config::Unit&) override { return true; }
  bool apply(const flova::config::Unit&) override { return true; }
  void run() override {}
  void setConnected(bool) override {}

  void failSafe() override {
    // Leave the physical output in a known safe state if configuration fails.
    digitalWrite(LED_PIN, LED_OFF_LEVEL);
  }
};

// Board services. The provisioning adapter owns the temporary SoftAP and its
// local HTTP setup server. The application does not need to hardcode device
// ID, device secret, Wi-Fi password, or Engine token.
FlovaEsp8266Entropy entropy;
ArduinoFlovaLink link(entropy);
FlovaEsp8266Storage storage;
ArduinoFlovaClock clockSource;
ArduinoFlovaLogger logger;
FlovaEsp8266Provisioning provisioning(storage);
TouchTestHardware hardware;
FlovaClient client(link, provisioning, storage, clockSource, logger, entropy,
                  hardware);

// These are the two datastreams exposed to the mobile app and Engine:
//   - LED receives mobile/automation writes and reports the applied state.
//   - Touch Sensor reports physical input and is not written by this device.
flova::Datastream<bool> led =
    client.device().datastream<bool>(LED_DATASTREAM_KEY);
flova::Datastream<bool> touch =
    client.device().datastream<bool>(TOUCH_DATASTREAM_KEY);

// Debounce state is fixed-size and independent of network connectivity.
bool touchInitialized = false;
bool rawTouch = false;
bool stableTouch = false;
uint32_t rawChangedAt = 0;

flova::WriteResult writeLed(void*, bool enabled) {
  // This callback is shared by every remote writer: the mobile app, an Engine
  // schedule, and an Engine automation. It is also the same hardware path used
  // by local led.write() calls below.
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

    // Publish the initial physical observation. KeepLatest below means this
    // remains useful even if Wi-Fi is not connected yet.
    touch.report(stableTouch, flova::Origin::PhysicalInput);
    return;
  }

  if (currentRaw != rawTouch) {
    rawTouch = currentRaw;
    rawChangedAt = now;
  }

  // Wait for the input to remain unchanged before accepting a touch edge.
  if (rawTouch == stableTouch || now - rawChangedAt < TOUCH_DEBOUNCE_MS)
    return;

  stableTouch = rawTouch;
  touch.report(stableTouch, flova::Origin::PhysicalInput);

  // Toggle only on the inactive -> touched transition. Holding the sensor
  // does not repeatedly toggle the LED.
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
  Serial.println("[flova] esp8266 touch test boot");

  // The external touch module has its own output driver. INPUT is correct for
  // the common TTP223 module; use INPUT_PULLUP only for a floating open-drain
  // sensor output.
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF_LEVEL);

  // All mobile/user/automation/schedule LED writes use this callback. The
  // callback is registered before begin() so it exists before Link can deliver
  // a command after binding.
  led.onWrite(writeLed, nullptr);

  // KeepLatest is the important offline policy for this test:
  //   - a touch can toggle LED while Wi-Fi is unavailable;
  //   - the latest LED state is retained in the SDK's bounded state;
  //   - once Link reconnects, the state is sent to Engine/mobile.
  // StoreHistory is not needed because the test cares about the final state,
  // not every intermediate toggle.
  led.offline(flova::OfflinePolicy::KeepLatest);
  touch.offline(flova::OfflinePolicy::KeepLatest);

  // The provisioning adapter starts SoftAP mode automatically when the
  // Flova configuration is absent. The mobile app sends Wi-Fi credentials,
  // Link URL, and short-lived provisioning token through that setup flow.
  client.setFirmwareTarget("touch-test-esp8266");
  if (!client.begin(true)) {
    Serial.println("[flova] touch test startup failed");
    return;
  }

  Serial.println("[flova] touch test ready");
  Serial.println("[flova] touch: D1 / GPIO5");
  Serial.println("[flova] LED: D2 / GPIO4");
  Serial.printf("[flova] datastreams: led=%s touch=%s\n",
                LED_DATASTREAM_KEY, TOUCH_DATASTREAM_KEY);
}

void loop() {
  // This processes SoftAP HTTP during provisioning, Device Link messages,
  // remote writes, reconnects, and outbound state. Keep it frequent.
  client.run();

  // The local touch behavior remains active in setup mode, offline mode, and
  // runtime mode. It always uses led.write(), so it follows the same type,
  // safety, cache, revision, and offline-delivery semantics as remote writes.
  pollTouch();

  yield();
}
