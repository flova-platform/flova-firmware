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
  ~FlovaEsp32Platform() override {
    stopping_.store(true);
    closeLink();
    // The worker finishes TLS using its own deadlines. Never destroy its
    // client or storage from another task.
    while (linkTask_ && !exited_.load()) delay(1);
  }

  bool connected() override {
    return !linkCancel_.load() && linkOpenStatus_.load() == LinkOpenStatus::Connected;
  }
  int available() override {
    return connected() ? static_cast<int>(rxProduced_.load() - rxConsumed_.load()) : 0;
  }
  int read() override {
    const uint32_t tail = rxConsumed_.load();
    if (!connected() || tail == rxProduced_.load()) return -1;
    const uint8_t value = rx_[tail % sizeof(rx_)];
    rxConsumed_.store(tail + 1);
    return value;
  }
  bool linkClosed() const override {
    const LinkOpenStatus state = linkOpenStatus_.load();
    return state == LinkOpenStatus::Idle || state == LinkOpenStatus::Failed;
  }

  bool startLink(const char* host, uint16_t port) override {
    if (!host || !host[0] || strlen(host) >= sizeof(linkHost_) || !port ||
        !linkClosed() || stopping_.load()) return false;
    memcpy(linkHost_, host, strlen(host) + 1);
    linkPort_ = port;
    rxProduced_.store(0);
    rxConsumed_.store(0);
    txLength_.store(0);
    linkCancel_.store(false);
    generation_.fetch_add(1);
    linkOpenStatus_.store(LinkOpenStatus::Opening);
    if (!linkTask_ &&
        xTaskCreate(runLinkTask, "flova-link", kLinkTaskStackBytes, this,
                    1, &linkTask_) != pdPASS) {
      resourceUnavailable_ = true;
      linkOpenStatus_.store(LinkOpenStatus::Failed);
      return false;
    }
    resourceUnavailable_ = false;
    return true;
  }

  FlovaLinkOpenStatus pollLink() override {
    if (connected()) return FlovaLinkOpenStatus::Connected;
    return linkClosed() ? FlovaLinkOpenStatus::Failed : FlovaLinkOpenStatus::InProgress;
  }

  void closeLink() override {
    generation_.fetch_add(1);
    linkCancel_.store(true);
  }
  bool resourceRecoveryRequired() const override { return resourceUnavailable_; }
  bool linkWriteBusy() const override { return txLength_.load() != 0; }

  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    if (!connected() || linkWriteBusy() || !data || !length ||
        length > sizeof(tx_)) return false;
    memcpy(tx_, data, length);
    txLength_.store(length);
    return true;
  }
  bool serviceLinkWrite() override {
    return linkOpenStatus_.load() != LinkOpenStatus::Failed;
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
    if (!linkClosed()) return flova::OtaInstallResult::ResourceUnavailable;
    if (strncmp(offer.url, "https://", 8) != 0 || !offer.sizeBytes ||
        offer.sizeBytes > otaMaxImageBytes())
      return flova::OtaInstallResult::DownloadFailed;

    HTTPClient http;
    WiFiClientSecure client;
    client.setHandshakeTimeout(10);
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
      const size_t remaining = offer.sizeBytes - written;
      const size_t count = stream->readBytes(
          transferBuffer_, min(remaining, min(available, sizeof(transferBuffer_))));
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
    {
      // This task is the sole owner for the entire socket lifetime.
      WiFiClientSecure client;
      while (!self->stopping_.load()) {
        if (self->linkOpenStatus_.load() != LinkOpenStatus::Opening) {
          vTaskDelay(1);
          continue;
        }
        const uint32_t generation = self->generation_.load();
        client.setCACert(FLOVA_TLS_ROOT_CERTS);
        client.setHandshakeTimeout(10);
        // The connect overload below sets the 5000 ms timeout before opening
        // TLS. Calling setTimeout() here would apply socket options to the
        // uninitialized TLS descriptor on this Arduino ESP32 core.
        const uint32_t started = millis();
        bool ok = !self->linkCancel_.load() &&
                  client.connect(self->linkHost_, self->linkPort_, 5000);
        // Arduino ESP32's TLS connect already enables TCP_NODELAY; do not
        // repeat the socket option operation after the handshake.
        if (!ok && !self->linkCancel_.load() &&
            generation == self->generation_.load()) {
          char detail[96] = {};
          const int native = client.lastError(detail, sizeof(detail));
          Serial.printf("[flova] Link open failed generation=%lu native=%d elapsed_ms=%lu\n",
                        static_cast<unsigned long>(generation), native,
                        static_cast<unsigned long>(millis() - started));
        }
        if (ok && generation == self->generation_.load() && !self->linkCancel_.load())
          self->linkOpenStatus_.store(LinkOpenStatus::Connected);
        size_t offset = 0;
        uint32_t progressAt = millis();
        while (ok && !self->stopping_.load() && !self->linkCancel_.load() &&
               generation == self->generation_.load()) {
          const size_t length = self->txLength_.load();
          if (length) {
            if (!offset) progressAt = millis();
            const size_t count = client.write(self->tx_ + offset, length - offset);
            if (!count) { ok = false; break; }
            offset += count;
            if (millis() - progressAt >= 5000UL) { ok = false; break; }
            if (offset == length) {
              offset = 0;
              self->txLength_.store(0);
            }
          }
          // Stop reading when the ring is full; TCP supplies backpressure.
          uint32_t head = self->rxProduced_.load();
          const uint32_t tail = self->rxConsumed_.load();
          size_t budget = 128;
          while (budget-- && head - tail < sizeof(self->rx_) && client.available() > 0) {
            const int byte = client.read();
            if (byte < 0) break;
            self->rx_[head++ % sizeof(self->rx_)] = static_cast<uint8_t>(byte);
            self->rxProduced_.store(head);
          }
          if (!client.connected() && client.available() == 0 &&
              self->rxProduced_.load() == self->rxConsumed_.load()) ok = false;
          vTaskDelay(1);
        }
        client.stop();
        self->txLength_.store(0);
        self->linkOpenStatus_.store(self->linkCancel_.load()
            ? LinkOpenStatus::Idle : LinkOpenStatus::Failed);
      }
    }
    self->exited_.store(true);
    vTaskDelete(nullptr);
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

  TaskHandle_t linkTask_ = nullptr;
  std::atomic<LinkOpenStatus> linkOpenStatus_{LinkOpenStatus::Idle};
  std::atomic<bool> linkCancel_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> exited_{false};
  std::atomic<uint32_t> generation_{0};
  std::atomic<uint32_t> rxProduced_{0}, rxConsumed_{0};
  std::atomic<size_t> txLength_{0};
  uint8_t rx_[1024] = {};
  uint8_t tx_[526] = {};
  bool resourceUnavailable_ = false;
  char linkHost_[128] = {};
  uint16_t linkPort_ = 0;
  uint8_t transferBuffer_[512] = {};
};
