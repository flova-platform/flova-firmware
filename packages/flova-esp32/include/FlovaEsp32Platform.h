#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <sdkconfig.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

  bool startLink(const char* host, uint16_t port) override {
    const LinkOpenStatus status = linkOpenStatus_.load();
    if (!host || !host[0] || strlen(host) >= sizeof(linkHost_) || !port ||
        status == LinkOpenStatus::Opening ||
        status == LinkOpenStatus::Connected)
      return false;
    memcpy(linkHost_, host, strlen(host) + 1);
    linkPort_ = port;
    linkCancel_.store(false);
    linkReady_ = false;
    linkOpenStatus_.store(LinkOpenStatus::Opening);
    if (!linkTask_) {
      if (xTaskCreatePinnedToCore(runLinkTask, "flova-link",
                                  kLinkTaskStackBytes, this, 1, &linkTask_,
                                  0) != pdPASS) {
        resourceUnavailable_ = true;
        linkOpenStatus_.store(LinkOpenStatus::Failed);
        return false;
      }
    }
    resourceUnavailable_ = false;
    xTaskNotifyGive(linkTask_);
    return true;
  }

  FlovaLinkOpenStatus pollLink() override {
    const LinkOpenStatus status = linkOpenStatus_.load();
    if (status == LinkOpenStatus::Connected) {
      if (!client_.connected()) {
        linkOpenStatus_.store(LinkOpenStatus::Failed);
        return FlovaLinkOpenStatus::Failed;
      }
      if (!linkReady_) {
        client_.setNoDelay(true);
        linkReady_ = true;
      }
      return FlovaLinkOpenStatus::Connected;
    }
    if (status == LinkOpenStatus::Failed)
      return FlovaLinkOpenStatus::Failed;
    return FlovaLinkOpenStatus::InProgress;
  }

  void closeLink() override {
    linkCancel_.store(true);
    linkReady_ = false;
    const LinkOpenStatus status = linkOpenStatus_.load();
    if (status != LinkOpenStatus::Opening) client_.stop();
    if (status == LinkOpenStatus::Connected)
      linkOpenStatus_.store(LinkOpenStatus::Failed);
    clearWrite();
  }

  bool resourceRecoveryRequired() const override {
    return resourceUnavailable_;
  }

  bool linkWriteBusy() const override { return writeOffset_ < writeLength_; }

  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    if (linkWriteBusy() || !data || !length || !client_.connected())
      return false;
    writeData_ = data;
    writeLength_ = length;
    writeOffset_ = 0;
    return true;
  }

  bool serviceLinkWrite() override {
    if (!linkWriteBusy()) return true;
    const size_t remaining = writeLength_ - writeOffset_;
    const size_t chunk = remaining < 64 ? remaining : 64;
    const size_t written = client_.write(writeData_ + writeOffset_, chunk);
    if (!written) return false;
    writeOffset_ += written;
    if (!linkWriteBusy()) clearWrite();
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
  enum class LinkOpenStatus : uint8_t { Idle, Opening, Connected, Failed };
  static const uint32_t kLinkTaskStackBytes = 8192;

  static void runLinkTask(void* context) {
    FlovaEsp32Platform* self = static_cast<FlovaEsp32Platform*>(context);
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      self->client_.stop();
      self->client_.setCACert(FLOVA_TLS_ROOT_CERTS);
      self->client_.setTimeout(FLOVA_HTTPS_TIMEOUT_MS / 1000UL);
      const bool connected =
          self->client_.connect(self->linkHost_, self->linkPort_);
      if (connected && self->linkCancel_.load())
        self->client_.stop();
      self->linkOpenStatus_.store(
          connected && !self->linkCancel_.load()
              ? LinkOpenStatus::Connected
              : LinkOpenStatus::Failed);
    }
  }

  void clearWrite() {
    writeData_ = nullptr;
    writeLength_ = 0;
    writeOffset_ = 0;
  }

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
  TaskHandle_t linkTask_ = nullptr;
  std::atomic<LinkOpenStatus> linkOpenStatus_{LinkOpenStatus::Idle};
  std::atomic<bool> linkCancel_{false};
  bool resourceUnavailable_ = false;
  bool linkReady_ = false;
  char linkHost_[128] = {};
  uint16_t linkPort_ = 0;
  const uint8_t* writeData_ = nullptr;
  size_t writeLength_ = 0;
  size_t writeOffset_ = 0;
  uint8_t transferBuffer_[512] = {};
};
