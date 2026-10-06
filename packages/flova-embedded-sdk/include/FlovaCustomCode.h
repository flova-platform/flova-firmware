#pragma once

// Small opt-in helpers for custom application code. They keep the typed
// Datastream API and its text-based Console keys visible while reducing the
// amount of C++ syntax beginners need to learn.
#include "FlovaDevice.h"

#define FLOVA_DATASTREAM(client, type, key) ((client).datastream<type>(key))
#define FLOVA_WRITE(stream, value) ((stream).write(value))
#define FLOVA_READ(stream) ((stream).value())
#define FLOVA_REPORT(stream, ...) ((stream).report(__VA_ARGS__))
#define FLOVA_ON_WRITE(stream, ...) ((stream).onWrite(__VA_ARGS__))
#define FLOVA_HAS_VALUE(stream) ((stream).hasValue())
#define FLOVA_SETTING(client, type, key, default_value) ((client).setting<type>(key, default_value))
