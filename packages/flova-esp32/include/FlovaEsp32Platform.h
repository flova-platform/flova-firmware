#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <sdkconfig.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include <FlovaArduinoPlatform.h>
#include <FlovaTlsRoots.h>

#ifndef FLOVA_HTTPS_TIMEOUT_MS
#define FLOVA_HTTPS_TIMEOUT_MS 15000
#endif

// ESP32 owns the concrete socket, TLS policy, flash updater, and partition
// capability. The Arduino package only sees the bounded Client/platform seam.
class FlovaEsp32Platform final : public FlovaArduinoPlatform {
 public:
  Client& linkClient() override { return client_; }

  bool openLink(const char* host, uint16_t port) override {
    if (!host || !host[0]) return false;
    client_.setCACert(FLOVA_TLS_ROOT_CERTS);
    client_.setTimeout(FLOVA_HTTPS_TIMEOUT_MS / 1000UL);
    if (!client_.connect(host, port)) return false;
    client_.setNoDelay(true);
    return true;
  }

  void closeLink() override { client_.stop(); }

  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    if (!data || !length || !client_.connected()) return false;
    size_t written = 0;
    while (written < length) {
      const size_t count = client_.write(data + written, length - written);
      if (!count) return false;
      written += count;
    }
    return true;
  }

  uint32_t otaMaxImageBytes() const override {
    return ESP.getFreeSketchSpace();
  }

  FlovaOtaStrategy otaStrategy() const override {
    return FlovaOtaStrategy::Ab;
  }

  const char* otaBootLayoutVersion() const override {
#if defined(FLOVA_OTA_BOOT_LAYOUT_VERSION)
    return FLOVA_OTA_BOOT_LAYOUT_VERSION;
#else
    return "esp32-ab";
#endif
  }

  bool otaRollbackCapable() const override {
#if defined(CONFIG_APP_ROLLBACK_ENABLE) && CONFIG_APP_ROLLBACK_ENABLE
    return true;
#else
    return false;
#endif
  }

  FlovaOtaBootState otaBootState() const override {
#if defined(CONFIG_APP_ROLLBACK_ENABLE) && CONFIG_APP_ROLLBACK_ENABLE
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
      return FlovaOtaBootState::Candidate;
#endif
    return FlovaOtaBootState::Stable;
  }

  bool confirmOtaBoot() override {
#if defined(CONFIG_APP_ROLLBACK_ENABLE) && CONFIG_APP_ROLLBACK_ENABLE
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
#else
    return false;
#endif
  }

  bool rollbackOtaBoot() override {
#if defined(CONFIG_APP_ROLLBACK_ENABLE) && CONFIG_APP_ROLLBACK_ENABLE
    return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
#else
    return false;
#endif
  }

  flova::OtaInstallResult installOta(const FlovaLinkOtaOffer& offer) override {
    if (strncmp(offer.url, "https://", 8) != 0 || !offer.sizeBytes ||
        offer.sizeBytes > otaMaxImageBytes())
      return flova::OtaInstallResult::DownloadFailed;

    HTTPClient http;
    WiFiClientSecure client;
    client.setCACert(FLOVA_TLS_ROOT_CERTS);
    client.setTimeout(FLOVA_HTTPS_TIMEOUT_MS / 1000UL);
    if (!http.begin(client, offer.url))
      return flova::OtaInstallResult::DownloadFailed;
    http.setTimeout(FLOVA_HTTPS_TIMEOUT_MS);
    const int status = http.GET();
    if (status != HTTP_CODE_OK || static_cast<uint32_t>(http.getSize()) != offer.sizeBytes) {
      http.end();
      return flova::OtaInstallResult::DownloadFailed;
    }
    if (!Update.begin(offer.sizeBytes)) {
      http.end();
      return flova::OtaInstallResult::FlashFailed;
    }

    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (mbedtls_sha256_starts_ret(&hash, 0) != 0) {
      mbedtls_sha256_free(&hash);
      abortUpdate();
      http.end();
      return flova::OtaInstallResult::HashMismatch;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint32_t written = 0;
    uint32_t lastProgressAt = millis();
    while (http.connected() && written < offer.sizeBytes) {
      const size_t available = stream->available();
      if (!available) {
        if (millis() - lastProgressAt >= FLOVA_HTTPS_TIMEOUT_MS) {
          abortUpdate();
          http.end();
          mbedtls_sha256_free(&hash);
          return flova::OtaInstallResult::DownloadFailed;
        }
        delay(1);
        continue;
      }
      const size_t count = stream->readBytes(
          transferBuffer_, min(available, sizeof(transferBuffer_)));
      if (!count || Update.write(transferBuffer_, count) != count) {
        abortUpdate();
        http.end();
        mbedtls_sha256_free(&hash);
        return flova::OtaInstallResult::FlashFailed;
      }
      mbedtls_sha256_update_ret(&hash, transferBuffer_, count);
      written += count;
      lastProgressAt = millis();
    }
    http.end();

    uint8_t digest[32] = {};
    mbedtls_sha256_finish_ret(&hash, digest);
    mbedtls_sha256_free(&hash);
    if (written != offer.sizeBytes || !hashMatches(digest, sizeof(digest), offer.sha256)) {
      abortUpdate();
      return flova::OtaInstallResult::HashMismatch;
    }
    return Update.end(true) ? flova::OtaInstallResult::Installed
                            : flova::OtaInstallResult::FlashFailed;
  }

 private:
  void abortUpdate() { Update.abort(); }

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

  WiFiClientSecure client_;
  uint8_t transferBuffer_[512] = {};
};
