#pragma once

#include <Arduino.h>
#include <FlovaOta.h>
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

class ArduinoOtaInstaller : public FlovaOtaInstaller {
 public:
#if defined(ESP8266)
  void setTrustAnchors(BearSSL::X509List& trustAnchors) {
    trustAnchors_ = &trustAnchors;
  }
#endif

  FlovaOtaResult install(const FlovaOtaOffer& offer) override {
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
      return FlovaOtaResult::ResourceUnavailable;
    }
    flova::configureOtaTls(client);
    client.setTrustAnchors(trustAnchors_);
#else
    WiFiClientSecure client;
    client.setCACert(FLOVA_TLS_ROOT_CERTS);
#endif
    if (!offer.artifactUrl.startsWith("https://") || !http.begin(client, offer.artifactUrl))
      return FlovaOtaResult::DownloadFailed;
    http.setTimeout(flova::kHttpsTimeoutMs);
    int status = http.GET();
    if (status != HTTP_CODE_OK || (uint32_t)http.getSize() != offer.sizeBytes) {
      http.end();
      return FlovaOtaResult::DownloadFailed;
    }
    if (!Update.begin(offer.sizeBytes)) {
      http.end();
      return FlovaOtaResult::FlashFailed;
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
      return FlovaOtaResult::HashMismatch;
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
        return FlovaOtaResult::FlashFailed;
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
    if (written != offer.sizeBytes || hex(digest, sizeof(digest)) != offer.sha256) {
      abortUpdate();
      return FlovaOtaResult::HashMismatch;
    }
    FlovaOtaResult result =
        Update.end(true) ? FlovaOtaResult::Installed : FlovaOtaResult::FlashFailed;
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

  String hex(const uint8_t* bytes, size_t length) {
    static const char digits[] = "0123456789abcdef";
    String result; result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
  }
#if defined(ESP8266)
  BearSSL::X509List* trustAnchors_ = nullptr;
#endif
  uint8_t transferBuffer_[512] = {};
};
