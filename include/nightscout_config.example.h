#pragma once

// Optional compile-time defaults. Without a private config, the device starts
// a password-protected setup Wi-Fi and asks for these values in its web page.
// If needed, copy to include/nightscout_config.h locally (excluded from Git).
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr char DEVICE_HOSTNAME[] = "glucosa-display";

// Base URL of your Nightscout server. Leave empty for first-boot web setup.
// No trailing /api/v1 path. The URL can later be changed through the device web UI.
constexpr char DEFAULT_NIGHTSCOUT_URL[] = "";

constexpr char WEB_USERNAME[] = "admin";
// 12+ characters. Leave empty to choose a password through first-boot setup.
constexpr char WEB_PASSWORD[] = "";

// Optional compile-time default. Normally enter this in the device web form.
// Sent only over HTTPS; never copied into the UI.
constexpr char NIGHTSCOUT_TOKEN[] = "";

// Optional private CA default (PEM). Normally leave empty to use the public root
// store. A custom CA can also be entered in the device web form without rebuilding.
static const char NIGHTSCOUT_ROOT_CA[] PROGMEM = R"PEM()PEM";
