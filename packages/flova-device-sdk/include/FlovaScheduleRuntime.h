#pragma once

#include "FlovaCore.h"

namespace flova {

static const size_t kMaxSchedules = FLOVA_SCHEDULE_CAPACITY;
static const size_t kMaxOccurrencesPerSchedule = FLOVA_SCHEDULE_OCCURRENCE_CAPACITY;
static const uint64_t kRenewRetryMs = 6ULL * 60 * 60 * 1000;

struct ScheduleAction {
  char key[kMaxText];
  Value value;
};

struct CompiledSchedule {
  char id[kMaxText];
  bool enabled;
  ScheduleAction action;
  uint8_t occurrenceCount;
  uint64_t occurrences[kMaxOccurrencesPerSchedule];
};

struct ScheduleManifest {
  uint32_t magic;
  uint32_t revision;
  uint64_t generatedAt;
  uint64_t validUntil;
  uint64_t renewBefore;
  uint8_t scheduleCount;
  CompiledSchedule schedules[kMaxSchedules];
  uint32_t checksum;
  ScheduleManifest()
      : magic(0x46534D31UL), revision(0), generatedAt(0), validUntil(0), renewBefore(0),
        scheduleCount(0), checksum(0) {}
};

typedef WriteResult (*ScheduleApply)(const char* scheduleId,
                                     const ScheduleAction& action,
                                     uint64_t occurrence);
typedef void (*ScheduleRenew)(uint32_t appliedRevision, uint64_t validUntil);
typedef void (*ScheduleStatus)(const char* status, uint32_t revision,
                               uint64_t validUntil);

class ScheduleRuntime {
 public:
  ScheduleRuntime(Storage& storage, Clock& clock)
      : storage_(storage), clock_(clock), apply_(0), renew_(0), status_(0),
        lastRenewRequest_(0), expiredReported_(false) {}

  void handlers(ScheduleApply apply, ScheduleRenew renew, ScheduleStatus status) {
    apply_ = apply;
    renew_ = renew;
    status_ = status;
  }

  bool begin() {
    if (!storage_.read("schedule.active", &manifest_, sizeof(manifest_))) return false;
    if (!valid(manifest_)) { storage_.remove("schedule.active"); return false; }
    restoreProgress();
    return true;
  }

  bool install(const ScheduleManifest& incoming) {
    if (!valid(incoming) || incoming.revision <= manifest_.revision) return false;
    if (!storage_.write("schedule.staging", &incoming, sizeof(incoming))) return false;
    if (!storage_.write("schedule.active", &incoming, sizeof(incoming))) return false;
    storage_.remove("schedule.staging");
    manifest_ = incoming;
    for (size_t i = 0; i < kMaxSchedules; ++i) progress_[i] = 0;
    persistProgress();
    expiredReported_ = false;
    if (status_) status_("installed", manifest_.revision, manifest_.validUntil);
    return true;
  }

  void run() {
    if (!clock_.utcValid() || !manifest_.revision) return;
    const uint64_t now = clock_.utcMilliseconds();
    if (now >= manifest_.validUntil) {
      if (!expiredReported_ && status_) status_("schedule_horizon_expired", manifest_.revision, manifest_.validUntil);
      expiredReported_ = true;
      requestRenew(now);
      return;
    }
    if (now >= manifest_.renewBefore) requestRenew(now);

    for (size_t i = 0; i < manifest_.scheduleCount; ++i) {
      CompiledSchedule& schedule = manifest_.schedules[i];
      if (!schedule.enabled) continue;
      while (progress_[i] < schedule.occurrenceCount &&
             schedule.occurrences[progress_[i]] <= now) {
        const uint64_t occurrence = schedule.occurrences[progress_[i]++];
        // Progress is persisted before hardware execution: after a brownout it
        // is safer to miss one action than to execute an imperative action twice.
        persistProgress();
        if (apply_) apply_(schedule.id, schedule.action, occurrence);
      }
    }
  }

  uint32_t revision() const { return manifest_.revision; }
  uint64_t validUntil() const { return manifest_.validUntil; }

  static uint32_t checksum(const ScheduleManifest& manifest) {
    uint32_t hash = 2166136261UL;
    mix(hash, manifest.revision); mix64(hash, manifest.generatedAt); mix64(hash, manifest.validUntil); mix64(hash, manifest.renewBefore);
    mix(hash, manifest.scheduleCount);
    for (size_t i = 0; i < manifest.scheduleCount; ++i) {
      const CompiledSchedule& schedule = manifest.schedules[i];
      mixText(hash, schedule.id); mix(hash, schedule.enabled); mixText(hash, schedule.action.key);
      mix(hash, static_cast<uint32_t>(schedule.action.value.type)); mixValue(hash, schedule.action.value);
      mix(hash, schedule.occurrenceCount);
      for (size_t j = 0; j < schedule.occurrenceCount; ++j) mix64(hash, schedule.occurrences[j]);
    }
    return hash;
  }

 private:
  bool valid(const ScheduleManifest& value) const {
    if (value.magic != 0x46534D31UL || !value.revision || !value.validUntil || !value.renewBefore ||
        value.scheduleCount > kMaxSchedules || value.checksum != checksum(value)) return false;
    for (size_t i = 0; i < value.scheduleCount; ++i)
      if (value.schedules[i].occurrenceCount > kMaxOccurrencesPerSchedule ||
          !value.schedules[i].id[0] || !value.schedules[i].action.key[0]) return false;
    return true;
  }

  void requestRenew(uint64_t now) {
    if (!renew_ || (lastRenewRequest_ && now - lastRenewRequest_ < kRenewRetryMs)) return;
    lastRenewRequest_ = now;
    renew_(manifest_.revision, manifest_.validUntil);
  }

  void restoreProgress() {
    if (!storage_.read("schedule.progress", progress_, sizeof(progress_)))
      for (size_t i = 0; i < kMaxSchedules; ++i) progress_[i] = 0;
    for (size_t i = 0; i < manifest_.scheduleCount; ++i)
      if (progress_[i] > manifest_.schedules[i].occurrenceCount) progress_[i] = 0;
  }
  void persistProgress() { storage_.write("schedule.progress", progress_, sizeof(progress_)); }
  static void mix(uint32_t& hash, uint32_t value) { for (uint8_t i = 0; i < 4; ++i) { hash ^= value & 0xff; hash *= 16777619UL; value >>= 8; } }
  static void mix64(uint32_t& hash, uint64_t value) { mix(hash, static_cast<uint32_t>(value)); mix(hash, static_cast<uint32_t>(value >> 32)); }
  static void mixText(uint32_t& hash, const char* value) { while (value && *value) { hash ^= static_cast<uint8_t>(*value++); hash *= 16777619UL; } }
  static void mixValue(uint32_t& hash, const Value& value) {
    if (value.type == ValueType::Boolean) mix(hash, value.scalar.boolean);
    else if (value.type == ValueType::Float) { uint32_t raw; memcpy(&raw, &value.scalar.floating, sizeof(raw)); mix(hash, raw); }
    else if (value.type == ValueType::Double) { uint64_t raw; memcpy(&raw, &value.scalar.number, sizeof(raw)); mix64(hash, raw); }
    else mixText(hash, value.text);
  }

  Storage& storage_;
  Clock& clock_;
  ScheduleManifest manifest_;
  uint8_t progress_[kMaxSchedules] = {};
  ScheduleApply apply_;
  ScheduleRenew renew_;
  ScheduleStatus status_;
  uint64_t lastRenewRequest_;
  bool expiredReported_;
};

}  // namespace flova
