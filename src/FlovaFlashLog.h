#pragma once

#include <FlovaLogging.h>

// Board packages may override literal access before including Arduino services.
#ifndef FLOVA_LOG_RAW
#define FLOVA_LOG_RAW(logger, literal) (logger).log(literal)
#endif
#ifndef FLOVA_FORMAT
#define FLOVA_FORMAT(out, size, literal, ...) snprintf(out, size, literal, __VA_ARGS__)
#endif
#ifndef FLOVA_SERIAL_PRINTF_RAW
#define FLOVA_SERIAL_PRINTF_RAW(literal, ...) Serial.printf(literal, __VA_ARGS__)
#endif
#ifndef FLOVA_SERIAL_PRINTLN_RAW
#define FLOVA_SERIAL_PRINTLN_RAW(literal) Serial.println(literal)
#endif

#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_ERROR
#define FLOVA_LOG_ERROR(logger, literal) FLOVA_LOG_RAW(logger, literal)
#define FLOVA_LOGF_ERROR(logger, literal, ...) \
  do { char flova_log_message[128] = {}; \
       FLOVA_FORMAT(flova_log_message, sizeof(flova_log_message), literal, __VA_ARGS__); \
       (logger).log(flova_log_message); } while (0)
#define FLOVA_LOG_MESSAGE_ERROR(logger, message) (logger).log(message)
#define FLOVA_SERIAL_PRINTF_ERROR(literal, ...) FLOVA_SERIAL_PRINTF_RAW(literal, __VA_ARGS__)
#define FLOVA_SERIAL_PRINTLN_ERROR(literal) FLOVA_SERIAL_PRINTLN_RAW(literal)
#else
#define FLOVA_LOG_ERROR(logger, literal) do { } while (0)
#define FLOVA_LOGF_ERROR(logger, literal, ...) do { } while (0)
#define FLOVA_LOG_MESSAGE_ERROR(logger, message) do { } while (0)
#define FLOVA_SERIAL_PRINTF_ERROR(literal, ...) do { } while (0)
#define FLOVA_SERIAL_PRINTLN_ERROR(literal) do { } while (0)
#endif

#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_WARN
#define FLOVA_LOG_WARN(logger, literal) FLOVA_LOG_RAW(logger, literal)
#define FLOVA_LOGF_WARN(logger, literal, ...) \
  do { char flova_log_message[128] = {}; \
       FLOVA_FORMAT(flova_log_message, sizeof(flova_log_message), literal, __VA_ARGS__); \
       (logger).log(flova_log_message); } while (0)
#define FLOVA_LOG_MESSAGE_WARN(logger, message) (logger).log(message)
#define FLOVA_SERIAL_PRINTF_WARN(literal, ...) FLOVA_SERIAL_PRINTF_RAW(literal, __VA_ARGS__)
#define FLOVA_SERIAL_PRINTLN_WARN(literal) FLOVA_SERIAL_PRINTLN_RAW(literal)
#else
#define FLOVA_LOG_WARN(logger, literal) do { } while (0)
#define FLOVA_LOGF_WARN(logger, literal, ...) do { } while (0)
#define FLOVA_LOG_MESSAGE_WARN(logger, message) do { } while (0)
#define FLOVA_SERIAL_PRINTF_WARN(literal, ...) do { } while (0)
#define FLOVA_SERIAL_PRINTLN_WARN(literal) do { } while (0)
#endif

#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_INFO
#define FLOVA_LOG_INFO(logger, literal) FLOVA_LOG_RAW(logger, literal)
#define FLOVA_LOGF_INFO(logger, literal, ...) \
  do { char flova_log_message[128] = {}; \
       FLOVA_FORMAT(flova_log_message, sizeof(flova_log_message), literal, __VA_ARGS__); \
       (logger).log(flova_log_message); } while (0)
