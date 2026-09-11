#pragma once

#include <stdint.h>

namespace flova {
// Keep the final ACK alive until the transport accepts and drains it. The
// promoted generation remains durable even if the peer disappears meanwhile.
class ConfigurationActivation {
 public:
  void begin(uint32_t now) { state_ = Reporting; started_ = now; failed_ = false; }
  bool active() const { return state_ != Idle; }
  bool failed() const { return failed_; }

  template <typename Link, typename Report>
  bool run(Link& link, const Report& report, uint32_t now) {
    if (state_ == Idle) return false;
    if (state_ == Reporting) {
      link.pollBootstrap();
      if (link.publishConfigurationReport(report)) {
        state_ = Draining;
        link.beginMaintenance();
      } else if (now - started_ >= 5000UL) {
        failed_ = true;
        link.disconnect();
        state_ = Idle;
        return true;
      } else {
        return false;
      }
    }
    if (!link.maintenanceReady()) return false;
    failed_ = link.maintenanceFailed();
    state_ = Idle;
    return true;
  }

 private:
  enum State : uint8_t { Idle, Reporting, Draining };
  State state_ = Idle;
  bool failed_ = false;
  uint32_t started_ = 0;
};
}  // namespace flova
