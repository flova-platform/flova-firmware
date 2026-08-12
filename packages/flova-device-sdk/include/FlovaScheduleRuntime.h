#pragma once

#include <stdlib.h>

#include "FlovaCore.h"

namespace flova {

static const size_t kMaxSchedules = FLOVA_SCHEDULE_CAPACITY;
static const size_t kMaxOccurrencesPerSchedule = FLOVA_SCHEDULE_OCCURRENCE_CAPACITY;
static const uint64_t kRenewRetryMs = 6ULL * 60 * 60 * 1000;

struct ScheduleAction {
  uint32_t offsetMs;
  DatastreamId datastreamId;
  Value value;
  ScheduleAction() : offsetMs(0), datastreamId(FLOVA_INVALID_DATASTREAM_ID) {}
};

struct CompiledSchedule {
  char id[kMaxText];
  bool enabled;
  uint8_t actionCount;
  ScheduleAction actions[flova::config::kConfigurationScheduleActions];
  // Kept as the first-action slot for existing typed SDK code. New compiled
  // schedules populate actions[] and mirror actions[0] here.
  ScheduleAction action;
  uint8_t occurrenceCount;
  uint64_t occurrences[kMaxOccurrencesPerSchedule];
  CompiledSchedule() : enabled(false), actionCount(0), occurrenceCount(0) { id[0] = 0; }
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

typedef WriteResult (*ScheduleApply)(void* context, const char* scheduleId,
                                     const ScheduleAction& action,
                                     uint64_t occurrence);
typedef void (*ScheduleRenew)(void* context, uint32_t appliedRevision,
                              uint64_t validUntil);
typedef void (*ScheduleStatus)(void* context, const char* status, uint32_t revision,
                               uint64_t validUntil);
class ScheduleRuntime {
 public:
  ScheduleRuntime(Storage& storage, Clock& clock)
      : storage_(storage), clock_(clock), apply_(0), renew_(0), status_(0),
        context_(0), lastRenewRequest_(0), expiredReported_(false) {}

  void handlers(ScheduleApply apply, ScheduleRenew renew, ScheduleStatus status,
                void* context) {
    apply_ = apply;
    renew_ = renew;
    status_ = status;
    context_ = context;
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
    progress_ = Progress();
    persistProgress();
    expiredReported_ = false;
    if (status_) status_(context_, "installed", manifest_.revision, manifest_.validUntil);
    return true;
  }

  // Configuration restore compiles directly into the inactive runtime
  // workspace. Keeping a second maximum-sized manifest permanently allocated
  // wastes scarce static RAM, especially on ESP32 where DRAM is segmented.
  ScheduleManifest& workspace() { return manifest_; }

  bool installPrepared() {
    if (!valid(manifest_)) return false;
    if (!storage_.write("schedule.staging", &manifest_, sizeof(manifest_))) return false;
    if (!storage_.write("schedule.active", &manifest_, sizeof(manifest_))) return false;
    storage_.remove("schedule.staging");
    progress_ = Progress();
    persistProgress();
    expiredReported_ = false;
    if (status_) status_(context_, "installed", manifest_.revision, manifest_.validUntil);
    return true;
  }

  void run() {
    if (!clock_.utcValid() || !manifest_.revision) return;
    const uint64_t now = clock_.utcMilliseconds();
    if (now >= manifest_.validUntil) {
      if (!expiredReported_ && status_) status_(context_, "schedule_horizon_expired", manifest_.revision, manifest_.validUntil);
      expiredReported_ = true;
      requestRenew(now);
      return;
    }
    if (now >= manifest_.renewBefore) requestRenew(now);

    for (size_t i = 0; i < manifest_.scheduleCount; ++i) {
      CompiledSchedule& schedule = manifest_.schedules[i];
      if (!schedule.enabled) continue;
      while (progress_.occurrence[i] < schedule.occurrenceCount) {
        const uint8_t occurrenceIndex = progress_.occurrence[i];
        const uint8_t actionCount = schedule.actionCount ? schedule.actionCount : 1;
        const ScheduleAction& action = schedule.actionCount
                                           ? schedule.actions[progress_.action[i]]
                                           : schedule.action;
        const uint64_t occurrence = schedule.occurrences[occurrenceIndex];
        if (occurrence + action.offsetMs > now) break;
        ++progress_.action[i];
        if (progress_.action[i] >= actionCount) {
          progress_.action[i] = 0;
          ++progress_.occurrence[i];
        }
        // Progress is persisted before hardware execution: after a brownout it
        // is safer to miss one action than to execute an imperative action twice.
        persistProgress();
        if (apply_) apply_(context_, schedule.id, action, occurrence);
      }
    }
  }

