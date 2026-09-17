#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <FlovaFlashLog.h>

struct TestLogger {
  int calls = 0;
  char last[128] = {};

  void log(const char* message) {
    ++calls;
    if (message) {
      strncpy(last, message, sizeof(last) - 1);
      last[sizeof(last) - 1] = 0;
    }
  }
};

int main() {
  TestLogger logger;
  FLOVA_LOG_ERROR(logger, "error");
  FLOVA_LOG_WARN(logger, "warning");
  FLOVA_LOG_INFO(logger, "info");
  FLOVA_LOG_DEBUG(logger, "debug");
  FLOVA_LOG_TRACE(logger, "trace");
  FLOVA_LOGF_ERROR(logger, "formatted=%u", 7U);
  assert(strcmp(logger.last, "formatted=7") == 0 || logger.calls == 0);

  int expected = 0;
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_ERROR
  ++expected;
#endif
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_WARN
  ++expected;
#endif
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_INFO
  ++expected;
#endif
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_DEBUG
  ++expected;
#endif
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_TRACE
  ++expected;
#endif
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_ERROR
  ++expected;
#endif
  assert(logger.calls == expected);
  return 0;
}
