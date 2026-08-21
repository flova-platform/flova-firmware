#pragma once

#include <Arduino.h>

#include <FlovaDevice.h>
#include <FlovaHardware.h>

class ArduinoFlovaHardware final : public flova::Hardware {
 public:
  flova::HardwareCapabilities capabilities() const override {
    return flova::HardwareCapabilities(
        true, FLOVA_HARDWARE_INPUT_CAPACITY, FLOVA_HARDWARE_OUTPUT_CAPACITY,
        true);
  }

  void attach(flova::Device& device) override { device_ = &device; }

  bool validate(const flova::config::Unit& unit) override {
    configurationError_[0] = 0;
    if (unit.kind == flova::config::UnitKind::System) {
      if (!unit.data.system.hasStatusLedPin ||
          validOutputPin(unit.data.system.statusLedPin))
        return true;
      return reject("hardware_pin_invalid");
    }
    if (unit.kind != flova::config::UnitKind::Datastream ||
        !unit.data.datastream.hasMapping)
      return true;
    if (unit.data.datastream.mapping.pin > 255)
      return reject("hardware_pin_invalid");
    flova::ValueType type;
    if (!valueType(unit.data.datastream.valueType, type))
      return reject("hardware_type_mismatch");
    const flova::config::MappingKind kind =
        unit.data.datastream.mapping.kind;
    if (kind == flova::config::MappingKind::DigitalInput) {
      if (type != flova::ValueType::Boolean ||
          !validInputPin(unit.data.datastream.mapping.pin))
        return reject(type != flova::ValueType::Boolean
                          ? "hardware_type_mismatch"
                          : "hardware_pin_invalid");
      return true;
    }
    if (kind == flova::config::MappingKind::DigitalOutput) {
      if (type != flova::ValueType::Boolean ||
          !validOutputPin(unit.data.datastream.mapping.pin))
        return reject(type != flova::ValueType::Boolean
                          ? "hardware_type_mismatch"
                          : "hardware_pin_invalid");
      return true;
    }
    if (kind == flova::config::MappingKind::AnalogInput) {
      if (!numeric(type) || !validAnalogPin(unit.data.datastream.mapping.pin))
        return reject(!numeric(type) ? "hardware_type_mismatch"
                                     : "hardware_pin_invalid");
      const double minimum = number(unit.data.datastream.minimum, 0);
      const double maximum = number(unit.data.datastream.maximum, 100);
      return minimum < maximum ? true : reject("hardware_range_invalid");
    }
    if (kind == flova::config::MappingKind::PwmOutput) {
      if (!numeric(type) || !validOutputPin(unit.data.datastream.mapping.pin))
        return reject(!numeric(type) ? "hardware_type_mismatch"
                                     : "hardware_pin_invalid");
      const double minimum = number(unit.data.datastream.minimum, 0);
      const double maximum = number(unit.data.datastream.maximum, 100);
      return minimum < maximum ? true : reject("hardware_range_invalid");
    }
    return reject("hardware_mapping_invalid");
  }

