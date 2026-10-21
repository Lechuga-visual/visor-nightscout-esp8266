#pragma once

#include <stdint.h>
#include <string.h>

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

// RFC 7231 IMF-fixdate. This is current response time, never a glucose timestamp.
// The caller must authenticate TLS before using this header to set the clock.
inline bool parseHttpDate(const char* text, int64_t& result) {
  if (!text || strlen(text) != 29 || text[3] != ',' || text[4] != ' ' ||
      text[7] != ' ' || text[11] != ' ' || text[16] != ' ' || text[19] != ':' ||
      text[22] != ':' || strcmp(text + 25, " GMT") != 0) return false;
  const unsigned positions[] = {5, 6, 12, 13, 14, 15, 17, 18, 20, 21, 23, 24};
  for (unsigned position : positions) if (text[position] < '0' || text[position] > '9') return false;
  const int year = (text[12] - '0') * 1000 + (text[13] - '0') * 100 +
                   (text[14] - '0') * 10 + text[15] - '0';
  const int day = (text[5] - '0') * 10 + text[6] - '0';
  const int hour = (text[17] - '0') * 10 + text[18] - '0';
  const int minute = (text[20] - '0') * 10 + text[21] - '0';
  const int second = (text[23] - '0') * 10 + text[24] - '0';
  if (year < 2024 || year > 2100 || hour > 23 || minute > 59 || second > 59 || day < 1) return false;
  constexpr char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int month = 0;
  for (; month < 12; ++month) if (memcmp(text + 8, months + month * 3, 3) == 0) break;
  if (month == 12) return false;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const int lengths[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > lengths[month]) return false;
  int64_t days = 0;
  for (int y = 1970; y < year; ++y) days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  for (int m = 0; m < month; ++m) days += lengths[m];
  days += day - 1;
  constexpr char weekdays[] = "SunMonTueWedThuFriSat";
  if (memcmp(text, weekdays + ((days + 4) % 7) * 3, 3) != 0) return false;
  const int64_t value = days * 86400 + hour * 3600 + minute * 60 + second;
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
