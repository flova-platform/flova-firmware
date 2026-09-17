#pragma once

#include <FlovaFlashLog.h>

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <Updater.h>
#include <WiFiClientSecureBearSSL.h>
#include <bearssl/bearssl_hash.h>
#include <lwip/dns.h>
#include <new>

#include <FlovaArduinoPlatform.h>
#include <FlovaEsp8266TlsProfile.h>

class FlovaEsp8266Platform final : public FlovaArduinoPlatform {
 public:
  ~FlovaEsp8266Platform() override { releaseClient(); delete trustAnchors_; }
  FlovaEsp8266Platform() = default;
  FlovaEsp8266Platform(const FlovaEsp8266Platform&) = delete;
  FlovaEsp8266Platform& operator=(const FlovaEsp8266Platform&) = delete;

  bool connected() override { return client_ && client_->connected(); }
  int available() override { return client_ ? client_->available() : 0; }
  int read() override { return client_ ? client_->read() : -1; }
  bool linkClosed() const override { return !open_; }

  bool beginLink() override {
    if (trustAnchors_) return trustAnchors_->getCount() != 0;
    static const char roots[] PROGMEM = FLOVA_TLS_ROOT_CERTS;
    trustAnchors_ = new (std::nothrow) BearSSL::X509List(roots);
    resourceUnavailable_ = !trustAnchors_ || !trustAnchors_->getCount();
    if (resourceUnavailable_) { delete trustAnchors_; trustAnchors_ = nullptr; }
    return !resourceUnavailable_;
  }

  bool startLink(const char* host, uint16_t port) override {
    if (!host || !*host || strlen(host) >= sizeof(linkHost_) ||
        !port || open_ || !trustAnchors_) return false;
    memcpy(linkHost_, host, strlen(host) + 1);
    linkPort_ = port;
    opening_ = true;
    open_ = true;
    resourceUnavailable_ = false;
    linkError_ = "link_open_failed";
    return true;
  }

