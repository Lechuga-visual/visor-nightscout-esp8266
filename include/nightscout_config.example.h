#pragma once

// Copy to include/nightscout_config.h locally. That file is excluded from Git.
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr char DEVICE_HOSTNAME[] = "glucosa-display";

// Computer running simulator/server.py, on the same Wi-Fi as the Wemos.
// No trailing /api/v1 path. The URL can later be changed through the device web UI.
constexpr char DEFAULT_NIGHTSCOUT_URL[] = "http://192.168.1.100:8000";

constexpr char WEB_USERNAME[] = "admin";
// Choose your own password (12 or more characters). Empty disables the web UI/OTA.
constexpr char WEB_PASSWORD[] = "";

// Optional read-only Nightscout access token. Leave blank for the simulator.
// Sent as a query parameter only over HTTPS; never sent to an HTTP server.
constexpr char NIGHTSCOUT_TOKEN[] = "";

// For a real HTTPS server, paste its trusted root CA PEM here. Certificate and
// hostname verification stay enabled. An empty CA intentionally rejects HTTPS.
static const char NIGHTSCOUT_ROOT_CA[] PROGMEM = R"PEM()PEM";
