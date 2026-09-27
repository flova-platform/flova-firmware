#pragma once

#include <new>
#include <utility>

// One fixed-size object per lifecycle owner. Allocation is restricted to phase
// entry; neither payload lengths nor registry counts determine its size.
// reset() must run only after all references borrowed in that phase return.
template <typename T> class FlovaPhaseStorage {
 public:
  FlovaPhaseStorage() = default;
  ~FlovaPhaseStorage() { reset(); }
  FlovaPhaseStorage(const FlovaPhaseStorage&) = delete;
  FlovaPhaseStorage& operator=(const FlovaPhaseStorage&) = delete;
  template <typename... Args> bool create(Args&&... args) {
    if (!value_) value_ = new (std::nothrow) T(std::forward<Args>(args)...);
    return value_ != nullptr;
  }
  void reset() { delete value_; value_ = nullptr; }
  explicit operator bool() const { return value_ != nullptr; }
  T* operator->() { return value_; }
  const T* operator->() const { return value_; }
 private:
  T* value_ = nullptr;
};
