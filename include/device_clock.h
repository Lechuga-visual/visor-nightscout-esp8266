#pragma once

#include <stdint.h>

namespace device_clock {
constexpr int64_t EARLIEST_UNIX = 1704067200;  // 2024-01-01 UTC.
constexpr int64_t LATEST_UNIX = 4102444800;    // 2100-01-01 UTC; rejects millisecond inputs.
constexpr uint32_t RETRY_MS = 30000;

inline bool plausible(int64_t value) { return value >= EARLIEST_UNIX && value <= LATEST_UNIX; }

inline bool parseBrowserTime(const char* text, int64_t& result) {
  if (!text || !*text) return false;
  int64_t value = 0;
  unsigned digits = 0;
  for (const char* c = text; *c; ++c) {
    if (*c < '0' || *c > '9' || ++digits > 10) return false;
    value = value * 10 + (*c - '0');
  }
  if (!plausible(value)) return false;
  result = value;
  return true;
}

class SyncSchedule {
 public:
  bool poll(bool connected, bool clockReady, uint32_t now) {
    if (!connected) { wasConnected_ = false; return false; }
    const bool due = !wasConnected_ || (!clockReady && (!attempted_ || now - lastAttempt_ >= RETRY_MS));
    wasConnected_ = true;
    if (due) restarted(now);
    return due;
  }

  void restarted(uint32_t now) {
    wasConnected_ = true;
    attempted_ = true;
    lastAttempt_ = now;
  }

 private:
  bool wasConnected_ = false;
  bool attempted_ = false;
  uint32_t lastAttempt_ = 0;
};
}  // namespace device_clock
