#pragma once

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <Updater.h>
#include <WiFiClientSecureBearSSL.h>
#include <bearssl/bearssl_hash.h>
#include <new>

#include <FlovaArduinoPlatform.h>
#include <FlovaEsp8266TlsProfile.h>

class FlovaEsp8266Platform final : public FlovaArduinoPlatform {
 public:
  ~FlovaEsp8266Platform() override { delete trustAnchors_; }

  Client& linkClient() override { return client_; }

  bool beginLink() override {
    if (trustAnchors_) return true;
    flova::TlsHeapStats heap;
    const flova::TlsResourceStatus resources = flova::tlsResourceStatus(flova::TlsUse::Link, &heap);
    flova::logTlsHeap("before Link", heap);
    if (resources != flova::TlsResourceStatus::Ready) {
      resourceUnavailable_ = true;
      Serial.print(F("[flova] Link rejected reason="));
      Serial.println(flova::tlsResourceError(resources));
      return false;
    }
    trustAnchors_ = new (std::nothrow) BearSSL::X509List(FLOVA_TLS_ROOT_CERTS);
    if (!trustAnchors_) {
      resourceUnavailable_ = true;
      return false;
    }
    resourceUnavailable_ = false;
    return true;
  }

  bool openLink(const char* host, uint16_t port) override {
    const flova::TlsResourceStatus resources =
        flova::tlsResourceStatus(flova::TlsUse::Link);
    if (!host || !host[0] || !trustAnchors_ ||
        resources != flova::TlsResourceStatus::Ready) {
      resourceUnavailable_ = resources != flova::TlsResourceStatus::Ready;
      return false;
    }
    resourceUnavailable_ = false;
    flova::configureLinkTls(client_, *trustAnchors_, time(nullptr));
    {
      HeapSelectIram iram;
      if (!client_.connect(host, port)) {
        flova::logLinkTlsFailure(client_);
        return false;
      }
    }
    client_.setNoDelay(true);
    return true;
  }

  void closeLink() override {
    clearWrite();
    client_.stop();
  }

  bool resourceRecoveryRequired() const override { return resourceUnavailable_; }

  bool linkWriteBusy() const override { return writeOffset_ < writeLength_; }

  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    if (linkWriteBusy() || !data || !length || !client_.connected()) return false;
    writeData_ = data;
    writeLength_ = length;
    writeOffset_ = 0;
    return true;
  }

  bool serviceLinkWrite() override {
    if (!client_.pollNonBlocking()) return false;
    if (!linkWriteBusy()) return true;
    const int written = client_.writeNonBlocking(writeData_ + writeOffset_,
                                                 writeLength_ - writeOffset_);
    if (written < 0) return false;
    writeOffset_ += static_cast<size_t>(written);
    return true;
  }

  uint32_t otaMaxImageBytes() const override { return ESP.getFreeSketchSpace(); }
  FlovaOtaStrategy otaStrategy() const override { return FlovaOtaStrategy::Ab; }
  const char* otaBootLayoutVersion() const override { return "esp8266-staged-copy"; }
  bool otaRollbackCapable() const override { return false; }

  flova::OtaInstallResult installOta(const FlovaLinkOtaOffer& offer) override {
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
    const int status = http.GET();
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
      const size_t count = stream->readBytes(transferBuffer_, min(available, sizeof(transferBuffer_)));
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
    writeData_ = nullptr;
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
  const uint8_t* writeData_ = nullptr;
  size_t writeLength_ = 0;
  size_t writeOffset_ = 0;
  bool resourceUnavailable_ = false;
  uint8_t transferBuffer_[512] = {};
};