  bool apply(const flova::config::Unit& unit) override {
    if (!validate(unit)) return false;
    if (unit.kind == flova::config::UnitKind::System) {
      if (!unit.data.system.hasStatusLedPin) return true;
      if (!validOutputPin(unit.data.system.statusLedPin))
        return reject("hardware_pin_invalid");
      statusLedPin_ = unit.data.system.statusLedPin;
      statusLedActiveLow_ = unit.data.system.hasStatusLedActiveLow &&
                            unit.data.system.statusLedActiveLow;
      pinMode(statusLedPin_, OUTPUT);
      updateStatusLed();
      return true;
    }
    if (unit.kind != flova::config::UnitKind::Datastream ||
        !unit.data.datastream.hasMapping) {
      return true;
    }
    if (!device_) return reject("hardware_mapping_missing");
    Mapping* mapping = find(unit.data.datastream.id);
    if (!mapping) {
      if (mappingCount_ >= kMaximumMappings)
        return reject("hardware_mapping_capacity");
      mapping = &mappings_[mappingCount_++];
    }
    *mapping = Mapping();
    mapping->owner = this;
    mapping->id = unit.data.datastream.id;
    mapping->kind = unit.data.datastream.mapping.kind;
    if (!valueType(unit.data.datastream.valueType, mapping->valueType))
      return reject("hardware_type_mismatch");
    mapping->pin = static_cast<uint8_t>(unit.data.datastream.mapping.pin);
    mapping->activeHigh = !unit.data.datastream.mapping.hasActiveHigh ||
                          unit.data.datastream.mapping.activeHigh;
    mapping->debounceMs = unit.data.datastream.mapping.hasDebounceMs
                              ? unit.data.datastream.mapping.debounceMs
                              : 50;
    mapping->sampleMs = unit.data.datastream.mapping.hasSampleMs
                            ? unit.data.datastream.mapping.sampleMs
                            : 1000;
    mapping->minimumOutputMs =
        unit.data.datastream.mapping.hasMinimumOutputMs
            ? unit.data.datastream.mapping.minimumOutputMs
            : 300;
    mapping->minimum = number(unit.data.datastream.minimum, 0);
    mapping->maximum = number(unit.data.datastream.maximum, 100);
    if (mapping->minimum >= mapping->maximum)
      return reject("hardware_range_invalid");

    if (mapping->kind == flova::config::MappingKind::DigitalInput) {
      if (mapping->valueType != flova::ValueType::Boolean)
        return reject("hardware_type_mismatch");
      const uint8_t mode = unit.data.datastream.mapping.hasPull &&
                                   unit.data.datastream.mapping.pull == 1
                               ? INPUT_PULLUP
                               : INPUT;
      pinMode(mapping->pin, mode);
      mapping->lastRaw = readDigital(*mapping);
      mapping->changedAt = millis();
    } else if (mapping->kind == flova::config::MappingKind::AnalogInput) {
      if (!numeric(mapping->valueType))
        return reject("hardware_type_mismatch");
      pinMode(mapping->pin, INPUT);
    } else {
      if ((mapping->kind == flova::config::MappingKind::DigitalOutput &&
           mapping->valueType != flova::ValueType::Boolean) ||
          (mapping->kind == flova::config::MappingKind::PwmOutput &&
           !numeric(mapping->valueType)))
        return reject("hardware_type_mismatch");
      pinMode(mapping->pin, OUTPUT);
      if (!device_->setWriteHandler(mapping->id, writeMapped, mapping))
        return reject("hardware_mapping_missing");
    }

    if (unit.data.datastream.hasDefault &&
        (mapping->kind == flova::config::MappingKind::DigitalOutput ||
         mapping->kind == flova::config::MappingKind::PwmOutput)) {
      flova::Value initial;
      if (!value(unit.data.datastream.defaultValue, initial) ||
          !device_->write(mapping->id, initial, flova::Origin::DeviceRestore)
               .accepted()) {
        return reject("hardware_default_rejected");
      }
    }
    return true;
  }

  void resetConfiguration() override {
    if (device_) {
      for (size_t i = 0; i < mappingCount_; ++i)
        device_->clearWriteHandler(mappings_[i].id, writeMapped, &mappings_[i]);
    }
    failSafe();
    mappingCount_ = 0;
    statusLedPin_ = 255;
    statusLedActiveLow_ = false;
    configurationError_[0] = 0;
  }

  const char* configurationError() const override { return configurationError_; }

