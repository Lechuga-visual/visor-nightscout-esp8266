#pragma once

#include "device_settings.h"

namespace patient_label {
constexpr uint32_t MAGIC = 0x4E534E31;
constexpr size_t MAX_CHARACTERS = 24;
constexpr size_t OFFSET = sizeof(device_config::Settings);
// Separate tagged extension: all existing v3 settings retain their offsets.
struct Label {
  uint32_t magic = MAGIC;
  char name[97]{};
};
static_assert(OFFSET + sizeof(Label) <= device_config::STORAGE_SIZE, "Name exceeds EEPROM allocation");

inline bool validName(const char* name) {
  if (!name || strlen(name) > 96) return false;
  const auto* cursor = reinterpret_cast<const unsigned char*>(name);
  size_t count = 0;
  while (*cursor) {
    uint32_t codepoint = *cursor++;
    unsigned extra = 0;
    uint32_t minimum = 0;
    if (codepoint >= 0xC2 && codepoint <= 0xDF) { extra = 1; codepoint &= 0x1F; minimum = 0x80; }
    else if (codepoint >= 0xE0 && codepoint <= 0xEF) { extra = 2; codepoint &= 0x0F; minimum = 0x800; }
    else if (codepoint >= 0xF0 && codepoint <= 0xF4) { extra = 3; codepoint &= 7; minimum = 0x10000; }
    else if (codepoint >= 0x80) return false;
    for (unsigned i = 0; i < extra; ++i) {
      if ((*cursor & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (*cursor++ & 0x3F);
    }
    if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
        codepoint < 32 || (codepoint >= 127 && codepoint <= 159) || ++count > MAX_CHARACTERS) return false;
  }
  return true;
}
inline bool valid(const Label& label) {
  return label.magic == MAGIC && device_config::terminated(label.name) && validName(label.name);
}
// The small built-in display font has ASCII glyphs. Preserve the original UTF-8
// name in storage/web, but transliterate common Spanish letters on the TFT.
inline void displayName(const Label& label, char (&output)[25]) {
  const auto* cursor = reinterpret_cast<const unsigned char*>(label.name);
  size_t used = 0;
  while (*cursor && used < MAX_CHARACTERS) {
    const unsigned char first = *cursor++;
    char value = first < 128 ? static_cast<char>(first) : '?';
    if (first == 0xC3) {
      const unsigned char second = *cursor;
      const char* source = "\x81\x89\x8D\x93\x9A\x9C\x91\xA1\xA9\xAD\xB3\xBA\xBC\xB1";
      const char* found = strchr(source, second);
      if (found) value = "AEIOUUNaeiouun"[found - source];
    }
    while ((*cursor & 0xC0) == 0x80) ++cursor;
    output[used++] = value;
  }
  output[used] = '\0';
  // Twenty 6-pixel glyphs fit within the circle at the header's height.
  if (used > 20) memcpy(output + 17, "...", 4);
}
}  // namespace patient_label
