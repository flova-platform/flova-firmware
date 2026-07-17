#pragma once

#include <Arduino.h>
#include <Crypto.h>
#include <SHA256.h>
#include <FlovaOta.h>

#if defined(ESP8266)
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <Updater.h>
#else
#include <HTTPClient.h>
#include <Update.h>
#endif

class ArduinoOtaInstaller : public FlovaOtaInstaller {
 public:
  FlovaOtaResult install(const FlovaOtaOffer& offer) override {
    HTTPClient http;
#if defined(ESP8266)
    BearSSL::WiFiClientSecure client;
    client.setInsecure();  // Integrity is checked against SHA-256 received over authenticated MQTT.
    if (!http.begin(client, offer.artifactUrl)) return FlovaOtaResult::DownloadFailed;
#else
    if (!http.begin(offer.artifactUrl)) return FlovaOtaResult::DownloadFailed;
#endif
    int status = http.GET();
    if (status != HTTP_CODE_OK || (uint32_t)http.getSize() != offer.sizeBytes) {
      http.end();
      return FlovaOtaResult::DownloadFailed;
    }
    if (!Update.begin(offer.sizeBytes)) {
      http.end();
      return FlovaOtaResult::FlashFailed;
    }

    SHA256 hash;
    hash.reset();
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[512];
    uint32_t written = 0;
    while (http.connected() && written < offer.sizeBytes) {
      size_t available = stream->available();
      if (!available) { delay(1); continue; }
      size_t count = stream->readBytes(buffer, min(available, sizeof(buffer)));
      if (!count || Update.write(buffer, count) != count) {
        abortUpdate();
        http.end();
        return FlovaOtaResult::FlashFailed;
      }
      hash.update(buffer, count);
      written += count;
    }
    http.end();

    uint8_t digest[32];
    hash.finalize(digest, sizeof(digest));
    if (written != offer.sizeBytes || hex(digest, sizeof(digest)) != offer.sha256) {
      abortUpdate();
      return FlovaOtaResult::HashMismatch;
    }
    return Update.end(true) ? FlovaOtaResult::Installed : FlovaOtaResult::FlashFailed;
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
};
