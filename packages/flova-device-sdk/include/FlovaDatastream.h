#pragma once

template <typename T> struct FlovaDomainValueAdapter;
template <> struct FlovaDomainValueAdapter<bool> {
  static constexpr FlovaValueType type = FlovaValueType::Bool;
  static String encode(bool value) { return value ? "true" : "false"; }
  static bool decode(const String& value) { return value == "true" || value == "1"; }
};
template <> struct FlovaDomainValueAdapter<float> {
  static constexpr FlovaValueType type = FlovaValueType::Float;
  static String encode(float value) { return String(value, 6); }
  static float decode(const String& value) { return value.toFloat(); }
};
template <> struct FlovaDomainValueAdapter<double> {
  static constexpr FlovaValueType type = FlovaValueType::Number;
  static String encode(double value) { return String(value, 6); }
  static double decode(const String& value) { return value.toDouble(); }
};
template <> struct FlovaDomainValueAdapter<String> {
  static constexpr FlovaValueType type = FlovaValueType::String;
  static String encode(const String& value) { return value; }
  static String decode(const String& value) { return value; }
};
template <typename T> class FlovaDevice::Datastream {
 public:
  typedef FlovaWriteResult (*WriteHandler)(T);
  typedef FlovaReadResult<T> (*ReadHandler)();
  Datastream(FlovaDevice& device, FlovaDevice::DatastreamState* state) : device_(device), state_(state) {}
  T read() const { return state_ && state_->hasValue ? FlovaDomainValueAdapter<T>::decode(state_->value) : T{}; }
  bool hasValue() const { return state_ && state_->hasValue; }
  FlovaDatastreamSnapshot<T> snapshot() const {
    FlovaDatastreamSnapshot<T> out; out.hasValue = hasValue(); out.value = out.hasValue ? FlovaDomainValueAdapter<T>::decode(state_->value) : T{};
    if (state_) { out.updatedAt = state_->updatedAt; out.origin = state_->origin; out.quality = state_->quality; out.dirty = state_->dirty; out.revision = state_->revision; }
    out.stale = out.quality == FlovaValueQuality::Stale; return out;
  }
  FlovaWriteResult write(const T& value) { return state_ ? device_.applyWriteState(*state_, FlovaDomainValueAdapter<T>::encode(value), FlovaDomainValueAdapter<T>::type, FlovaValueOrigin::LocalLogic) : FlovaWriteResult::reject("datastream_unresolved"); }
  FlovaWriteResult report(const T& value) { return state_ ? device_.reportValueState(*state_, FlovaDomainValueAdapter<T>::encode(value), FlovaDomainValueAdapter<T>::type, FlovaValueOrigin::SensorRead) : FlovaWriteResult::reject("datastream_unresolved"); }
  FlovaReadResult<T> refresh() {
    if (!readHandler_) return FlovaReadResult<T>::error("read_handler_missing");
    FlovaReadResult<T> result = readHandler_(); if (!result.ok || !state_) return result;
    FlovaWriteResult reported = device_.reportValueState(*state_, FlovaDomainValueAdapter<T>::encode(result.value), FlovaDomainValueAdapter<T>::type, FlovaValueOrigin::SensorRead);
    return reported.accepted() ? result : FlovaReadResult<T>::error(reported.reasonCode);
  }
  Datastream& onWrite(WriteHandler handler) { writeHandler_ = handler; if (state_) state_->writeHandler = reinterpret_cast<void*>(handler); return *this; }
  Datastream& onRead(ReadHandler handler) { readHandler_ = handler; if (state_) state_->readHandler = reinterpret_cast<void*>(handler); return *this; }
  Datastream& mode(FlovaDatastreamMode value) { if (state_) state_->mode = value; return *this; }
  Datastream& offline(FlovaOfflinePolicy value) { if (state_) state_->offline = value; return *this; }
  Datastream& persist(FlovaPersistencePolicy value) { if (state_) state_->persistence = value; return *this; }
  Datastream& restore(FlovaRestorePolicy value) { if (state_) state_->restore = value; return *this; }
  Datastream& publishEvery(uint32_t milliseconds) { if (state_) state_->minimumPublishIntervalMs = milliseconds; return *this; }
 private:
  FlovaDevice& device_; FlovaDevice::DatastreamState* state_; WriteHandler writeHandler_ = nullptr; ReadHandler readHandler_ = nullptr;
};

template <typename T> FlovaDevice::Datastream<T> FlovaDevice::datastream(const char* key) {
  DatastreamState* state = stateForKey(key, FlovaDomainValueAdapter<T>::type, true); if (state && !state->hasValue) state->type = FlovaDomainValueAdapter<T>::type;
  return Datastream<T>(*this, state);
}
