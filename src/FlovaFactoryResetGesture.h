#pragma once

#include <stdint.h>

class FlovaFactoryResetGesture {
 public:
  enum Profile : uint8_t { Hold = 0, TapThenHold = 1 };
  enum Event : uint8_t {
    None,
    Armed,
    TapAccepted,
    HoldStarted,
    ReleaseRequested,
    Confirmed,
    WindowClosed,
    ConfirmationExpired
  };

  void configure(uint32_t holdMs = 10000, Profile profile = TapThenHold,
                 uint8_t tapCount = 3, uint32_t windowMs = 60000,
                 uint32_t debounceMs = 50, bool releaseConfirm = true) {
    holdMs_ = holdMs;
    profile_ = profile;
    tapCount_ = tapCount;
    windowMs_ = windowMs;
    debounceMs_ = debounceMs;
    releaseConfirm_ = releaseConfirm;
    state_ = AwaitRelease;
    taps_ = 0;
    windowStartedMs_ = stateStartedMs_ = rawChangedMs_ = releasedSinceMs_ = 0;
    rawPressed_ = stablePressed_ = false;
  }

  Event update(bool pressed, uint32_t now) {
    if (state_ == Disabled) return None;
    if (!windowStartedMs_) windowStartedMs_ = now ? now : 1;
    if (windowMs_ && now - windowStartedMs_ >= windowMs_) {
      state_ = Disabled;
      return WindowClosed;
    }

    if (pressed != rawPressed_) {
      rawPressed_ = pressed;
      rawChangedMs_ = now;
    }
    if (rawPressed_ != stablePressed_ && now - rawChangedMs_ >= debounceMs_)
      stablePressed_ = rawPressed_;

    switch (state_) {
      case AwaitRelease:
        if (stablePressed_) {
          releasedSinceMs_ = 0;
        } else {
          if (!releasedSinceMs_) releasedSinceMs_ = now ? now : 1;
          if (now - releasedSinceMs_ >= 500UL) {
            state_ = profile_ == Hold ? AwaitHold : AwaitTapPress;
            stateStartedMs_ = now;
            taps_ = 0;
            return Armed;
          }
        }
        break;
      case AwaitTapPress:
        if (stablePressed_) {
          state_ = AwaitTapRelease;
          stateStartedMs_ = now;
        } else if (taps_ && now - stateStartedMs_ > 2000UL) {
          restart(now);
        }
        break;
      case AwaitTapRelease:
        if (!stablePressed_) {
          if (now - stateStartedMs_ > 2000UL) {
            restart(now);
            break;
          }
          taps_++;
          state_ = taps_ == tapCount_ ? AwaitHold : AwaitTapPress;
          stateStartedMs_ = now;
          return TapAccepted;
        }
        if (now - stateStartedMs_ > 2000UL) restart(0);
        break;
      case AwaitHold:
        if (stablePressed_) {
          state_ = Holding;
          stateStartedMs_ = now;
          return HoldStarted;
        }
        if (now - stateStartedMs_ > 3000UL) restart(now);
        break;
      case Holding:
        if (!stablePressed_) {
          restart(now);
        } else if (now - stateStartedMs_ >= holdMs_) {
          if (!releaseConfirm_) {
            state_ = Disabled;
            return Confirmed;
          }
          state_ = AwaitConfirmRelease;
          stateStartedMs_ = now;
          return ReleaseRequested;
        }
        break;
      case AwaitConfirmRelease:
        if (!stablePressed_) {
          state_ = Disabled;
          return Confirmed;
        }
        if (now - stateStartedMs_ > 5000UL) {
          state_ = Disabled;
          return ConfirmationExpired;
        }
        break;
      case Disabled:
        break;
    }
    return None;
  }

  bool showingProgress() const {
    return state_ == Holding || state_ == AwaitConfirmRelease;
  }

 private:
  enum State : uint8_t {
    AwaitRelease,
    AwaitTapPress,
    AwaitTapRelease,
    AwaitHold,
    Holding,
    AwaitConfirmRelease,
    Disabled
  };

  void restart(uint32_t now) {
    state_ = AwaitRelease;
    releasedSinceMs_ = now;
    taps_ = 0;
  }

  State state_ = Disabled;
  uint8_t taps_ = 0;
  bool rawPressed_ = false;
  bool stablePressed_ = false;
  uint32_t holdMs_ = 10000;
  Profile profile_ = TapThenHold;
  uint8_t tapCount_ = 3;
  uint32_t windowMs_ = 60000;
  uint32_t debounceMs_ = 50;
  bool releaseConfirm_ = true;
  uint32_t windowStartedMs_ = 0;
  uint32_t stateStartedMs_ = 0;
  uint32_t rawChangedMs_ = 0;
  uint32_t releasedSinceMs_ = 0;
};
