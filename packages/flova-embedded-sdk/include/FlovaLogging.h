#pragma once

// Logging is compile-time policy so disabled diagnostics do not retain format
// strings, allocate formatting buffers, or call a board serial implementation.
#ifndef FLOVA_LOGGING_ENABLED
#define FLOVA_LOGGING_ENABLED 1
#endif

#define FLOVA_LOG_LEVEL_ERROR 0
#define FLOVA_LOG_LEVEL_WARN 1
#define FLOVA_LOG_LEVEL_INFO 2
#define FLOVA_LOG_LEVEL_DEBUG 3
#define FLOVA_LOG_LEVEL_TRACE 4

#ifndef FLOVA_LOG_LEVEL
#define FLOVA_LOG_LEVEL FLOVA_LOG_LEVEL_WARN
#endif