  uint32_t revision() const { return manifest_.revision; }
  uint64_t validUntil() const { return manifest_.validUntil; }

  void clear() {
    storage_.remove("schedule.staging");
    storage_.remove("schedule.active");
    storage_.remove("schedule.progress");
    manifest_ = ScheduleManifest();
    progress_ = Progress();
    expiredReported_ = false;
  }

  static uint32_t checksum(const ScheduleManifest& manifest) {
    uint32_t hash = 2166136261UL;
    mix(hash, manifest.revision); mix64(hash, manifest.generatedAt); mix64(hash, manifest.validUntil); mix64(hash, manifest.renewBefore);
    mix(hash, manifest.scheduleCount);
    for (size_t i = 0; i < manifest.scheduleCount; ++i) {
      const CompiledSchedule& schedule = manifest.schedules[i];
      mixText(hash, schedule.id); mix(hash, schedule.enabled); mix(hash, schedule.actionCount);
      const uint8_t actionCount = schedule.actionCount ? schedule.actionCount : 1;
      for (uint8_t action = 0; action < actionCount; ++action) {
        const ScheduleAction& item = schedule.actionCount ? schedule.actions[action] : schedule.action;
        mix(hash, item.offsetMs); mix(hash, item.datastreamId); mix(hash, static_cast<uint32_t>(item.value.type)); mixValue(hash, item.value);
      }
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
          value.schedules[i].actionCount > flova::config::kConfigurationScheduleActions ||
          !value.schedules[i].id[0] ||
          !(value.schedules[i].actionCount ? flovaValidDatastreamId(value.schedules[i].actions[0].datastreamId) : flovaValidDatastreamId(value.schedules[i].action.datastreamId))) return false;
    return true;
  }

  void requestRenew(uint64_t now) {
    if (!renew_ || (lastRenewRequest_ && now - lastRenewRequest_ < kRenewRetryMs)) return;
    lastRenewRequest_ = now;
    renew_(context_, manifest_.revision, manifest_.validUntil);
  }

  void restoreProgress() {
    if (!storage_.read("schedule.progress", &progress_, sizeof(progress_)))
      progress_ = Progress();
    for (size_t i = 0; i < manifest_.scheduleCount; ++i)
      if (progress_.occurrence[i] > manifest_.schedules[i].occurrenceCount ||
          progress_.action[i] > manifest_.schedules[i].actionCount) {
        progress_.occurrence[i] = 0;
        progress_.action[i] = 0;
      }
  }
  void persistProgress() { storage_.write("schedule.progress", &progress_, sizeof(progress_)); }
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
  struct Progress {
    uint8_t occurrence[kMaxSchedules];
    uint8_t action[kMaxSchedules];
    Progress() { memset(this, 0, sizeof(*this)); }
  } progress_;
  ScheduleApply apply_;
  ScheduleRenew renew_;
  ScheduleStatus status_;
  void* context_;
  uint64_t lastRenewRequest_;
  bool expiredReported_;
};

// Compiles CONFIG_RECORD schedule metadata plus ordered occurrence chunks into
// the existing fixed ScheduleManifest. It never retains more than the bounded
// schedule runtime model and rejects duplicates, gaps, and over-capacity input.
class ScheduleChunkCompiler {
 public:
  explicit ScheduleChunkCompiler(ScheduleManifest& workspace)
      : manifest_(workspace), scheduleCount_(0), started_(false) {
    reset();
  }

  bool begin(uint32_t revision, uint64_t generatedAt, uint64_t validUntil,
             uint64_t renewBefore, uint8_t scheduleCount) {
    if (!revision || !validUntil || !renewBefore || scheduleCount > kMaxSchedules) return false;
    reset();
    manifest_.revision = revision;
    manifest_.generatedAt = generatedAt;
    manifest_.validUntil = validUntil;
    manifest_.renewBefore = renewBefore;
    manifest_.scheduleCount = scheduleCount;
    scheduleCount_ = scheduleCount;
    started_ = true;
    return true;
  }

