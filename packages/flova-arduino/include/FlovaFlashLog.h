#pragma once

// Board packages may override literal access before including Arduino services.
#ifndef FLOVA_LOG
#define FLOVA_LOG(logger, literal) (logger).log(literal)
#endif
#ifndef FLOVA_FORMAT
#define FLOVA_FORMAT(out, size, literal, ...) snprintf(out, size, literal, __VA_ARGS__)
#endif
#ifndef FLOVA_SERIAL_PRINTF
#define FLOVA_SERIAL_PRINTF(literal, ...) Serial.printf(literal, __VA_ARGS__)
#endif
#ifndef FLOVA_SERIAL_PRINTLN
#define FLOVA_SERIAL_PRINTLN(literal) Serial.println(literal)
#endif
