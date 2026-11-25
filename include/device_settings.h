#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace device_config {
constexpr uint32_t MAGIC = 0x4E534335;
constexpr uint32_t V4_MAGIC = 0x4E534334;
constexpr uint32_t V3_MAGIC = 0x4E534333;
constexpr uint32_t V2_MAGIC = 0x4E534332;
constexpr uint32_t LEGACY_MAGIC = 0x4E534331;
constexpr size_t STORAGE_SIZE = 4096;

struct V2Settings {
  uint32_t magic;
  char url[201];
  char ssid[33];
  char wifiPassword[65];
  char webUsername[33];
  char webPassword[65];
};

struct Settings {
  uint32_t magic;
  char url[201];
  char ssid[33];
  char wifiPassword[65];
  char webUsername[33];
  char webPassword[65];
  char token[257];
};

struct LegacySettings {
  uint32_t magic;
  char url[201];
};

constexpr size_t LEGACY_SETTINGS_SLOT_SIZE = 3732;
static_assert(sizeof(Settings) == 660, "Keep the historical Wi-Fi, admin and token layout");
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
         terminated(settings.token) &&
         validBaseUrl(settings.url);
}

inline bool migrateV3OrV4(Settings& settings) {
  if (settings.magic != V3_MAGIC && settings.magic != V4_MAGIC) return false;
  settings.magic = MAGIC;
  return validStoredSettings(settings);
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
  return validStoredSettings(settings) && validWifi(settings) && validAdmin(settings) &&
         (!settings.token[0] || strncmp(settings.url, "https://", 8) == 0);
}

inline bool sameHttpsOrigin(const char* first, const char* second) {
  if (strncmp(first, "https://", 8) != 0 || strncmp(second, "https://", 8) != 0) return false;
  first += 8;
  second += 8;
  while (*first && *first != '/' && *second && *second != '/') {
    const char a = *first >= 'A' && *first <= 'Z' ? *first + ('a' - 'A') : *first;
    const char b = *second >= 'A' && *second <= 'Z' ? *second + ('a' - 'A') : *second;
    if (a != b) return false;
    ++first;
    ++second;
  }
  return (!*first || *first == '/') && (!*second || *second == '/');
}

inline bool withUpdatedUrl(const Settings& current, const char* url, Settings& updated) {
  if (!validStoredSettings(current) || !validBaseUrl(url)) return false;
  updated = current;
  memcpy(updated.url, url, strlen(url) + 1);
  if (!sameHttpsOrigin(current.url, url)) {
    // An old server's credential must never be forwarded to a different origin.
    memset(updated.token, 0, sizeof(updated.token));
  }
  return true;
}

inline bool updateServer(const Settings& current, const char* url, const char* token,
                         bool clearToken, Settings& updated) {
  if (!url || !token || strlen(token) > 256 || (clearToken && *token)) return false;
  for (const unsigned char* c = reinterpret_cast<const unsigned char*>(token); *c; ++c) {
    if (*c <= ' ' || *c >= 127) return false;
  }
  if (!withUpdatedUrl(current, url, updated)) return false;
  if (clearToken) memset(updated.token, 0, sizeof(updated.token));
  else if (*token) { memset(updated.token, 0, sizeof(updated.token)); memcpy(updated.token, token, strlen(token)); }
  return !updated.token[0] || strncmp(updated.url, "https://", 8) == 0;
}

inline bool migrateV2(const V2Settings& previous, Settings& settings) {
  if (previous.magic != V2_MAGIC || !terminated(previous.url) ||
      !terminated(previous.ssid) || !terminated(previous.wifiPassword) ||
      !terminated(previous.webUsername) || !terminated(previous.webPassword) ||
      !validBaseUrl(previous.url)) return false;
  memcpy(settings.url, previous.url, sizeof(settings.url));
  memcpy(settings.ssid, previous.ssid, sizeof(settings.ssid));
  memcpy(settings.wifiPassword, previous.wifiPassword, sizeof(settings.wifiPassword));
  memcpy(settings.webUsername, previous.webUsername, sizeof(settings.webUsername));
  memcpy(settings.webPassword, previous.webPassword, sizeof(settings.webPassword));
  memset(settings.token, 0, sizeof(settings.token));
  settings.magic = MAGIC;
  return true;
}

inline bool migrateLegacy(const LegacySettings& legacy, Settings& settings) {
  if (legacy.magic != LEGACY_MAGIC || !terminated(legacy.url) || !validBaseUrl(legacy.url)) return false;
  // Retain current credential defaults; legacy firmware persisted only the URL.
  memcpy(settings.url, legacy.url, sizeof(settings.url));
  memset(settings.token, 0, sizeof(settings.token));
  settings.magic = MAGIC;
  return true;
}
}  // namespace device_config
