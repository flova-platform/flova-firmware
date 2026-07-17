#pragma once

// Resource capacities belong to the board/build profile. The SDK deliberately
// has no product defaults because available RAM and storage vary by target.
#if !defined(FLOVA_DATASTREAM_CAPACITY) || !defined(FLOVA_TEXT_CAPACITY) || \
    !defined(FLOVA_HISTORY_CAPACITY) || !defined(FLOVA_COMMAND_DEDUP_CAPACITY) || \
    !defined(FLOVA_HARDWARE_INPUT_CAPACITY) || !defined(FLOVA_HARDWARE_OUTPUT_CAPACITY) || \
    !defined(FLOVA_SCHEDULE_CAPACITY) || !defined(FLOVA_SCHEDULE_OCCURRENCE_CAPACITY)
#error "Define the Flova resource capacities in the board build profile"
#endif

// A board advertises offline schedules only after its transport and durable
// storage are wired to ScheduleRuntime. Array capacity alone is not support.
#ifndef FLOVA_SCHEDULE_RUNTIME_ENABLED
#define FLOVA_SCHEDULE_RUNTIME_ENABLED 0
#endif

#ifndef FLOVA_HISTORY_RUNTIME_ENABLED
#define FLOVA_HISTORY_RUNTIME_ENABLED 0
#endif
