#include <assert.h>
#include <initializer_list>
#include <string.h>
#include <FlovaEsp32HardwarePolicy.h>
#include <FlovaEsp8266HardwarePolicy.h>
#include <adapters/ArduinoFlovaHardware.h>
#include <adapters/ArduinoFlovaManualHardware.h>

HardwareSerial Serial;
uint32_t millis() { return 0; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return 0; }
int analogRead(uint8_t) { return 0; }
void analogWrite(uint8_t, int) {}

int main() {
  uint16_t pin = 65535;
  uint8_t mode = 255;
  for (const char* reference : {"A0", "ADC0", "TOUT"}) {
    assert(flova::esp8266::resolvePin(reference, pin) && pin == A0);
    assert(flova::esp8266::validAnalogPin(pin));
    assert(!flova::esp8266::validDigitalPin(pin));
  }
  assert(flova::esp8266::resolvePin("GPIO0", pin) && pin == 0);
  assert(!flova::esp8266::validAnalogPin(pin));
  assert(flova::esp8266::inputMode(16, 2, mode) && mode == INPUT_PULLDOWN_16);
  assert(!flova::esp8266::inputMode(16, 1, mode));
  assert(!flova::esp8266::inputMode(5, 2, mode));
  assert(flova::esp32::resolvePin("ADC1_CH4", pin) && pin == 32);
  assert(flova::esp32::validAnalogPin(pin));
  assert(flova::esp32::resolvePin("ADC2_CH0", pin) && pin == 4);
  assert(!flova::esp32::validAnalogPin(pin)); // Wi-Fi owns ADC2.
  assert(!flova::esp32::resolvePin("A0", pin)); // No board-specific alias guessed.
  for (uint16_t invalid : {6, 11, 20, 24, 28, 31, 40, 255})
    assert(!flova::esp32::validInputPin(invalid));
  assert(!flova::esp32::validOutputPin(34));
  assert(flova::esp32::inputMode(34, 0, mode) && mode == INPUT);
  assert(!flova::esp32::inputMode(34, 1, mode));
  assert(flova::esp32::inputMode(32, 2, mode) && mode == INPUT_PULLDOWN);
  for (const char* invalid : {"GPIO", "GPIO-1", "GPIO65536", "GPIO1x", "UNKNOWN", "ADC1_CH8"})
    assert(!flova::esp32::resolvePin(invalid, pin));

  ArduinoFlovaHardware hardware(flova::esp8266::validDigitalPin,
      flova::esp8266::validDigitalPin, flova::esp8266::validAnalogPin,
      flova::esp8266::resolvePin, flova::esp8266::inputMode);
  flova::config::Unit unit = {};
  unit.kind = flova::config::UnitKind::Datastream;
  unit.data.datastream.hasMapping = true;
  unit.data.datastream.valueType = 2;
  unit.data.datastream.mapping.kind = flova::config::MappingKind::AnalogInput;
  strcpy(unit.data.datastream.mapping.pinReference, "TOUT");
  assert(hardware.resolve(unit) && unit.data.datastream.mapping.pin == A0);
  assert(hardware.validate(unit));
  unit.data.datastream.mapping.kind = flova::config::MappingKind::DigitalOutput;
  assert(!hardware.validate(unit));
  assert(!hardware.validateInputMode(16, 1));

  unit = {};
  unit.kind = flova::config::UnitKind::Datastream;
  assert(hardware.resolve(unit) && hardware.validate(unit));

  // A custom SDK application never resolves, rejects, or applies user-owned pins.
  ArduinoFlovaManualHardware manual;
  strcpy(unit.data.datastream.mapping.pinReference, "CUSTOM_PERIPHERAL");
  assert(!manual.capabilities().automaticMapping);
  assert(manual.resolve(unit) && manual.validate(unit) && manual.apply(unit));
}
