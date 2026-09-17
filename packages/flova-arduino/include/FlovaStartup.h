#pragma once
#include <Arduino.h>
#include <FlovaSdkVersion.h>

inline void flovaPrintStartup() {
#ifndef FLOVA_NO_DEFAULT_BANNER
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_INFO
  Serial.println(F("\n FFFFF  L      OOO   V   V   AAA"));
  Serial.println(F(" F      L     O   O  V   V  A   A"));
  Serial.println(F(" FFFF   L     O   O  V   V  AAAAA"));
  Serial.println(F(" F      L     O   O   V V   A   A"));
  Serial.println(F(" F      LLLLL  OOO     V    A   A"));
  Serial.print(F(" FlovaSDK " FLOVA_SDK_VERSION " on "));
#ifdef FLOVA_BOARD_NAME
  Serial.println(F(FLOVA_BOARD_NAME));
#else
  Serial.println(F("Arduino"));
#endif
#ifdef FLOVA_FIRMWARE_VERSION
  Serial.print(F(" Firmware: "));
  Serial.println(F(FLOVA_FIRMWARE_VERSION));
#endif
#endif
#endif
}
