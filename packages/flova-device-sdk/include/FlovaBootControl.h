#pragma once
#include <stdint.h>

enum class FlovaOtaStrategy : uint8_t { None, Ab, AbRecovery };
enum class FlovaBootState : uint8_t { Stable, Candidate, RolledBack, Recovery };

class FlovaBootControl {
 public:
  virtual ~FlovaBootControl() {}
  virtual FlovaOtaStrategy strategy() const = 0;
  virtual FlovaBootState state() const = 0;
  virtual const char* layoutVersion() const = 0;
  virtual const char* activeSlot() const = 0;
  virtual uint32_t maxImageBytes() const = 0;
  virtual const char* rollbackReason() const = 0;
  virtual bool confirmCandidate() = 0;
  virtual bool rollbackCandidate() = 0;
};

class FlovaLegacyBootControl : public FlovaBootControl {
 public:
  FlovaOtaStrategy strategy() const override { return FlovaOtaStrategy::None; }
  FlovaBootState state() const override { return FlovaBootState::Stable; }
  const char* layoutVersion() const override { return "legacy"; }
  const char* activeSlot() const override { return "single"; }
  uint32_t maxImageBytes() const override { return 0; }
  const char* rollbackReason() const override { return ""; }
  bool confirmCandidate() override { return true; }
  bool rollbackCandidate() override { return false; }
};
