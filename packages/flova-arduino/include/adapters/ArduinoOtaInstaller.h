#pragma once

#include <Arduino.h>
#include <FlovaClientLink.h>
#include <FlovaTlsRoots.h>
#include <FlovaTlsProfile.h>

#if defined(ESP8266)
#include <bearssl/bearssl_hash.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <Updater.h>
#else
#include <mbedtls/sha256.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#endif

class ArduinoOtaInstaller final {
 public:
#if defined(ESP8266)
  void setTrustAnchors(BearSSL::X509List& trustAnchors) {
    trustAnchors_ = &trustAnchors;
  }
#endif

  flova::OtaInstallResult install(const FlovaLinkOtaOffer& offer) {
    HTTPClient http;
#if defined(ESP8266)
    BearSSL::WiFiClientSecure client;
    flova::TlsHeapStats heap;
    const flova::TlsResourceStatus resources =
        flova::tlsResourceStatus(flova::TlsUse::Ota, &heap);
    flova::logTlsHeap("before OTA", heap);
    if (!trustAnchors_ || resources != flova::TlsResourceStatus::Ready) {
      if (resources != flova::TlsResourceStatus::Ready) {
        Serial.print(F("[flova] OTA rejected reason="));
        Serial.println(flova::tlsResourceError(resources));
      }
      return flova::OtaInstallResult::ResourceUnavailable;
    }
    flova::configureOtaTls(client);
    client.setTrustAnchors(trustAnchors_);
#else
    WiFiClientSecure client;
    client.setCACert(FLOVA_TLS_ROOT_CERTS);
#endif
    if (strncmp(offer.url, "https://", 8) != 0 || !http.begin(client, offer.url))
      return flova::OtaInstallResult::DownloadFailed;
    http.setTimeout(flova::kHttpsTimeoutMs);
    int status = http.GET();
    if (status != HTTP_CODE_OK || (uint32_t)http.getSize() != offer.sizeBytes) {
      http.end();
      return flova::OtaInstallResult::DownloadFailed;
    }
    if (!Update.begin(offer.sizeBytes)) {
      http.end();
      return flova::OtaInstallResult::FlashFailed;
    }

#if defined(ESP8266)
    br_sha256_context hash;
    br_sha256_init(&hash);
#else
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (mbedtls_sha256_starts_ret(&hash, 0) != 0) {
      mbedtls_sha256_free(&hash);
      abortUpdate();
      http.end();
      return flova::OtaInstallResult::HashMismatch;
    }
#endif
    WiFiClient* stream = http.getStreamPtr();
    uint32_t written = 0;
    while (http.connected() && written < offer.sizeBytes) {
      size_t available = stream->available();
      if (!available) { delay(1); continue; }
      size_t count = stream->readBytes(
          transferBuffer_, min(available, sizeof(transferBuffer_)));
      if (!count || Update.write(transferBuffer_, count) != count) {
        abortUpdate();
        http.end();
        return flova::OtaInstallResult::FlashFailed;
      }
#if defined(ESP8266)
      br_sha256_update(&hash, transferBuffer_, count);
#else
      mbedtls_sha256_update_ret(&hash, transferBuffer_, count);
#endif
      written += count;
    }
    http.end();

    uint8_t digest[32];
#if defined(ESP8266)
    br_sha256_out(&hash, digest);
#else
    mbedtls_sha256_finish_ret(&hash, digest);
    mbedtls_sha256_free(&hash);
#endif
    if (written != offer.sizeBytes ||
        !hashMatches(digest, sizeof(digest), offer.sha256)) {
      abortUpdate();
      return flova::OtaInstallResult::HashMismatch;
    }
    flova::OtaInstallResult result =
        Update.end(true) ? flova::OtaInstallResult::Installed
                         : flova::OtaInstallResult::FlashFailed;
    return result;
  }

 private:
  void abortUpdate() {
#if defined(ESP8266)
    Update.end(false);
#else
    Update.abort();
#endif
  }

  static bool hashMatches(const uint8_t* bytes, size_t length,
                          const char* expected) {
    static const char digits[] = "0123456789abcdef";
    if (!bytes || !expected || strlen(expected) != length * 2) return false;
    for (size_t i = 0; i < length; ++i) {
      if (expected[i * 2] != digits[bytes[i] >> 4] ||
          expected[i * 2 + 1] != digits[bytes[i] & 15]) return false;
    }
    return true;
  }
#if defined(ESP8266)
  BearSSL::X509List* trustAnchors_ = nullptr;
#endif
  uint8_t transferBuffer_[512] = {};
};
