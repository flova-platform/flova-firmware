#pragma once

// Small opt-in helpers for custom application code. They keep the typed
// Datastream API and its text-based Console keys visible while reducing the
// amount of C++ syntax beginners need to learn.
#include "FlovaDevice.h"

#include <stdint.h>

struct FlovaDatastreamCommand {
  const char* key;
  const char* commandId;
  uint32_t desiredVersion;
  flova::Value value;
};

typedef flova::WriteResult (*FlovaDatastreamCommandHandler)(
    void*, const FlovaDatastreamCommand&);

enum class FlovaDatastreamHandlerKind : uint8_t {
  None,
  Result,
  ResultWithContext,
  Void,
  VoidWithContext,
};

template <typename T>
struct FlovaDatastreamType;

template <> struct FlovaDatastreamType<bool> {
  static flova::ValueType value() { return flova::ValueType::Boolean; }
};
template <> struct FlovaDatastreamType<int64_t> {
  static flova::ValueType value() { return flova::ValueType::Int64; }
};
template <> struct FlovaDatastreamType<float> {
  static flova::ValueType value() { return flova::ValueType::Float; }
};
template <> struct FlovaDatastreamType<double> {
  static flova::ValueType value() { return flova::ValueType::Double; }
};
template <> struct FlovaDatastreamType<flova::Text> {
  static flova::ValueType value() { return flova::ValueType::Text; }
};

template <typename T>
struct FlovaDatastreamBinding {
  typedef flova::WriteResult (*ResultHandler)(T);
  typedef flova::WriteResult (*ResultContextHandler)(void*, T);
  typedef void (*VoidHandler)(T);
  typedef void (*VoidContextHandler)(void*, T);

  FlovaDatastreamHandlerKind kind = FlovaDatastreamHandlerKind::None;
  union {
    ResultHandler result;
    ResultContextHandler resultContext;
    VoidHandler voidHandler;
    VoidContextHandler voidContext;
  } handler = {};
  void* context = nullptr;

  static flova::WriteResult dispatch(void* context,
                                     const FlovaDatastreamCommand& command) {
    FlovaDatastreamBinding& binding = *static_cast<FlovaDatastreamBinding*>(context);
    if (command.value.type != FlovaDatastreamType<T>::value())
      return flova::WriteResult::reject("type_mismatch");

    const T value = flova::Codec<T>::decode(command.value);
    switch (binding.kind) {
      case FlovaDatastreamHandlerKind::Result:
        return binding.handler.result ? binding.handler.result(value)
                                      : flova::WriteResult::failure("write_handler_missing");
      case FlovaDatastreamHandlerKind::ResultWithContext:
        return binding.handler.resultContext
                   ? binding.handler.resultContext(binding.context, value)
                   : flova::WriteResult::failure("write_handler_missing");
      case FlovaDatastreamHandlerKind::Void:
        if (binding.handler.voidHandler) binding.handler.voidHandler(value);
        return binding.handler.voidHandler ? flova::WriteResult::accept()
                                           : flova::WriteResult::failure("write_handler_missing");
      case FlovaDatastreamHandlerKind::VoidWithContext:
        if (binding.handler.voidContext) binding.handler.voidContext(binding.context, value);
        return binding.handler.voidContext ? flova::WriteResult::accept()
                                           : flova::WriteResult::failure("write_handler_missing");
      default:
        return flova::WriteResult::failure("write_handler_missing");
    }
  }
};

template <typename T, typename Client>
class FlovaDatastream {
 public:
  FlovaDatastream(Client& client, const char* key) : client_(client), key_(key) {}

  const char* key() const { return key_; }
  flova::WriteResult write(const T& value) { return client_.flovaWrite(key_, value); }
  bool hasValue() const { return client_.template flovaHasValue<T>(key_); }
  T value() const { return client_.template flovaValue<T>(key_); }
  flova::WriteResult report(const T& value,
                            flova::Origin origin = flova::Origin::SensorRead) {
    return client_.flovaReport(key_, value, origin);
  }
  FlovaDatastream& mode(flova::Mode value) {
    client_.template flovaMode<T>(key_, value);
    return *this;
  }
  FlovaDatastream& offline(flova::OfflinePolicy value) {
    client_.template flovaOffline<T>(key_, value);
    return *this;
  }
  FlovaDatastream& retention(const flova::HistoryRetentionPolicy& value) {
    client_.template flovaRetention<T>(key_, value);
    return *this;
  }
  FlovaDatastream& persist(flova::PersistencePolicy value) {
    client_.template flovaPersist<T>(key_, value);
    return *this;
  }

  FlovaDatastream& onWrite(typename FlovaDatastreamBinding<T>::ResultHandler handler) {
    binding_.kind = FlovaDatastreamHandlerKind::Result;
    binding_.handler.result = handler;
    client_.flovaOnWrite(key_, FlovaDatastreamType<T>::value(), binding_);
    return *this;
  }
  FlovaDatastream& onWrite(typename FlovaDatastreamBinding<T>::ResultContextHandler handler,
                          void* context) {
    binding_.kind = FlovaDatastreamHandlerKind::ResultWithContext;
    binding_.handler.resultContext = handler;
    binding_.context = context;
    client_.flovaOnWrite(key_, FlovaDatastreamType<T>::value(), binding_);
    return *this;
  }
  FlovaDatastream& onWrite(typename FlovaDatastreamBinding<T>::VoidHandler handler) {
    binding_.kind = FlovaDatastreamHandlerKind::Void;
    binding_.handler.voidHandler = handler;
    client_.flovaOnWrite(key_, FlovaDatastreamType<T>::value(), binding_);
    return *this;
  }
  FlovaDatastream& onWrite(typename FlovaDatastreamBinding<T>::VoidContextHandler handler,
                          void* context) {
    binding_.kind = FlovaDatastreamHandlerKind::VoidWithContext;
    binding_.handler.voidContext = handler;
    binding_.context = context;
    client_.flovaOnWrite(key_, FlovaDatastreamType<T>::value(), binding_);
    return *this;
  }

 private:
  Client& client_;
  const char* key_;
  FlovaDatastreamBinding<T> binding_;
};

#define FLOVA_DATASTREAM(client, type, key) ((client).stream<type>(key))
#define FLOVA_WRITE(stream, value) ((stream).write(value))
#define FLOVA_READ(stream) ((stream).value())
#define FLOVA_REPORT(stream, ...) ((stream).report(__VA_ARGS__))
#define FLOVA_ON_WRITE(stream, ...) ((stream).onWrite(__VA_ARGS__))
#define FLOVA_HAS_VALUE(stream) ((stream).hasValue())
#define FLOVA_SETTING(client, type, key, default_value) ((client).setting<type>(key, default_value))
