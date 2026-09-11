#pragma once

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
  ~FlovaEsp8266Platform() override { client_.stop(0); delete trustAnchors_; }

  bool connected() override { return client_.connected(); }
  int available() override { return client_.available(); }
  int read() override { return client_.read(); }
  bool linkClosed() const override { return !open_; }

  bool beginLink() override {
    if (trustAnchors_) return true;
    trustAnchors_ = new (std::nothrow) BearSSL::X509List(FLOVA_TLS_ROOT_CERTS);
    resourceUnavailable_ = !trustAnchors_;
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
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
      flova::configureLinkTls(client_, *trustAnchors_, time(nullptr));
      // Stock BearSSL is synchronous. This operation can pause device.run();
      // no polling facade can make its cryptographic work nonblocking.
      const uint32_t started = millis();
      bool connected = false;
#if defined(MMU_IRAM_HEAP)
      { HeapSelectIram iram; connected = client_.connect(linkHost_, linkPort_); }
#else
      connected = client_.connect(linkHost_, linkPort_);
#endif
      if (!connected) {
        flova::logLinkTlsFailure(client_);
        Serial.printf("[flova] Link open elapsed_ms=%lu\n",
                      static_cast<unsigned long>(millis() - started));
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
      client_.setTimeout(5000);
      client_.setNoDelay(true);
    }
    return client_.connected() ? FlovaLinkOpenStatus::Connected
                               : FlovaLinkOpenStatus::Failed;
  }

  void closeLink() override {
    clearWrite();
    client_.stop(0);
    opening_ = false;
    open_ = false;
  }
  bool resourceRecoveryRequired() const override { return resourceUnavailable_; }
  bool linkWriteBusy() const override { return writeOffset_ < writeLength_; }

  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    if (linkWriteBusy() || !data || !length || length > sizeof(writeData_) ||
        !client_.connected()) return false;
    memcpy(writeData_, data, length);
    writeLength_ = length;
    writeOffset_ = 0;
    return true;
  }

  bool serviceLinkWrite() override {
    if (!linkWriteBusy()) return true;
    const size_t written = client_.write(writeData_ + writeOffset_,
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
    if (!linkClosed()) return flova::OtaInstallResult::ResourceUnavailable;
    if (strncmp(offer.url, "https://", 8) != 0 || !offer.sizeBytes ||
        offer.sizeBytes > otaMaxImageBytes())
      return flova::OtaInstallResult::DownloadFailed;
    flova::TlsHeapStats heap;
    const flova::TlsResourceStatus resources = flova::tlsResourceStatus(flova::TlsUse::Ota, &heap);
    flova::logTlsHeap("before OTA", heap);
    if (resources != flova::TlsResourceStatus::Ready)
      return flova::OtaInstallResult::ResourceUnavailable;

    HTTPClient http;
    BearSSL::WiFiClientSecure client;
    flova::configureOtaTls(client);
    if (!trustAnchors_)
      return flova::OtaInstallResult::ResourceUnavailable;
    client.setTrustAnchors(trustAnchors_);
    if (!http.begin(client, offer.url))
      return flova::OtaInstallResult::DownloadFailed;
    http.setTimeout(flova::kHttpsTimeoutMs);
    int status = 0;
#if defined(MMU_IRAM_HEAP)
    { HeapSelectIram iram; status = http.GET(); }
#else
    status = http.GET();
#endif
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
      const size_t count = stream->readBytes(transferBuffer_, min(remaining, min(available, sizeof(transferBuffer_))));
      if (!count || Update.write(transferBuffer_, count) != count) {
        abortUpdate();
        http.end();
        return flova::OtaInstallResult::FlashFailed;
      }
      br_sha256_update(&hash, transferBuffer_, count);
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

  BearSSL::WiFiClientSecure client_;
  BearSSL::X509List* trustAnchors_ = nullptr;
  bool open_ = false;
  bool opening_ = false;
  char linkHost_[128] = {};
  uint16_t linkPort_ = 0;
  uint8_t writeData_[526] = {};
  size_t writeLength_ = 0;
  size_t writeOffset_ = 0;
  bool resourceUnavailable_ = false;
  uint8_t transferBuffer_[512] = {};
};
