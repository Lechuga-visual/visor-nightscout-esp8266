#pragma once
#include "view_appearance.h"
#ifdef ARDUINO
#include <pgmspace.h>
#define NIGHT_TEXT(text) PSTR(text)
#else
#define NIGHT_TEXT(text) text
#endif

namespace night_display {
constexpr uint32_t MAGIC = 0x4E534231;
constexpr size_t OFFSET = view_appearance::OFFSET + sizeof(view_appearance::Settings);
struct Settings {
  uint32_t magic = MAGIC;
  uint16_t start = 22 * 60;
  uint16_t end = 7 * 60;
  uint8_t day = 100;
  uint8_t night = 25;
  uint8_t enabled = 0;
  uint8_t zone = 0;
  uint8_t reserved[4] = {};
};
static_assert(sizeof(Settings) == 16, "Keep brightness settings stable");
static_assert(OFFSET == 3856, "Keep existing Wi-Fi, trust, patient and appearance offsets");
static_assert(OFFSET + sizeof(Settings) <= device_config::STORAGE_SIZE, "Brightness exceeds EEPROM");
inline bool valid(const Settings& s) {
  return s.magic == MAGIC && s.start < 1440 && s.end < 1440 && s.start != s.end &&
         s.day >= 10 && s.day <= 100 && s.night >= 10 && s.night <= 100 && s.enabled <= 1 && s.zone < 8 &&
         !s.reserved[0] && !s.reserved[1] && !s.reserved[2] && !s.reserved[3];
}
inline bool clockMinute(const char* text, uint16_t& result) {
  if (!text || strlen(text) != 5 || text[2] != ':') return false;
  for (unsigned i = 0; i < 5; ++i) if (i != 2 && (text[i] < '0' || text[i] > '9')) return false;
  const unsigned hour = (text[0]-'0')*10 + text[1]-'0', minute = (text[3]-'0')*10 + text[4]-'0';
  if (hour > 23 || minute > 59) return false;
  result = hour * 60 + minute;
  return true;
}
inline bool number(const char* text, unsigned low, unsigned high, uint8_t& result) {
  if (!text || !*text || strlen(text) > 3) return false;
  unsigned value = 0;
  for (const char* c = text; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    value = value * 10 + *c - '0';
  }
  if (value < low || value > high) return false;
  result = value;
  return true;
}
inline bool update(const char* start, const char* end, const char* day, const char* night,
                   const char* zone, bool enabled, Settings& output) {
  Settings s;
  if (!clockMinute(start,s.start) || !clockMinute(end,s.end) || !number(day,10,100,s.day) ||
      !number(night,10,100,s.night) || !number(zone,0,7,s.zone)) return false;
  s.enabled = enabled;
  if (!valid(s)) return false;
  output = s;
  return true;
}
inline bool nightAt(const Settings& s, int minute, bool clockValid) {
  if (!s.enabled || !clockValid || minute < 0 || minute >= 1440) return false;
  return s.start < s.end ? minute >= s.start && minute < s.end : minute >= s.start || minute < s.end;
}
inline uint8_t intensity(const Settings& s, int minute, bool clockValid) {
  return nightAt(s,minute,clockValid) ? s.night : s.day;
}
// Scale each RGB565 channel once, including text, warnings, graph and background.
inline uint16_t scale565(uint16_t color, uint8_t percent) {
  return (((((color >> 11) & 31) * percent + 50) / 100) << 11) |
         (((((color >> 5) & 63) * percent + 50) / 100) << 5) |
         (((color & 31) * percent + 50) / 100);
}
struct Preview {
  bool enabled = false;
  uint8_t percent = 100;
  uint32_t started = 0;
  void start(uint8_t value, uint32_t now) { percent = value; started = now; enabled = true; }
  bool active(uint32_t now) {
    if (enabled && static_cast<uint32_t>(now-started) >= 5000) enabled = false;
    return enabled;
  }
};
inline const char* timezoneName(unsigned zone) {
  switch (zone) {
    case 1: return NIGHT_TEXT("America/Punta_Arenas · Magallanes/Aysén");
    case 2: return NIGHT_TEXT("America/Argentina/Buenos_Aires");
    case 3: return NIGHT_TEXT("America/Lima");
    case 4: return NIGHT_TEXT("America/Bogota");
    case 5: return NIGHT_TEXT("America/Mexico_City");
    case 6: return NIGHT_TEXT("Europe/Madrid");
    case 7: return NIGHT_TEXT("Etc/UTC");
    default: return NIGHT_TEXT("America/Santiago");
  }
}
inline const char* timezoneRule(unsigned zone) {
  switch (zone) {
    case 1: case 2: return NIGHT_TEXT("<-03>3");
    case 3: case 4: return NIGHT_TEXT("<-05>5");
    case 5: return NIGHT_TEXT("CST6");
    case 6: return NIGHT_TEXT("CET-1CEST,M3.5.0,M10.5.0/3");
    case 7: return NIGHT_TEXT("UTC0");
    default: return NIGHT_TEXT("<-04>4<-03>,M9.1.6/24,M4.1.6/24");
  }
}
}  // namespace night_display
#undef NIGHT_TEXT
