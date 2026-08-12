#pragma once

// Arduino-only adapters. This package supplies platform services for the
// standalone flova::Device core; it does not define the portable runtime.
#include "Flova.h"
#include "adapters/ArduinoOtaInstaller.h"
#include "adapters/ArduinoDeviceLink.h"
#include "adapters/ArduinoFlovaHardware.h"
#include "adapters/ArduinoFlovaLink.h"
#include "adapters/ArduinoFlovaServices.h"
