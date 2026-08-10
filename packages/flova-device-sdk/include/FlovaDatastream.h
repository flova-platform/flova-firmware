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
  Datastream(FlovaDevice& device, const char* key) : device_(device), key_(key) {}
  T read() const { String value; return device_.readCached(key_.c_str(), value) ? FlovaDomainValueAdapter<T>::decode(value) : T{}; }
  bool hasValue() const { return device_.hasValue(key_.c_str()); }
  FlovaDatastreamSnapshot<T> snapshot() const {
    FlovaDatastreamSnapshot<T> out; String value; uint32_t revision = 0;
    out.hasValue = device_.readCached(key_.c_str(), value, &revision); out.value = out.hasValue ? FlovaDomainValueAdapter<T>::decode(value) : T{}; out.revision = revision;
    device_.readSnapshotMetadata(key_.c_str(), out.updatedAt, out.origin, out.quality, out.dirty, out.revision); out.stale = out.quality == FlovaValueQuality::Stale; return out;
  }
  FlovaWriteResult write(const T& value) { return device_.applyWrite(key_.c_str(), FlovaDomainValueAdapter<T>::encode(value), FlovaDomainValueAdapter<T>::type); }
  FlovaWriteResult report(const T& value) { return device_.reportValue(key_.c_str(), FlovaDomainValueAdapter<T>::encode(value), FlovaDomainValueAdapter<T>::type); }
  FlovaReadResult<T> refresh() {
    if (!readHandler_) return FlovaReadResult<T>::error("read_handler_missing");
    FlovaReadResult<T> result = readHandler_(); if (!result.ok) return result;
    FlovaWriteResult reported = device_.reportValue(key_.c_str(), FlovaDomainValueAdapter<T>::encode(result.value), FlovaDomainValueAdapter<T>::type);
    return reported.accepted() ? result : FlovaReadResult<T>::error(reported.reasonCode);
  }
  Datastream& onWrite(WriteHandler handler) { writeHandler_ = handler; device_.registerTypedWrite(key_.c_str(), reinterpret_cast<void*>(handler), FlovaDomainValueAdapter<T>::type); return *this; }
  Datastream& onRead(ReadHandler handler) { readHandler_ = handler; device_.registerTypedRead(key_.c_str(), reinterpret_cast<void*>(handler), FlovaDomainValueAdapter<T>::type); return *this; }
  Datastream& mode(FlovaDatastreamMode value) { mode_ = value; sync(); return *this; }
  Datastream& offline(FlovaOfflinePolicy value) { offline_ = value; sync(); return *this; }
  Datastream& persist(FlovaPersistencePolicy value) { persistence_ = value; sync(); return *this; }
  Datastream& restore(FlovaRestorePolicy value) { restore_ = value; sync(); return *this; }
  Datastream& publishEvery(uint32_t milliseconds) { device_.configurePublishInterval(key_.c_str(), milliseconds); return *this; }
 private:
  void sync() { device_.configureDatastream(key_.c_str(), mode_, offline_, persistence_, restore_); }
  FlovaDevice& device_; String key_; WriteHandler writeHandler_ = nullptr; ReadHandler readHandler_ = nullptr;
  FlovaDatastreamMode mode_ = FlovaDatastreamMode::State; FlovaOfflinePolicy offline_ = FlovaOfflinePolicy::KeepLatest;
  FlovaPersistencePolicy persistence_ = FlovaPersistencePolicy::None; FlovaRestorePolicy restore_ = FlovaRestorePolicy::DoNotRestore;
};

template <typename T> FlovaDevice::Datastream<T> FlovaDevice::datastream(const char* key) {
  DatastreamState* state = stateFor(key, FlovaDomainValueAdapter<T>::type, true); if (state && !state->hasValue) state->type = FlovaDomainValueAdapter<T>::type;
  return Datastream<T>(*this, key);
}