  bool addSchedule(const config::Schedule& source) {
    if (!started_ || source.actionCount == 0 || source.actionCount > flova::config::kConfigurationScheduleActions) return false;
    size_t slot = kMaxSchedules;
    for (size_t i = 0; i < scheduleCount_; ++i) if (manifest_.schedules[i].id[0] && strtoul(manifest_.schedules[i].id, 0, 10) == source.id) { slot = i; break; }
    if (slot == kMaxSchedules) for (size_t i = 0; i < scheduleCount_; ++i) if (!manifest_.schedules[i].id[0]) { slot = i; break; }
    if (slot == kMaxSchedules) return false;
    CompiledSchedule& target = manifest_.schedules[slot];
    snprintf(target.id, sizeof(target.id), "%lu", static_cast<unsigned long>(source.id));
    target.enabled = source.enabled;
    target.actionCount = source.actionCount;
    target.occurrenceCount = 0;
    occurrenceChunkCount_[slot] = 0;
    for (uint8_t i = 0; i < source.actionCount; ++i) {
      if (source.actions[i].value.kind == config::ValueKind::Int64) return false;
      target.actions[i].value = source.actions[i].value.kind == config::ValueKind::Boolean ? Value::from(source.actions[i].value.data.boolean) :
                                  source.actions[i].value.kind == config::ValueKind::Float32 ? Value::from(source.actions[i].value.data.float32) :
                                  source.actions[i].value.kind == config::ValueKind::Float64 ? Value::from(source.actions[i].value.data.float64) : Value::from(source.actions[i].value.data.text);
      if (!flovaValidDatastreamId(source.actions[i].datastreamId)) return false;
      target.actions[i].offsetMs = source.actions[i].offsetMs;
      target.actions[i].datastreamId = source.actions[i].datastreamId;
    }
    target.action = target.actions[0];
    return true;
  }

  bool addOccurrences(const config::ScheduleOccurrences& source) {
    if (!started_ || !source.chunkCount || source.chunkIndex >= source.chunkCount || source.occurrenceCount == 0 || source.occurrenceCount > flova::config::kConfigurationOccurrenceChunk) return false;
    size_t slot = kMaxSchedules;
    for (size_t i = 0; i < scheduleCount_; ++i) if (manifest_.schedules[i].id[0] && strtoul(manifest_.schedules[i].id, 0, 10) == source.scheduleId) { slot = i; break; }
    if (slot == kMaxSchedules || source.chunkIndex != occurrenceChunkCount_[slot] || manifest_.schedules[slot].occurrenceCount + source.occurrenceCount > kMaxOccurrencesPerSchedule) return false;
    occurrenceChunkTotals_[slot] = source.chunkCount;
    for (uint8_t i = 0; i < source.occurrenceCount; ++i) manifest_.schedules[slot].occurrences[manifest_.schedules[slot].occurrenceCount++] = source.occurrences[i];
    occurrenceChunkCount_[slot]++;
    return true;
  }

  bool finish() {
    if (!started_) return false;
    for (size_t i = 0; i < scheduleCount_; ++i) if (!manifest_.schedules[i].id[0] || !occurrenceChunkTotals_[i] || occurrenceChunkCount_[i] != occurrenceChunkTotals_[i]) return false;
    manifest_.checksum = ScheduleRuntime::checksum(manifest_);
    started_ = false;
    return true;
  }

 private:
  void reset() {
    manifest_ = ScheduleManifest();
    memset(occurrenceChunkCount_, 0, sizeof(occurrenceChunkCount_));
    memset(occurrenceChunkTotals_, 0, sizeof(occurrenceChunkTotals_));
    scheduleCount_ = 0;
  }

  ScheduleManifest& manifest_;
  uint16_t occurrenceChunkCount_[kMaxSchedules];
  uint16_t occurrenceChunkTotals_[kMaxSchedules];
  uint8_t scheduleCount_;
  bool started_;
};

}  // namespace flova
