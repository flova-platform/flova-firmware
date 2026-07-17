#pragma once
#include <esp_ota_ops.h>
#include <FlovaBootControl.h>

#ifndef FLOVA_ESP32_AB_OTA
#define FLOVA_ESP32_AB_OTA 0
#endif
#ifndef FLOVA_ESP32_AB_LAYOUT
#define FLOVA_ESP32_AB_LAYOUT "legacy"
#endif
#ifndef FLOVA_ESP32_AB_MIN_SLOT_BYTES
#define FLOVA_ESP32_AB_MIN_SLOT_BYTES 0
#endif

class FlovaEsp32BootControl : public FlovaBootControl {
 public:
  void begin() {
#if FLOVA_ESP32_AB_OTA
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    valid_ = next && next->size >= FLOVA_ESP32_AB_MIN_SLOT_BYTES;
#else
    esp_ota_img_states_t state;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
      esp_ota_mark_app_valid_cancel_rollback();
#endif
  }

  void setRolledBack(bool value) { rolledBack_ = FLOVA_ESP32_AB_OTA && value; }
  FlovaOtaStrategy strategy() const override {
    return FLOVA_ESP32_AB_OTA && valid_ ? FlovaOtaStrategy::Ab : FlovaOtaStrategy::None;
  }

  FlovaBootState state() const override {
#if FLOVA_ESP32_AB_OTA
    if (!valid_) return FlovaBootState::Stable;
    if (rolledBack_) return FlovaBootState::RolledBack;
    esp_ota_img_states_t state;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) return FlovaBootState::Candidate;
#endif
    return FlovaBootState::Stable;
  }

  const char* layoutVersion() const override {
#if FLOVA_ESP32_AB_OTA
    return valid_ ? FLOVA_ESP32_AB_LAYOUT : "legacy";
#else
    return "legacy";
#endif
  }

  const char* activeSlot() const override {
#if FLOVA_ESP32_AB_OTA
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running ? running->label : "unknown";
#else
    return "platform-managed";
#endif
  }

  uint32_t maxImageBytes() const override {
#if FLOVA_ESP32_AB_OTA
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    return next ? next->size : 0;
#else
    return 0;
#endif
  }
  const char* rollbackReason() const override { return rolledBack_ ? "candidate_boot_failed" : ""; }

  bool confirmCandidate() override {
    return state() != FlovaBootState::Candidate || esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
  }

  bool rollbackCandidate() override {
    return state() == FlovaBootState::Candidate &&
           esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
  }

 private:
  bool rolledBack_ = false;
  bool valid_ = false;
};