#define FLOVA_LOG_MESSAGE_INFO(logger, message) (logger).log(message)
#define FLOVA_SERIAL_PRINTF_INFO(literal, ...) FLOVA_SERIAL_PRINTF_RAW(literal, __VA_ARGS__)
#define FLOVA_SERIAL_PRINTLN_INFO(literal) FLOVA_SERIAL_PRINTLN_RAW(literal)
#else
#define FLOVA_LOG_INFO(logger, literal) do { } while (0)
#define FLOVA_LOGF_INFO(logger, literal, ...) do { } while (0)
#define FLOVA_LOG_MESSAGE_INFO(logger, message) do { } while (0)
#define FLOVA_SERIAL_PRINTF_INFO(literal, ...) do { } while (0)
#define FLOVA_SERIAL_PRINTLN_INFO(literal) do { } while (0)
#endif

#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_DEBUG
#define FLOVA_LOG_DEBUG(logger, literal) FLOVA_LOG_RAW(logger, literal)
#define FLOVA_LOGF_DEBUG(logger, literal, ...) \
  do { char flova_log_message[128] = {}; \
       FLOVA_FORMAT(flova_log_message, sizeof(flova_log_message), literal, __VA_ARGS__); \
       (logger).log(flova_log_message); } while (0)
#define FLOVA_LOG_MESSAGE_DEBUG(logger, message) (logger).log(message)
#define FLOVA_SERIAL_PRINTF_DEBUG(literal, ...) FLOVA_SERIAL_PRINTF_RAW(literal, __VA_ARGS__)
#define FLOVA_SERIAL_PRINTLN_DEBUG(literal) FLOVA_SERIAL_PRINTLN_RAW(literal)
#else
#define FLOVA_LOG_DEBUG(logger, literal) do { } while (0)
#define FLOVA_LOGF_DEBUG(logger, literal, ...) do { } while (0)
#define FLOVA_LOG_MESSAGE_DEBUG(logger, message) do { } while (0)
#define FLOVA_SERIAL_PRINTF_DEBUG(literal, ...) do { } while (0)
#define FLOVA_SERIAL_PRINTLN_DEBUG(literal) do { } while (0)
#endif

#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_TRACE
#define FLOVA_LOG_TRACE(logger, literal) FLOVA_LOG_RAW(logger, literal)
#define FLOVA_LOGF_TRACE(logger, literal, ...) \
  do { char flova_log_message[128] = {}; \
       FLOVA_FORMAT(flova_log_message, sizeof(flova_log_message), literal, __VA_ARGS__); \
       (logger).log(flova_log_message); } while (0)
#define FLOVA_LOG_MESSAGE_TRACE(logger, message) (logger).log(message)
#define FLOVA_SERIAL_PRINTF_TRACE(literal, ...) FLOVA_SERIAL_PRINTF_RAW(literal, __VA_ARGS__)
#define FLOVA_SERIAL_PRINTLN_TRACE(literal) FLOVA_SERIAL_PRINTLN_RAW(literal)
#else
#define FLOVA_LOG_TRACE(logger, literal) do { } while (0)
#define FLOVA_LOGF_TRACE(logger, literal, ...) do { } while (0)
#define FLOVA_LOG_MESSAGE_TRACE(logger, message) do { } while (0)
#define FLOVA_SERIAL_PRINTF_TRACE(literal, ...) do { } while (0)
#define FLOVA_SERIAL_PRINTLN_TRACE(literal) do { } while (0)
#endif

// Legacy unqualified helpers remain source-compatible while now following the
// informational threshold. Board-specific raw helpers stay private to this
// header's severity implementations.

#define FLOVA_LOG(logger, literal) FLOVA_LOG_INFO(logger, literal)
#define FLOVA_SERIAL_PRINTF(literal, ...) FLOVA_SERIAL_PRINTF_INFO(literal, __VA_ARGS__)
#define FLOVA_SERIAL_PRINTLN(literal) FLOVA_SERIAL_PRINTLN_INFO(literal)
