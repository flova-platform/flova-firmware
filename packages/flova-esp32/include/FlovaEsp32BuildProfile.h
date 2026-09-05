#pragma once

// Board defaults keep the portable runtime explicitly bounded for a standard
// ESP32. An application may define any value before including a Flova header.
#ifndef FLOVA_DATASTREAM_CAPACITY
#define FLOVA_DATASTREAM_CAPACITY 64
#endif
#ifndef FLOVA_TEXT_CAPACITY
#define FLOVA_TEXT_CAPACITY 96
#endif
#ifndef FLOVA_HISTORY_CAPACITY
#define FLOVA_HISTORY_CAPACITY 32
#endif
#ifndef FLOVA_COMMAND_DEDUP_CAPACITY
#define FLOVA_COMMAND_DEDUP_CAPACITY 4
#endif
#ifndef FLOVA_HARDWARE_INPUT_CAPACITY
#define FLOVA_HARDWARE_INPUT_CAPACITY 8
#endif
#ifndef FLOVA_HARDWARE_OUTPUT_CAPACITY
#define FLOVA_HARDWARE_OUTPUT_CAPACITY 8
#endif
#ifndef FLOVA_SCHEDULE_CAPACITY
#define FLOVA_SCHEDULE_CAPACITY 8
#endif
#ifndef FLOVA_SCHEDULE_OCCURRENCE_CAPACITY
#define FLOVA_SCHEDULE_OCCURRENCE_CAPACITY 96
#endif
