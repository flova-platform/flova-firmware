#pragma once

#include <stddef.h>
#include <stdint.h>

#include <FlovaCustomCode.h>

class FlovaLinux {
 public:
  struct Config {
    const char* url;
    const char* deviceId;
    const char* secret;
    const char* stateDirectory;
  };

  FlovaLinux();
  explicit FlovaLinux(const Config& config);
  ~FlovaLinux();

  bool begin();
  void run();
  bool connected() const;
  bool ready() const;
  bool running() const;
  const char* lastError() const;

  template <typename T>
  FlovaDatastream<T, FlovaLinux> stream(const char* key) {
    return FlovaDatastream<T, FlovaLinux>(*this, key);
  }

  template <typename T>
  flova::WriteResult flovaWrite(const char* key, const T& value);

  template <typename T>
  bool flovaHasValue(const char* key) const;

  template <typename T>
  T flovaValue(const char* key) const;

  template <typename T>
  flova::WriteResult flovaReport(const char* key, const T& value,
                                 flova::Origin origin = flova::Origin::SensorRead);

  template <typename T>
  bool flovaOnWrite(const char* key, flova::ValueType valueType,
                    FlovaDatastreamBinding<T>& binding);

 private:
  class Impl {
   public:
    explicit Impl(const Config& config);
    ~Impl();
    bool begin();
    void run();
    bool connected() const;
    bool ready() const;
    bool running() const;
    const char* lastError() const;

    template <typename T>
    flova::WriteResult write(const char* key, const T& value);
    template <typename T>
    bool hasValue(const char* key) const;
    template <typename T>
    T value(const char* key) const;
    template <typename T>
    flova::WriteResult report(const char* key, const T& value,
                              flova::Origin origin);
    template <typename T>
    bool onWrite(const char* key, flova::ValueType valueType,
                 FlovaDatastreamBinding<T>& binding);

   private:
    struct State;
    State* state_;
  };
  Impl* impl_;
};

#include "FlovaLinuxDevice.inl"
