#include <sdkconfig.h>

// Arduino-ESP32's initArduino() has a weak btInUse() hook. If the hook
// resolves to the framework default, BTDM memory is released before the
// provisioning manager can initialize its BLE controller.

namespace flova {
namespace esp32 {
namespace detail {

void ensureBleArduinoSupport() {}

}  // namespace detail
}  // namespace esp32
}  // namespace flova

// This symbol is intentionally strong. The package object is pulled into an
// application by ensureBleArduinoSupport() only when the BLE adapter is used,
// so SoftAP-only ESP32 applications keep their existing memory behavior.
extern "C" bool btInUse() { return true; }

// Keep a pending OTA image pending until FlovaClient has authenticated,
// restored configuration, and completed its runtime health window.
#if defined(CONFIG_APP_ROLLBACK_ENABLE) && CONFIG_APP_ROLLBACK_ENABLE
extern "C" bool verifyRollbackLater() { return true; }
#endif
