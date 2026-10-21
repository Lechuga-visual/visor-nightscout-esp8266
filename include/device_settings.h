#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace device_config {
constexpr uint32_t MAGIC = 0x4E534332;
constexpr uint32_t LEGACY_MAGIC = 0x4E534331;
constexpr size_t STORAGE_SIZE = 512;

struct Settings {
  uint32_t magic;
  char url[201];
  char ssid[33];
  char wifiPassword[65];
  char webUsername[33];
  char webPassword[65];
};

struct LegacySettings {
  uint32_t magic;
  char url[201];
};

static_assert(sizeof(Settings) <= STORAGE_SIZE, "Settings exceed EEPROM allocation");

template <size_t N>
inline bool terminated(const char (&value)[N]) {
  return memchr(value, '\0', N) != nullptr;
}

inline bool validBaseUrl(const char* url) {
  const bool https = strncmp(url, "https://", 8) == 0;
  if (!https && strncmp(url, "http://", 7) != 0) return false;
  if (strlen(url) > 200) return false;
  const char* authority = url + (https ? 8 : 7);
  if (!*authority || *authority == '/') return false;
  for (const unsigned char* c = reinterpret_cast<const unsigned char*>(authority); *c; ++c) {
    if (*c <= ' ' || *c == '@' || *c == '?' || *c == '#' || *c == '\\') return false;
  }
  return true;
}

inline bool validStoredSettings(const Settings& settings) {
  return settings.magic == MAGIC && terminated(settings.url) &&
         terminated(settings.ssid) && terminated(settings.wifiPassword) &&
         terminated(settings.webUsername) && terminated(settings.webPassword) &&
         validBaseUrl(settings.url);
}

inline bool validWifi(const Settings& settings) {
  if (!terminated(settings.ssid) || !terminated(settings.wifiPassword) || !settings.ssid[0]) return false;
  const size_t length = strlen(settings.wifiPassword);
  if (length == 0 || (length >= 8 && length <= 63)) return true;
  // A 64-character WPA pre-shared key must be hexadecimal.
  if (length != 64) return false;
  for (const char* c = settings.wifiPassword; *c; ++c) {
    if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F'))) return false;
  }
  return true;
}

inline bool validAdmin(const Settings& settings) {
  if (!terminated(settings.webUsername) || !terminated(settings.webPassword) ||
      !settings.webUsername[0] || strlen(settings.webPassword) < 12) return false;
  for (const unsigned char* c = reinterpret_cast<const unsigned char*>(settings.webUsername); *c; ++c) {
    if (*c < ' ' || *c == ':' || *c == 127) return false;
  }
  return true;
}

inline bool configured(const Settings& settings) {
  return validStoredSettings(settings) && validWifi(settings) && validAdmin(settings);
}

inline bool withUpdatedUrl(const Settings& current, const char* url, Settings& updated) {
  if (!validStoredSettings(current) || !validBaseUrl(url)) return false;
  updated = current;
  memcpy(updated.url, url, strlen(url) + 1);
  return true;
}

inline bool migrateLegacy(const LegacySettings& legacy, Settings& settings) {
  if (legacy.magic != LEGACY_MAGIC || !terminated(legacy.url) || !validBaseUrl(legacy.url)) return false;
  // Retain current credential defaults; legacy firmware persisted only the URL.
  memcpy(settings.url, legacy.url, sizeof(settings.url));
  settings.magic = MAGIC;
  return true;
}
}  // namespace device_config