  void run() override {
    if (!device_) return;
    const uint32_t now = millis();
    for (size_t i = 0; i < mappingCount_; ++i) {
      Mapping& mapping = mappings_[i];
      if (mapping.kind == flova::config::MappingKind::DigitalInput) {
        const bool current = readDigital(mapping);
        if (current != mapping.lastRaw) {
          mapping.lastRaw = current;
          mapping.changedAt = now;
        }
        if ((!mapping.reported || current != mapping.lastReported) &&
            now - mapping.changedAt >= mapping.debounceMs &&
            device_->report(mapping.id, flova::Value::from(current),
                            flova::Origin::PhysicalInput)
                .accepted()) {
          mapping.reported = true;
          mapping.lastReported = current;
        }
      } else if (mapping.kind == flova::config::MappingKind::AnalogInput &&
                 (!mapping.sampled || now - mapping.lastSampleAt >= mapping.sampleMs)) {
        mapping.sampled = true;
        mapping.lastSampleAt = now;
        const int reading = analogRead(mapping.pin);
        const flova::Value reported = mapping.valueType == flova::ValueType::Int64
                                          ? flova::Value::from(static_cast<int64_t>(reading))
                                      : mapping.valueType == flova::ValueType::Float
                                          ? flova::Value::from(static_cast<float>(reading))
                                          : flova::Value::from(static_cast<double>(reading));
        device_->report(mapping.id, reported, flova::Origin::PhysicalInput);
      }
    }
  }

  void setConnected(bool connected) override {
    if (connected_ == connected) return;
    connected_ = connected;
    updateStatusLed();
  }

  void failSafe() override {
    for (size_t i = 0; i < mappingCount_; ++i) {
      const Mapping& mapping = mappings_[i];
      if (mapping.kind == flova::config::MappingKind::DigitalOutput)
        digitalWrite(mapping.pin, mapping.activeHigh ? LOW : HIGH);
      else if (mapping.kind == flova::config::MappingKind::PwmOutput)
        analogWrite(mapping.pin, 0);
    }
    connected_ = false;
    updateStatusLed();
  }

 private:
  static const size_t kMaximumMappings =
      FLOVA_HARDWARE_INPUT_CAPACITY + FLOVA_HARDWARE_OUTPUT_CAPACITY;

  struct Mapping {
    ArduinoFlovaHardware* owner = nullptr;
    DatastreamId id = FLOVA_INVALID_DATASTREAM_ID;
    flova::config::MappingKind kind =
        flova::config::MappingKind::DigitalInput;
    flova::ValueType valueType = flova::ValueType::Boolean;
    uint8_t pin = 255;
    bool activeHigh = true;
    bool lastRaw = false;
    bool lastReported = false;
    bool reported = false;
    bool sampled = false;
    uint32_t debounceMs = 50;
    uint32_t sampleMs = 1000;
    uint32_t minimumOutputMs = 300;
    uint32_t changedAt = 0;
    uint32_t lastSampleAt = 0;
    uint32_t lastOutputAt = 0;
    double minimum = 0;
    double maximum = 100;
  };

  static flova::WriteResult writeMapped(void* context,
                                         const flova::Value& input) {
    Mapping* mapping = static_cast<Mapping*>(context);
    if (!mapping || !mapping->owner) {
      return flova::WriteResult::failure("hardware_mapping_missing");
    }
    const uint32_t now = millis();
    if (mapping->lastOutputAt &&
        now - mapping->lastOutputAt < mapping->minimumOutputMs) {
      return flova::WriteResult::reject("output_rate_limited");
    }
    if (mapping->kind == flova::config::MappingKind::DigitalOutput) {
      if (input.type != flova::ValueType::Boolean)
        return flova::WriteResult::reject("hardware_type_mismatch");
      const bool level = mapping->activeHigh ? input.scalar.boolean
                                             : !input.scalar.boolean;
      digitalWrite(mapping->pin, level ? HIGH : LOW);
    } else if (mapping->kind == flova::config::MappingKind::PwmOutput) {
      if (input.type != flova::ValueType::Int64 &&
          input.type != flova::ValueType::Float &&
          input.type != flova::ValueType::Double) {
        return flova::WriteResult::reject("hardware_type_mismatch");
      }
      const double actual = input.type == flova::ValueType::Int64
                                ? static_cast<double>(input.scalar.integer)
                                : input.type == flova::ValueType::Float
                                      ? input.scalar.floating
                                      : input.scalar.number;
      if (actual < mapping->minimum || actual > mapping->maximum)
        return flova::WriteResult::reject("out_of_range");
      const double ratio = (actual - mapping->minimum) /
                           (mapping->maximum - mapping->minimum);
      analogWrite(mapping->pin, static_cast<int>(ratio * 255.0));
    } else {
      return flova::WriteResult::reject("hardware_not_writable");
    }
    mapping->lastOutputAt = now ? now : 1;
    return flova::WriteResult::accept();
  }

