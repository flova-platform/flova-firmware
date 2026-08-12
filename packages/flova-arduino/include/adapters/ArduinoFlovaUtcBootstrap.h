#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

// Supplies UTC for TLS without installing a global SNTP configuration or
// changing the application's timezone. Network ownership remains external.
template <typename Udp>
class ArduinoFlovaUtcBootstrap {
 public:
  void run(bool networkConnected) {
    if (ready()) {
      stop();
      return;
    }
    if (!networkConnected) {
      stop();
      lastRequestAt_ = 0;
      return;
    }
    if (!started_ && !udp_.begin(0)) return;
    started_ = true;

    const int packetSize = udp_.parsePacket();
    if (packetSize >= static_cast<int>(sizeof(packet_))) {
      const int count = udp_.read(packet_, sizeof(packet_));
      if (count == static_cast<int>(sizeof(packet_))) applyResponse();
    } else if (packetSize > 0) {
      while (udp_.available()) udp_.read();
    }
    if (ready()) {
      stop();
      return;
    }

    const uint32_t now = millis();
    if (lastRequestAt_ && now - lastRequestAt_ < 5000UL) return;
    memset(packet_, 0, sizeof(packet_));
    packet_[0] = 0x1b;
    if (udp_.beginPacket("pool.ntp.org", 123) == 1) {
      udp_.write(packet_, sizeof(packet_));
      udp_.endPacket();
    }
    lastRequestAt_ = now ? now : 1;
  }

  bool ready() const { return time(nullptr) >= 1700000000; }

 private:
  void applyResponse() {
    static const uint32_t kNtpEpochOffset = 2208988800UL;
    const uint8_t leapIndicator = packet_[0] >> 6;
    const uint8_t mode = packet_[0] & 0x07;
    const uint8_t stratum = packet_[1];
    if (leapIndicator == 3 || mode != 4 || stratum == 0 || stratum > 15)
      return;
    const uint32_t ntpSeconds =
        (static_cast<uint32_t>(packet_[40]) << 24) |
        (static_cast<uint32_t>(packet_[41]) << 16) |
        (static_cast<uint32_t>(packet_[42]) << 8) |
        static_cast<uint32_t>(packet_[43]);
    if (ntpSeconds <= kNtpEpochOffset + 1700000000UL) return;
    timeval value = {};
    value.tv_sec = static_cast<time_t>(ntpSeconds - kNtpEpochOffset);
    settimeofday(&value, nullptr);
  }

  void stop() {
    if (started_) udp_.stop();
    started_ = false;
  }

  Udp udp_;
  uint8_t packet_[48] = {};
  uint32_t lastRequestAt_ = 0;
  bool started_ = false;
};
