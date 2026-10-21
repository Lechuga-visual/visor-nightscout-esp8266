#pragma once

#include <math.h>
#include "patient_label.h"

namespace view_appearance {
constexpr uint32_t MAGIC = 0x4E534131;
constexpr size_t OFFSET = patient_label::OFFSET + sizeof(patient_label::Label);
enum class Mode : uint8_t { Graph, Lateral, Centered };
enum class Theme : uint8_t { Mint, Red, Blue, Pink, Cyan, Custom };
struct Palette { uint32_t accent, background, number; };
struct Settings {
  uint32_t magic = MAGIC;
  uint32_t accent = 0x74EDBC;
  uint32_t background = 0x0B1114;
  uint32_t number = 0x74EDBC;
  Mode mode = Mode::Graph;
  Theme theme = Theme::Mint;
};
static_assert(OFFSET + sizeof(Settings) <= device_config::STORAGE_SIZE, "Appearance exceeds EEPROM allocation");
static_assert(sizeof(Settings) == 20, "Keep the tagged appearance layout stable");

inline uint16_t rgb565(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}
inline double linear(unsigned channel) {
  const double value = channel / 255.0;
  return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}
inline double luminance(uint32_t rgb) {
  // Validate the quantized color the physical display will actually draw.
  const uint16_t color = rgb565(rgb);
  return 0.2126 * linear(((color >> 11) & 31) * 255 / 31) +
         0.7152 * linear(((color >> 5) & 63) * 255 / 63) +
         0.0722 * linear((color & 31) * 255 / 31);
}
inline double contrast(uint32_t first, uint32_t second) {
  const double a = luminance(first), b = luminance(second);
  return a > b ? (a + 0.05) / (b + 0.05) : (b + 0.05) / (a + 0.05);
}
inline bool readable(const Palette& palette) {
  return palette.accent <= 0xFFFFFF && palette.background <= 0xFFFFFF && palette.number <= 0xFFFFFF &&
         contrast(palette.number, palette.background) >= 4.5 &&
         contrast(palette.accent, palette.background) >= 4.5 &&
         contrast(0xD3E0DD, palette.background) >= 4.5 &&
         contrast(0x9AABB0, palette.background) >= 3.0 &&
         contrast(0xFF828B, palette.background) >= 3.0 &&
         contrast(0xFFBD73, palette.background) >= 3.0;
}
inline Palette preset(Theme theme) {
  uint32_t accent = 0x74EDBC;
  switch (theme) {
    case Theme::Red: accent = 0xFF675E; break;
    case Theme::Blue: accent = 0x699BFF; break;
    case Theme::Pink: accent = 0xF69BD1; break;
    case Theme::Cyan: accent = 0x72D9F5; break;
    default: break;
  }
  return {accent, 0x0B1114, accent};
}
inline Palette palette(const Settings& settings) {
  return settings.theme == Theme::Custom ? Palette{settings.accent, settings.background, settings.number}
                                          : preset(settings.theme);
}
inline bool valid(const Settings& settings) {
  return settings.magic == MAGIC && static_cast<unsigned>(settings.mode) <= 2 &&
         static_cast<unsigned>(settings.theme) <= 5 &&
         readable({settings.accent, settings.background, settings.number});
}
inline bool color(const char* text, uint32_t& output) {
  if (!text || strlen(text) != 7 || text[0] != '#') return false;
  uint32_t result = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = text[i];
    const int digit = c >= '0' && c <= '9' ? c - '0' :
                      c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                      c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (digit < 0) return false;
    result = result * 16 + digit;
  }
  output = result;
  return true;
}
inline bool choice(const char* text, unsigned maximum, uint8_t& output) {
  if (!text || !text[0] || text[1] || text[0] < '0' || static_cast<unsigned>(text[0] - '0') > maximum) return false;
  output = text[0] - '0';
  return true;
}
// Missing fields preserve the stored value, supporting a mode-only selector.
// Assign only after validating everything, so a rejected request is atomic.
inline bool update(const Settings& current, const char* mode, const char* theme,
                   const char* accent, const char* background, const char* number, Settings& output) {
  Settings candidate = current;
  uint8_t selected;
  if (mode) { if (!choice(mode, 2, selected)) return false; candidate.mode = static_cast<Mode>(selected); }
  if (theme) { if (!choice(theme, 5, selected)) return false; candidate.theme = static_cast<Theme>(selected); }
  if (accent && !color(accent, candidate.accent)) return false;
  if (background && !color(background, candidate.background)) return false;
  if (number && !color(number, candidate.number)) return false;
  if (!valid(candidate)) return false;
  output = candidate;
  return true;
}
}  // namespace view_appearance
