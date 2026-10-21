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
  strcpy(settings.token, "discard-legacy-default-token");
  strcpy(settings.customCa, "discard-legacy-default-ca");
  assert(migrateLegacy(legacy, settings));
  assert(strcmp(settings.url, legacy.url) == 0);
  assert(strcmp(settings.ssid, "test-network") == 0);
  assert(strcmp(settings.webPassword, "test-admin-fixture-only") == 0);
  assert(!settings.token[0] && !settings.customCa[0]);
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

void testTokenIsBoundToHttpsOrigin() {
  Settings before = sample();
  strcpy(before.url, "https://my-nightscout.example");
  strcpy(before.token, "test-readonly-token");
  strcpy(before.customCa, "test-CA-placeholder");
  Settings after{};
  assert(withUpdatedUrl(before, "https://MY-nightscout.example/path", after));
  assert(strcmp(after.token, before.token) == 0);
  assert(strcmp(after.customCa, before.customCa) == 0);
  assert(withUpdatedUrl(before, "https://another-nightscout.example", after));
  assert(!after.token[0] && !after.customCa[0]);
  assert(strcmp(after.wifiPassword, before.wifiPassword) == 0);
  assert(withUpdatedUrl(before, "http://my-nightscout.example", after));
  assert(!after.token[0]);
  assert(!sameHttpsOrigin(before.url, "https://my-nightscout.example:8443"));
  assert(!sameHttpsOrigin(before.url, "https://my-nightscout.example.attacker.invalid"));
}

void testServerTokenUpdateAndClear() {
  Settings current = sample();
  Settings updated{};
  assert(!updateServer(current, "http://192.168.1.100:8000", "token-fixture", false, "", false, updated));
  assert(updateServer(current, "https://nightscout.example", "token-fixture", false, "", false, updated));
  assert(strcmp(updated.token, "token-fixture") == 0);
  current = updated;
  assert(updateServer(current, current.url, "", false, "", false, updated));
  assert(strcmp(updated.token, "token-fixture") == 0);
  assert(updateServer(current, current.url, "", true, "", false, updated));
  assert(!updated.token[0]);
  assert(!updateServer(current, current.url, "new", true, "", false, updated));
  assert(!updateServer(current, current.url, "bad\r\ntoken", false, "", false, updated));
  assert(!updateServer(current, current.url, "", false, "-----BEGIN PRIVATE KEY-----", false, updated));
}

void testVersionTwoMigrationKeepsWifiAndAdmin() {
  V2Settings old{};
  old.magic = V2_MAGIC;
  strcpy(old.url, "https://nightscout.example");
  strcpy(old.ssid, "old-network");
  strcpy(old.wifiPassword, "old-fixture-password");
  strcpy(old.webUsername, "old-admin");
  strcpy(old.webPassword, "old-admin-fixture-password");
  Settings migrated = sample();
  strcpy(migrated.token, "discard-compile-default");
  strcpy(migrated.customCa, "discard-old-default");
  assert(migrateV2(old, migrated));
  assert(configured(migrated));
  assert(strcmp(migrated.url, old.url) == 0 && strcmp(migrated.ssid, old.ssid) == 0);
  assert(strcmp(migrated.wifiPassword, old.wifiPassword) == 0);
  assert(strcmp(migrated.webPassword, old.webPassword) == 0);
  assert(!migrated.token[0] && !migrated.customCa[0]);
  memset(old.webPassword, 'x', sizeof(old.webPassword));
  assert(!migrateV2(old, migrated));
}

int main() {
  testFirstBootNeedsProvisioning();
  testWifiValidation();
  testAdminValidation();
  testCorruptStorageRejected();
  testLegacyMigrationPreservesDefaults();
  testUrlChangePreservesCredentials();
  testTokenIsBoundToHttpsOrigin();
  testServerTokenUpdateAndClear();
  testVersionTwoMigrationKeepsWifiAndAdmin();
  puts("9 settings test groups passed");
}
