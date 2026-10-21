#include "device_settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace device_config;

Settings sample() {
  Settings settings{};
  settings.magic = MAGIC;
  strcpy(settings.url, "http://192.168.1.100:8000");
  strcpy(settings.ssid, "test-network");
  strcpy(settings.wifiPassword, "test-fixture-only");
  strcpy(settings.webUsername, "admin");
  strcpy(settings.webPassword, "test-admin-fixture-only");
  return settings;
}

void testFirstBootNeedsProvisioning() {
  Settings empty{};
  assert(!configured(empty));
  Settings defaults{};
  defaults.magic = MAGIC;
  strcpy(defaults.url, "http://192.168.1.100:8000");
  strcpy(defaults.webUsername, "admin");
  assert(validStoredSettings(defaults));
  assert(!configured(defaults));
  assert(configured(sample()));
}

void testWifiValidation() {
  Settings settings = sample();
  settings.ssid[0] = '\0';
  assert(!validWifi(settings));
  settings = sample();
  strcpy(settings.wifiPassword, "short");
  assert(!validWifi(settings));
  settings.wifiPassword[0] = '\0';
  assert(validWifi(settings));  // Explicitly selected open network.
  memset(settings.wifiPassword, 'a', 64);
  settings.wifiPassword[64] = '\0';
  assert(validWifi(settings));  // A hexadecimal raw WPA key.
  settings.wifiPassword[63] = 'z';
  assert(!validWifi(settings));
}

void testAdminValidation() {
  Settings settings = sample();
  strcpy(settings.webPassword, "too-short");
  assert(!validAdmin(settings));
  settings = sample();
  strcpy(settings.webUsername, "bad:user");
  assert(!validAdmin(settings));
  settings = sample();
  settings.webUsername[0] = '\0';
  assert(!validAdmin(settings));
}

void testCorruptStorageRejected() {
  Settings settings = sample();
  settings.magic = 0xffffffff;
  assert(!validStoredSettings(settings));
  settings = sample();
  memset(settings.wifiPassword, 'x', sizeof(settings.wifiPassword));
  assert(!validStoredSettings(settings));
  settings = sample();
  memset(settings.url, 'x', sizeof(settings.url));
  assert(!validStoredSettings(settings));
  assert(!validBaseUrl("http://"));
  assert(!validBaseUrl("ftp://example.com"));
  assert(!validBaseUrl("https://user:password@example.com"));
  assert(!validBaseUrl("https://example.com?token=hidden"));
  assert(validBaseUrl("https://example.com"));
}

void testLegacyMigrationPreservesDefaults() {
  LegacySettings legacy{};
  legacy.magic = LEGACY_MAGIC;
  strcpy(legacy.url, "http://192.168.1.5:9000");
  Settings settings = sample();
  assert(migrateLegacy(legacy, settings));
  assert(strcmp(settings.url, legacy.url) == 0);
  assert(strcmp(settings.ssid, "test-network") == 0);
  assert(strcmp(settings.webPassword, "test-admin-fixture-only") == 0);
  assert(configured(settings));
  legacy.magic = 0;
  assert(!migrateLegacy(legacy, settings));
}

void testUrlChangePreservesCredentials() {
  const Settings before = sample();
  Settings after{};
  assert(withUpdatedUrl(before, "http://192.168.1.7:8000", after));
  assert(configured(after));
  assert(strcmp(after.url, "http://192.168.1.7:8000") == 0);
  assert(strcmp(after.ssid, before.ssid) == 0);
  assert(strcmp(after.wifiPassword, before.wifiPassword) == 0);
  assert(strcmp(after.webUsername, before.webUsername) == 0);
  assert(strcmp(after.webPassword, before.webPassword) == 0);
  assert(!withUpdatedUrl(before, "http://", after));
  assert(strcmp(after.url, "http://192.168.1.7:8000") == 0);
}

int main() {
  testFirstBootNeedsProvisioning();
  testWifiValidation();
  testAdminValidation();
  testCorruptStorageRejected();
  testLegacyMigrationPreservesDefaults();
  testUrlChangePreservesCredentials();
  puts("6 settings test groups passed");
}
