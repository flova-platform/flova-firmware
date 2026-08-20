#pragma once

#include <stddef.h>
#include <stdint.h>

namespace flova {

enum class ResourceKind : uint8_t { History, Schedules, State, Commands, Ota, Count };

struct StorageCapabilities {
  uint32_t usableBytes;
  uint32_t availableBytes;
  uint32_t maxRecordBytes;
  uint32_t eraseBlockBytes;
  uint16_t writeGranularity;
  bool persistent;
  bool wearSensitive;
  StorageCapabilities()
      : usableBytes(0), availableBytes(0), maxRecordBytes(0), eraseBlockBytes(0),
        writeGranularity(1), persistent(false), wearSensitive(false) {}
};

struct ResourceBudget {
  uint32_t reservedBytes;
  uint32_t maximumBytes;
  uint8_t priority;
  bool elastic;
  ResourceBudget() : reservedBytes(0), maximumBytes(0), priority(0), elastic(false) {}
};

struct ResourceUsage {
  uint32_t usedBytes;
  uint32_t highWaterBytes;
  uint32_t reclaimedBytes;
  ResourceUsage() : usedBytes(0), highWaterBytes(0), reclaimedBytes(0) {}
};

// ResourceManager enforces negotiated byte ceilings. Feature-specific eviction
// remains with the feature because only it knows which records are disposable.
class ResourceManager {
 public:
  ResourceManager() : totalBytes_(0) {}

  void configure(uint32_t totalBytes, const ResourceBudget* budgets, size_t count) {
    totalBytes_ = totalBytes;
    for (size_t i = 0; i < static_cast<size_t>(ResourceKind::Count); ++i) {
      budgets_[i] = ResourceBudget();
      usage_[i] = ResourceUsage();
    }
    for (size_t i = 0; i < count && i < static_cast<size_t>(ResourceKind::Count); ++i)
      budgets_[i] = budgets[i];
  }

  bool reserve(ResourceKind kind, uint32_t bytes) {
    const size_t index = static_cast<size_t>(kind);
    if (!bytes || usage_[index].usedBytes + bytes > budgets_[index].maximumBytes) return false;
    uint32_t protectedBytes = 0;
    for (size_t i = 0; i < static_cast<size_t>(ResourceKind::Count); ++i) {
      if (i == index) continue;
      if (budgets_[i].reservedBytes > usage_[i].usedBytes)
        protectedBytes += budgets_[i].reservedBytes - usage_[i].usedBytes;
    }
    if (totalUsed() + protectedBytes + bytes > totalBytes_) return false;
    usage_[index].usedBytes += bytes;
    if (usage_[index].usedBytes > usage_[index].highWaterBytes)
      usage_[index].highWaterBytes = usage_[index].usedBytes;
    return true;
  }

  void release(ResourceKind kind, uint32_t bytes, bool reclaimed = false) {
    ResourceUsage& usage = usage_[static_cast<size_t>(kind)];
    const uint32_t released = bytes > usage.usedBytes ? usage.usedBytes : bytes;
    usage.usedBytes -= released;
    if (reclaimed) usage.reclaimedBytes += released;
  }

  const ResourceUsage& usage(ResourceKind kind) const { return usage_[static_cast<size_t>(kind)]; }
  const ResourceBudget& budget(ResourceKind kind) const { return budgets_[static_cast<size_t>(kind)]; }

 private:
  uint32_t totalUsed() const {
    uint32_t total = 0;
    for (size_t i = 0; i < static_cast<size_t>(ResourceKind::Count); ++i) total += usage_[i].usedBytes;
    return total;
  }
  uint32_t totalBytes_;
  ResourceBudget budgets_[static_cast<size_t>(ResourceKind::Count)];
  ResourceUsage usage_[static_cast<size_t>(ResourceKind::Count)];
};

enum class HistoryOverflow : uint8_t { DropOldest, DropNewest };

struct HistoryRetentionPolicy {
  uint32_t maximumBytes;
  uint32_t maximumRecords;
  uint32_t maximumAgeSeconds;
  uint32_t minimumIntervalMs;
  HistoryOverflow overflow;
  HistoryRetentionPolicy()
      : maximumBytes(0), maximumRecords(0), maximumAgeSeconds(0), minimumIntervalMs(0),
        overflow(HistoryOverflow::DropOldest) {}
};

}  // namespace flova
