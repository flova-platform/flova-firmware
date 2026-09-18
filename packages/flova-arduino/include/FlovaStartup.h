#pragma once
#include <Arduino.h>
#include <FlovaSdkVersion.h>

inline void flovaPrintStartup() {
#ifndef FLOVA_NO_DEFAULT_BANNER
  // Keep the identity banner independent from diagnostic logging policy so
  // production profiles still identify themselves at boot.
  Serial.println(F("\n███████╗ ██╗      ██████╗  ██╗   ██╗  █████╗"));
  Serial.println(F("██╔════╝ ██║     ██╔═══██╗ ██║   ██║ ██╔══██╗"));
  Serial.println(F("█████╗   ██║     ██║   ██║ ██║   ██║ ███████║"));
  Serial.println(F("██╔══╝   ██║     ██║   ██║ ╚██╗ ██╔╝ ██╔══██║"));
  Serial.println(F("██║      ███████╗╚██████╔╝  ╚████╔╝  ██║  ██║"));
  Serial.println(F("╚═╝      ╚══════╝ ╚═════╝    ╚═══╝   ╚═╝  ╚═╝"));
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
}
