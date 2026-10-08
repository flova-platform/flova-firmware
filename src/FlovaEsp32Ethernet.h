#pragma once

#include <ETH.h>

#include <FlovaEsp32.h>

// Ethernet uses ESP32's normal IP/TLS stack. This facade owns the repetitive
// composition and direct-device bootstrap retry; the application still owns
// PHY wiring and datastream handlers.
class FlovaEsp32EthernetNetwork final : public FlovaNetworkRuntime {
 public:
  bool connected() const override {
    return ETH.linkUp() && static_cast<uint32_t>(ETH.localIP()) != 0;
  }
};

class FlovaEsp32Ethernet final {
 public:
  FlovaEsp32Ethernet(uint8_t phyAddress = 0, int power = -1, int mdc = 23,
                     int mdio = 18,
                     eth_clock_mode_t clockMode = ETH_CLOCK_GPIO0_IN)
      : phyAddress_(phyAddress), power_(power), mdc_(mdc), mdio_(mdio),
        clockMode_(clockMode), linkPlatform_(), link_(linkPlatform_, entropy_),
        identity_("custom_ethernet_esp32"),
        client_(link_, provisioning_, network_, tlsClock_, identity_, storage_,
                clock_, logger_, entropy_, hardware_) {}

  bool begin() {
    if (!FLOVA_DEVICE_TEMPLATE_ID[0] || !FLOVA_DEVICE_PROVISION_TOKEN[0] ||
        strncmp(FLOVA_DEVICE_LINK_URL, "wss://", 6) != 0 ||
        !ETH.begin(phyAddress_, power_, mdc_, mdio_, ETH_PHY_LAN8720,
                   clockMode_))
      return false;
    return client_.begin(false);
  }

  void run() {
    client_.run();
    const uint32_t now = millis();
    if (network_.connected() &&
        client_.lifecycle() == FlovaLifecycle::AwaitingProvisioning &&
        (!attempted_ || now - lastAttempt_ >= 30000UL)) {
      attempted_ = true;
      lastAttempt_ = now;
      client_.provision(flova::ProvisioningHandoff(
          FLOVA_DEVICE_LINK_URL, FLOVA_DEVICE_PROVISION_TOKEN));
    }
  }

  FlovaLifecycle lifecycle() const { return client_.lifecycle(); }
  bool connected() const { return client_.connected(); }
  bool ready() const { return client_.ready(); }
  const char* lastError() const { return client_.lastError(); }

  template <typename T>
  flova::Datastream<T> datastream(const char* key) {
    return client_.datastream<T>(key);
  }
  template <typename T>
  flova::Setting<T> setting(const char* key, const T& defaultValue) {
    return client_.setting<T>(key, defaultValue);
  }

 private:
  uint8_t phyAddress_;
  int power_;
  int mdc_;
  int mdio_;
  eth_clock_mode_t clockMode_;
  FlovaEsp32Entropy entropy_;
  FlovaEsp32Platform linkPlatform_;
  ArduinoFlovaLink link_;
  FlovaEsp32Storage storage_;
  ArduinoFlovaClock clock_;
  ArduinoFlovaLogger logger_;
  ArduinoFlovaManualHardware hardware_;
  FlovaProvisioningAdapter provisioning_;
  FlovaEsp32EthernetNetwork network_;
  ArduinoFlovaUtcBootstrap<WiFiUDP> tlsClock_;
  FlovaEsp32Identity identity_;
  FlovaClient client_;
  uint32_t lastAttempt_ = 0;
  bool attempted_ = false;
};
