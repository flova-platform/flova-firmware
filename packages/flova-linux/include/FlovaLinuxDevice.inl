#pragma once

template <typename T>
flova::WriteResult FlovaLinux::flovaWrite(const char* key, const T& value) {
  return impl_->write(key, value);
}

template <typename T>
bool FlovaLinux::flovaHasValue(const char* key) const {
  return impl_->hasValue<T>(key);
}

template <typename T>
T FlovaLinux::flovaValue(const char* key) const {
  return impl_->value<T>(key);
}

template <typename T>
flova::WriteResult FlovaLinux::flovaReport(const char* key, const T& value,
                                           flova::Origin origin) {
  return impl_->report(key, value, origin);
}

template <typename T>
bool FlovaLinux::flovaOnWrite(const char* key, flova::ValueType valueType,
                              FlovaDatastreamBinding<T>& binding) {
  return impl_->onWrite(key, valueType, binding);
}
