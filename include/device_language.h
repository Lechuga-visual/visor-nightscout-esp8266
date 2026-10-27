#pragma once
#include "night_display.h"

namespace device_language {
enum class Locale : uint8_t { Spanish, English };
constexpr uint32_t MAGIC = 0x4E534C31;
constexpr size_t OFFSET = night_display::OFFSET + sizeof(night_display::Settings);
struct Settings {
  uint32_t magic = MAGIC;
  Locale locale = Locale::Spanish;
  uint8_t reserved[3] = {};
};
static_assert(sizeof(Settings) == 8, "Keep language settings stable");
static_assert(OFFSET == 3872, "Keep all earlier settings at their original offsets");
static_assert(OFFSET + sizeof(Settings) <= device_config::STORAGE_SIZE, "Language exceeds EEPROM");
inline bool valid(const Settings& s) {
  return s.magic == MAGIC && static_cast<unsigned>(s.locale) <= 1 &&
         !s.reserved[0] && !s.reserved[1] && !s.reserved[2];
}
inline bool parse(const char* value, Settings& output) {
  if (!value || (strcmp(value,"es") && strcmp(value,"en"))) return false;
  Settings candidate;
  candidate.locale = !strcmp(value,"en") ? Locale::English : Locale::Spanish;
  output = candidate;
  return true;
}
inline const char* code(const Settings& s) { return s.locale == Locale::English ? "en" : "es"; }
}  // namespace device_language
