#pragma once

// Arduino-only adapters. This package supplies platform services for the
// standalone flova::Device core; it does not define the portable runtime.
#include "adapters/ArduinoOtaInstaller.h"
#include <FlovaDevice.h>
#include "adapters/ArduinoClock.h"
#include "adapters/ArduinoLogger.h"
#include "adapters/ArduinoDeviceLink.h"
#include "adapters/ArduinoStorage.h"
