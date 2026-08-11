#pragma once

#include <stddef.h>
#include <stdint.h>

using DatastreamId = uint16_t;

static constexpr DatastreamId FLOVA_INVALID_DATASTREAM_ID = 0;
#ifndef FLOVA_MAX_ACTIVE_DATASTREAMS_LIMIT
#define FLOVA_MAX_ACTIVE_DATASTREAMS_LIMIT 64
#endif
static constexpr size_t FLOVA_MAX_ACTIVE_DATASTREAMS = FLOVA_MAX_ACTIVE_DATASTREAMS_LIMIT;
static constexpr size_t FLOVA_MAX_DATASTREAM_KEY_LENGTH = 48;

inline bool flovaValidDatastreamId(DatastreamId id) {
  return id != FLOVA_INVALID_DATASTREAM_ID;
}

struct DatastreamRuntime {
  DatastreamId id;
  uint8_t type;
  uint8_t flags;
};