  FlovaLinkOpenStatus pollLink() override {
    if (opening_) {
      opening_ = false;
      flova::TlsHeapStats heap;
      const auto resources = flova::tlsResourceStatus(flova::TlsUse::Link, &heap);
      flova::logTlsHeap("before Link", heap);
      if (resources != flova::TlsResourceStatus::Ready) {
        resourceUnavailable_ = true;
        linkError_ = flova::tlsResourceError(resources);
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
      if (!createClient()) {
        resourceUnavailable_ = true;
        linkError_ = "tls_client_allocation_failed";
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
      flova::configureLinkTls(*client_, *trustAnchors_, time(nullptr));
      // Stock BearSSL is synchronous. This operation can pause device.run();
      // no polling facade can make its cryptographic work nonblocking.
      const uint32_t started = millis();
      bool connected = false;
      // BearSSL selects IRAM for record buffers itself. Keep its contexts and
      // TCP allocations in DRAM, matching the separate preflight budgets.
      { HeapSelectDram dram; connected = client_->connect(linkHost_, linkPort_); }
      if (!connected) {
        linkError_ = "link_tls_failed";
        flova::logLinkTlsFailure(*client_);
        FLOVA_SERIAL_PRINTF("[flova] Link open elapsed_ms=%lu\n",
                      static_cast<unsigned long>(millis() - started));
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
      client_->setTimeout(5000);
      client_->setNoDelay(true);
    }
    return client_ && client_->connected() ? FlovaLinkOpenStatus::Connected
                               : FlovaLinkOpenStatus::Failed;
  }

  void closeLink() override {
    clearWrite();
    releaseClient();
    opening_ = false;
    open_ = false;
  }
  bool resourceRecoveryRequired() const override { return resourceUnavailable_; }
  const char* linkError() const override { return linkError_; }
  bool linkWriteBusy() const override { return writeOffset_ < writeLength_; }

  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    if (linkWriteBusy() || !data || !length || length > sizeof(writeData_) ||
        (!client_ || !client_->connected())) return false;
    memcpy(writeData_, data, length);
    writeLength_ = length;
    writeOffset_ = 0;
    return true;
  }

  bool serviceLinkWrite() override {
    if (!linkWriteBusy()) return true;
    const size_t written = client_->write(writeData_ + writeOffset_,
                                         writeLength_ - writeOffset_);
    if (!written) return false;
    writeOffset_ += written;
    if (!linkWriteBusy()) clearWrite();
    return true;
  }

  uint32_t otaMaxImageBytes() const override { return ESP.getFreeSketchSpace(); }
  FlovaOtaStrategy otaStrategy() const override { return FlovaOtaStrategy::Ab; }
  const char* otaBootLayoutVersion() const override { return "esp8266-staged-copy"; }
  bool otaRollbackCapable() const override { return false; }

  flova::OtaInstallResult installOta(const FlovaLinkOtaOffer& offer) override {
    if (!linkClosed() || !trustAnchors_) return flova::OtaInstallResult::ResourceUnavailable;
    if (strncmp(offer.url, "https://", 8) != 0 || !offer.sizeBytes ||
        offer.sizeBytes > otaMaxImageBytes())
      return flova::OtaInstallResult::DownloadFailed;
    flova::TlsHeapStats heap;
    const flova::TlsResourceStatus resources = flova::tlsResourceStatus(flova::TlsUse::Ota, &heap);
    flova::logTlsHeap("before OTA", heap);
    if (resources != flova::TlsResourceStatus::Ready)
      return flova::OtaInstallResult::ResourceUnavailable;

    if (!createClient()) return flova::OtaInstallResult::ResourceUnavailable;
    // The HTTP borrower must be destroyed before releasing the TLS owner.
    ClientRelease release{*this};
    HTTPClient http;
    BearSSL::WiFiClientSecure& client = *client_;
    flova::configureOtaTls(client);
    client.setTrustAnchors(trustAnchors_);
    if (!http.begin(client, offer.url))
      return flova::OtaInstallResult::DownloadFailed;
    http.setTimeout(flova::kHttpsTimeoutMs);
    int status = 0;
    // OTA uses the same context/buffer heap split as Device Link.
    { HeapSelectDram dram; status = http.GET(); }
    if (status != HTTP_CODE_OK || static_cast<uint32_t>(http.getSize()) != offer.sizeBytes) {
      http.end();
      return flova::OtaInstallResult::DownloadFailed;
    }
    if (!Update.begin(offer.sizeBytes)) {
      http.end();
      return flova::OtaInstallResult::FlashFailed;
    }

    br_sha256_context hash;
    br_sha256_init(&hash);
    WiFiClient* stream = http.getStreamPtr();
    uint32_t written = 0;
    uint32_t lastProgressAt = millis();
    while (http.connected() && written < offer.sizeBytes) {
      const size_t available = stream->available();
      if (!available) {
        if (millis() - lastProgressAt >= flova::kHttpsTimeoutMs) {
          abortUpdate();
          http.end();
          return flova::OtaInstallResult::DownloadFailed;
        }
        delay(1);
        continue;
      }
      const size_t remaining = offer.sizeBytes - written;
      const size_t count = stream->readBytes(writeData_, min(remaining, min(available, sizeof(writeData_))));
      if (!count || Update.write(writeData_, count) != count) {
        abortUpdate();
        http.end();
        return flova::OtaInstallResult::FlashFailed;
      }
      br_sha256_update(&hash, writeData_, count);
      written += count;
      lastProgressAt = millis();
    }
    http.end();
    uint8_t digest[32] = {};
    br_sha256_out(&hash, digest);
    if (written != offer.sizeBytes || !hashMatches(digest, sizeof(digest), offer.sha256)) {
      abortUpdate();
      return flova::OtaInstallResult::HashMismatch;
    }
    return Update.end(true) ? flova::OtaInstallResult::Installed
                            : flova::OtaInstallResult::FlashFailed;
  }

 private:
  bool createClient() {
    if (client_) return true;
    HeapSelectDram dram;
    client_ = new (std::nothrow) BearSSL::WiFiClientSecure;
    return client_ != nullptr;
  }
  void releaseClient() {
    if (client_) { client_->stop(0); delete client_; client_ = nullptr; }
  }
  struct ClientRelease {
    FlovaEsp8266Platform& owner;
    ~ClientRelease() { owner.releaseClient(); }
  };
  void clearWrite() {
    writeLength_ = 0;
    writeOffset_ = 0;
  }

  void abortUpdate() { Update.end(false); }

  static bool hashMatches(const uint8_t* bytes, size_t length,
                          const char* expected) {
    static const char digits[] = "0123456789abcdef";
    if (!bytes || !expected || strlen(expected) != length * 2) return false;
    for (size_t i = 0; i < length; ++i) {
      if (expected[i * 2] != digits[bytes[i] >> 4] ||
          expected[i * 2 + 1] != digits[bytes[i] & 0x0f]) return false;
    }
    return true;
  }

  BearSSL::WiFiClientSecure* client_ = nullptr;
  BearSSL::X509List* trustAnchors_ = nullptr;
  bool open_ = false;
  bool opening_ = false;
  char linkHost_[128] = {};
  uint16_t linkPort_ = 0;
  uint8_t writeData_[526] = {};
  size_t writeLength_ = 0;
  size_t writeOffset_ = 0;
  bool resourceUnavailable_ = false;
  const char* linkError_ = "link_open_failed";
};