  static bool readDigital(const Mapping& mapping) {
    const bool high = digitalRead(mapping.pin) == HIGH;
    return mapping.activeHigh ? high : !high;
  }

  static double number(const flova::config::Value& input,
                       double fallback) {
    if (input.kind == flova::config::ValueKind::Int64)
      return static_cast<double>(input.data.integer);
    if (input.kind == flova::config::ValueKind::Float32)
      return input.data.float32;
    if (input.kind == flova::config::ValueKind::Float64)
      return input.data.float64;
    return fallback;
  }

  static bool value(const flova::config::Value& input,
                    flova::Value& output) {
    if (input.kind == flova::config::ValueKind::Boolean)
      output = flova::Value::from(input.data.boolean);
    else if (input.kind == flova::config::ValueKind::Int64)
      output = flova::Value::from(input.data.integer);
    else if (input.kind == flova::config::ValueKind::Float32)
      output = flova::Value::from(input.data.float32);
    else if (input.kind == flova::config::ValueKind::Float64)
      output = flova::Value::from(input.data.float64);
    else if (input.kind == flova::config::ValueKind::Text)
      output = flova::Value::from(input.data.text);
    else
      return false;
    return true;
  }

  static bool valueType(uint8_t input, flova::ValueType& output) {
    if (input == 0) output = flova::ValueType::Boolean;
    else if (input == 1) output = flova::ValueType::Int64;
    else if (input == 2) output = flova::ValueType::Float;
    else if (input == 3) output = flova::ValueType::Double;
    else if (input == 4) output = flova::ValueType::Text;
    else return false;
    return true;
  }

  static bool numeric(flova::ValueType type) {
    return type == flova::ValueType::Int64 || type == flova::ValueType::Float ||
           type == flova::ValueType::Double;
  }

  bool reject(const char* error) {
    strncpy(configurationError_, error, sizeof(configurationError_) - 1);
    configurationError_[sizeof(configurationError_) - 1] = 0;
    return false;
  }

  static bool validInputPin(uint16_t pin) {
#if defined(ESP32)
    return pin <= 39 && !(pin >= 6 && pin <= 11);
#elif defined(ESP8266)
    return pin == 0 || pin == 2 || pin == 4 || pin == 5 ||
           (pin >= 12 && pin <= 16);
#else
    return pin <= 255;
#endif
  }

  static bool validOutputPin(uint16_t pin) {
#if defined(ESP32)
    return pin <= 33 && !(pin >= 6 && pin <= 11);
#elif defined(ESP8266)
    return pin == 0 || pin == 2 || pin == 4 || pin == 5 ||
           (pin >= 12 && pin <= 16);
#else
    return pin <= 255;
#endif
  }

  static bool validAnalogPin(uint16_t pin) {
#if defined(ESP32)
    return pin >= 32 && pin <= 39;
#elif defined(ESP8266)
    return pin == A0;
#else
    return pin <= 255;
#endif
  }

  Mapping* find(DatastreamId id) {
    for (size_t i = 0; i < mappingCount_; ++i)
      if (mappings_[i].id == id) return &mappings_[i];
    return nullptr;
  }

  void updateStatusLed() {
    if (statusLedPin_ == 255) return;
    const bool level = statusLedActiveLow_ ? !connected_ : connected_;
    digitalWrite(statusLedPin_, level ? HIGH : LOW);
  }

  flova::Device* device_ = nullptr;
  Mapping mappings_[kMaximumMappings];
  size_t mappingCount_ = 0;
  uint8_t statusLedPin_ = 255;
  bool statusLedActiveLow_ = false;
  bool connected_ = false;
  char configurationError_[48] = {};
};
