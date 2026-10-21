#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>

#if __has_include("nightscout_config.h")
#include "nightscout_config.h"
#else
#include "nightscout_config.example.h"
#endif

namespace {
constexpr char VERSION[] = "0.1.0";
constexpr uint32_t POLL_MS = 30000;
constexpr time_t VALID_CLOCK = 1704067200;  // 2024-01-01 UTC
TFT_eSPI display;
ESP8266WebServer web(80);
ESP8266HTTPUpdateServer updater;
String baseUrl;
String message = "Iniciando";
String csrf;
bool storageReady = false;
bool webEnabled = false;
bool attemptedFetch = false;
uint32_t lastFetch = 0;
uint32_t lastRender = 0;
String lastFrame;

struct Reading {
  bool available = false;
  bool synthetic = false;
  int sgv = 0;
  int64_t dateMs = 0;
  String direction;
} reading;

struct Settings {
  uint32_t magic;
  char url[201];
};
constexpr uint32_t SETTINGS_MAGIC = 0x4E534331;

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String urlEncode(const char* value) {
  String result;
  constexpr char hex[] = "0123456789ABCDEF";
  for (const uint8_t* c = reinterpret_cast<const uint8_t*>(value); *c; ++c) {
    if (isalnum(*c) || *c == '-' || *c == '_' || *c == '.' || *c == '~') {
      result += static_cast<char>(*c);
    } else {
      result += '%';
      result += hex[*c >> 4];
      result += hex[*c & 15];
    }
  }
  return result;
}

bool validUrl(const String& url) {
  const bool https = url.startsWith("https://");
  if (!https && !url.startsWith("http://")) return false;
  if (url.length() > 200) return false;
  const String authority = url.substring(https ? 8 : 7);
  if (authority.length() == 0 || authority[0] == '/') return false;
  for (size_t i = 0; i < authority.length(); ++i) {
    if (authority[i] <= ' ' || authority[i] == '@' || authority[i] == '?' ||
        authority[i] == '#' || authority[i] == '\\') return false;
  }
  return true;
}

bool authenticated() {
  web.sendHeader("Cache-Control", "no-store");
  if (!webEnabled) {
    web.send(503, "text/plain; charset=utf-8", "Configura WEB_PASSWORD (12 caracteres minimo) y carga por USB.");
    return false;
  }
  if (!web.authenticate(WEB_USERNAME, WEB_PASSWORD)) {
    web.requestAuthentication();
    return false;
  }
  return true;
}

void homePage() {
  if (!authenticated()) return;
  String page = F("<!doctype html><html lang='es'><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>Pantalla de glucosa</title><style>body{font:18px system-ui;"
                  "max-width:650px;margin:30px auto;padding:20px}input{width:95%;padding:8px}"
                  "button{padding:10px;margin-top:10px}</style><h1>Pantalla de glucosa</h1>");
  page += "<p>Firmware " + String(VERSION) + " · IP " + WiFi.localIP().toString() + "</p>";
  page += "<p>Estado: " + htmlEscape(message) + "</p>";
  page += F("<form method='post' action='/settings'><label>URL base de Nightscout o simulador"
            "<input name='url' type='url' maxlength='200' required value='");
  page += htmlEscape(baseUrl);
  page += "'></label><input type='hidden' name='csrf' value='" + csrf + "'>";
  page += F("<button>Guardar servidor</button></form>"
            "<p>Usa la IP del computador que ejecuta el simulador, por ejemplo "
            "<code>http://192.168.1.100:8000</code>.</p>");
  if (!storageReady) page += F("<p>No se puede guardar la URL: EEPROM no esta disponible.</p>");
  page += F("<p><a href='/update'>Actualizar firmware (.bin)</a></p>"
            "<p>Actualizaciones disponibles solo para uso en una red local de confianza.</p></html>");
  web.send(200, "text/html; charset=utf-8", page);
}

void saveSettings() {
  if (!authenticated()) return;
  if (!web.hasArg("csrf") || web.arg("csrf") != csrf) {
    web.send(403, "text/plain", "Formulario caducado. Recarga la pagina.");
    return;
  }
  String candidate = web.arg("url");
  candidate.trim();
  while (candidate.endsWith("/")) candidate.remove(candidate.length() - 1);
  if (!validUrl(candidate)) {
    web.send(400, "text/plain", "URL HTTP(S) invalida. No incluyas credenciales, query ni /api/v1.");
    return;
  }
  if (!storageReady) {
    web.send(500, "text/plain", "No se pudo guardar la configuracion.");
    return;
  }
  Settings saved{};
  saved.magic = SETTINGS_MAGIC;
  candidate.toCharArray(saved.url, sizeof(saved.url));
  EEPROM.put(0, saved);
  if (!EEPROM.commit()) {
    web.send(500, "text/plain", "No se pudo guardar la configuracion.");
    return;
  }
  baseUrl = candidate;
  reading.available = false;
  attemptedFetch = false;
  message = "Servidor cambiado";
  web.sendHeader("Location", "/");
  web.send(303);
}

void parseReading(HTTPClient& http) {
  StaticJsonDocument<1536> document;
  const auto error = deserializeJson(document, http.getStream());
  if (error || !document.is<JsonArray>()) {
    message = "JSON invalido";
    return;
  }
  if (document.size() == 0) {
    reading.available = false;
    message = "Servidor sin lecturas";
    return;
  }
  JsonObject entry = document[0];
  const int value = entry["sgv"] | 0;
  const int64_t date = entry["date"].as<int64_t>();
  const time_t now = time(nullptr);
  if (value <= 0 || value > 1000 || date < static_cast<int64_t>(VALID_CLOCK) * 1000 ||
      (now >= VALID_CLOCK && date > (static_cast<int64_t>(now) + 300) * 1000)) {
    message = "Lectura invalida";
    return;
  }
  reading.available = true;
  reading.sgv = value;
  reading.dateMs = date;
  reading.direction = String(entry["direction"] | "NONE").substring(0, 24);
  reading.synthetic = entry["synthetic"] | false;
  message = "Conectado";
}

void fetchReading() {
  String endpoint = baseUrl + "/api/v1/entries.json?count=1";
  const bool https = baseUrl.startsWith("https://");
  if (NIGHTSCOUT_TOKEN[0]) {
    if (!https) {
      message = "Token requiere HTTPS";
      return;
    }
    endpoint += "&token=" + urlEncode(NIGHTSCOUT_TOKEN);
  }
  HTTPClient http;
  http.useHTTP10(true);  // A non-chunked body can be parsed straight from the stream.
  http.setTimeout(2500);
  http.setReuse(false);
  WiFiClient plain;
  BearSSL::WiFiClientSecure secure;
  // Both the TLS client and its trust anchors must outlive GET and JSON parsing.
  BearSSL::X509List trust(NIGHTSCOUT_ROOT_CA);
  if (https) {
    if (strlen_P(NIGHTSCOUT_ROOT_CA) == 0) {
      message = "Configura CA para HTTPS";
      return;
    }
    if (time(nullptr) < VALID_CLOCK) {
      message = "Esperando hora NTP";
      return;
    }
    secure.setTrustAnchors(&trust);
    secure.setTimeout(2500);
    if (!http.begin(secure, endpoint)) {
      message = "URL HTTPS invalida";
      return;
    }
  } else if (!http.begin(plain, endpoint)) {
    message = "URL HTTP invalida";
    return;
  }
  const int result = http.GET();
  if (result == HTTP_CODE_OK) {
    parseReading(http);
  } else {
    // Never print the endpoint or token to Serial or the web UI.
    message = result > 0 ? "Error HTTP " + String(result) : "Error de conexion";
  }
  http.end();
}

void drawTrend(uint16_t color) {
  const String& direction = reading.direction;
  int dx = 0, dy = 0;
  if (direction == "Flat") dx = 22;
  else if (direction == "SingleUp" || direction == "DoubleUp") dy = -22;
  else if (direction == "SingleDown" || direction == "DoubleDown") dy = 22;
  else if (direction == "FortyFiveUp") { dx = 16; dy = -16; }
  else if (direction == "FortyFiveDown") { dx = 16; dy = 16; }
  else { display.drawString("?", 120, 146, 4); return; }
  for (int offset = -1; offset <= 1; ++offset) {
    display.drawLine(120 - dx / 2 + offset, 147 - dy / 2,
                     120 + dx / 2 + offset, 147 + dy / 2, color);
    display.drawLine(120 + dx / 2, 147 + dy / 2,
                     120 + dx / 2 - (dx + dy) / 3, 147 + dy / 2 - (dy - dx) / 3, color);
    display.drawLine(120 + dx / 2, 147 + dy / 2,
                     120 + dx / 2 - (dx - dy) / 3, 147 + dy / 2 - (dy + dx) / 3, color);
  }
  if (direction.startsWith("Double")) display.drawString("2x", 157, 147, 2);
}

void render() {
  const time_t now = time(nullptr);
  const bool clockReady = now >= VALID_CLOCK;
  const int64_t age = clockReady && reading.available
                          ? max(static_cast<int64_t>(0), static_cast<int64_t>(now) - reading.dateMs / 1000)
                          : 0;
  const bool stale = clockReady && reading.available && age >= 600;
  const String ageText = !reading.available ? "Sin lecturas" :
                         !clockReady ? "Hora sin sincronizar" :
                         "Hace " + String(static_cast<unsigned long>(age / 60)) + " min";
  String frame = String(reading.available) + reading.sgv + reading.direction + ageText +
                 message + WiFi.localIP().toString() + String(reading.synthetic);
  if (frame == lastFrame) return;
  lastFrame = frame;
  uint16_t color = reading.sgv < 70 || reading.sgv > 180 ? TFT_ORANGE : TFT_GREEN;
  if (stale || !clockReady || message != "Conectado") color = TFT_LIGHTGREY;
  display.fillScreen(TFT_BLACK);
  display.drawCircle(120, 120, 116, stale ? TFT_ORANGE : TFT_DARKGREY);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawString(reading.synthetic ? "DEMO - DATOS FICTICIOS" : "GLUCOSA", 120, 35, 2);
  display.setTextColor(color, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(reading.available ? String(reading.sgv) : "---", 120, 85, 4);
  display.setTextSize(1);
  display.drawString("mg/dL", 120, 119, 2);
  if (reading.available) drawTrend(color);
  display.setTextColor(stale ? TFT_ORANGE : TFT_WHITE, TFT_BLACK);
  display.drawString(stale ? "LECTURA ANTIGUA" : ageText, 120, 174, 2);
  display.drawString(message, 120, 195, 2);
  display.drawString(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Sin Wi-Fi", 120, 215, 2);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(0);
  EEPROM.begin(256);
  storageReady = EEPROM.length() == 256;
  baseUrl = DEFAULT_NIGHTSCOUT_URL;
  if (storageReady) {
    Settings saved{};
    EEPROM.get(0, saved);
    if (saved.magic == SETTINGS_MAGIC && memchr(saved.url, '\0', sizeof(saved.url)) && validUrl(saved.url))
      baseUrl = saved.url;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(DEVICE_HOSTNAME);
  WiFi.setAutoReconnect(true);
  if (WIFI_SSID[0]) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  message = WIFI_SSID[0] ? "Conectando Wi-Fi" : "Configura Wi-Fi por USB";
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  csrf = String(ESP.random(), HEX) + String(ESP.random(), HEX);
  webEnabled = WEB_USERNAME[0] && strlen(WEB_PASSWORD) >= 12;
  web.on("/", HTTP_GET, homePage);
  web.on("/settings", HTTP_POST, saveSettings);
  if (webEnabled) updater.setup(&web, "/update", WEB_USERNAME, WEB_PASSWORD);
  web.begin();
  render();
}

void loop() {
  web.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    static bool mdnsStarted = false;
    if (!mdnsStarted) mdnsStarted = MDNS.begin(DEVICE_HOSTNAME);
    if (mdnsStarted) MDNS.update();
    if (!attemptedFetch || millis() - lastFetch >= POLL_MS) {
      attemptedFetch = true;
      fetchReading();
      lastFetch = millis();
    }
  } else if (WIFI_SSID[0]) {
    message = "Sin Wi-Fi";
  }
  if (millis() - lastRender >= 1000) {
    render();
    lastRender = millis();
  }
  delay(1);
}
