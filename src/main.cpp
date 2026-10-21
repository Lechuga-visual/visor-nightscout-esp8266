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
#include "device_settings.h"

#if __has_include("nightscout_config.h")
#include "nightscout_config.h"
#else
#include "nightscout_config.example.h"
#endif

namespace {
constexpr char VERSION[] = "0.2.0";
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
device_config::Settings settings{};
bool setupMode = false;
bool hasConnected = false;
bool restartPending = false;
uint32_t restartRequestedAt = 0;
String setupSsid;
String setupPassword;

struct Reading {
  bool available = false;
  bool synthetic = false;
  int sgv = 0;
  int64_t dateMs = 0;
  String direction;
} reading;

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
  return device_config::validBaseUrl(url.c_str());
}

bool persistSettings(const device_config::Settings& candidate) {
  if (!storageReady || !device_config::validStoredSettings(candidate)) return false;
  EEPROM.put(0, candidate);
  if (!EEPROM.commit()) {
    EEPROM.put(0, settings);  // Restore the in-memory buffer after a failed write.
    return false;
  }
  settings = candidate;
  baseUrl = settings.url;
  return true;
}

bool authenticated() {
  web.sendHeader("Cache-Control", "no-store");
  if (!webEnabled) {
    web.send(503, "text/plain; charset=utf-8", "Completa primero la configuracion inicial.");
    return false;
  }
  if (!web.authenticate(settings.webUsername, settings.webPassword)) {
    web.requestAuthentication();
    return false;
  }
  return true;
}

void configurationPage() {
  if (!setupMode && !authenticated()) return;
  web.sendHeader("Cache-Control", "no-store");
  String page = F("<!doctype html><html lang='es'><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>Configurar pantalla</title><style>body{font:18px system-ui;"
                  "max-width:650px;margin:25px auto;padding:20px}label{display:block;margin:15px 0}"
                  "input{box-sizing:border-box;width:100%;font:inherit;padding:8px}button{font:inherit;padding:10px}"
                  "</style><h1>Configurar pantalla</h1><p>Guarda tu red Wi-Fi de 2,4 GHz y el servidor de datos.</p>"
                  "<form method='post' action='/configure'><label>Nombre de tu Wi-Fi (SSID)"
                  "<input name='ssid' maxlength='32' required value='");
  page += htmlEscape(settings.ssid);
  page += F("'></label><label>Contraseña de tu Wi-Fi"
            "<input name='wifi_password' type='password' maxlength='64' autocomplete='off'>"
            "</label><p>Introduce de nuevo la clave al guardar. Déjala vacía solo si la red es abierta.</p>"
            "<label>URL base del simulador o Nightscout"
            "<input name='url' type='url' maxlength='200' required value='");
  page += htmlEscape(baseUrl);
  page += F("'></label><p>Por ejemplo: http://192.168.1.100:8000 (IP de tu computador).</p>"
            "<label>Usuario de administración<input name='web_username' maxlength='32' required value='");
  page += htmlEscape(settings.webUsername);
  page += F("'></label><label>Contraseña para administrar y actualizar el Wemos"
            "<input name='web_password' type='password' minlength='12' maxlength='64' required "
            "autocomplete='new-password'></label><input type='hidden' name='csrf' value='");
  page += csrf;
  page += F("'><button>Guardar y reiniciar</button></form>"
            "<p>Las contraseñas se guardan en el Wemos. No se muestran en esta página.</p></html>");
  web.send(200, "text/html; charset=utf-8", page);
}

void saveConfiguration() {
  if (!setupMode && !authenticated()) return;
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("csrf") || web.arg("csrf") != csrf) {
    web.send(403, "text/plain", "Formulario caducado. Recarga la pagina.");
    return;
  }
  String url = web.arg("url");
  url.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  const String ssid = web.arg("ssid");
  const String wifiPassword = web.arg("wifi_password");
  String username = web.arg("web_username");
  username.trim();
  const String password = web.arg("web_password");
  if (!validUrl(url) || ssid.length() > 32 || wifiPassword.length() > 64 ||
      username.length() > 32 || password.length() > 64) {
    web.send(400, "text/plain; charset=utf-8", "Revisa la URL y la longitud de los campos.");
    return;
  }
  device_config::Settings candidate{};
  candidate.magic = device_config::MAGIC;
  url.toCharArray(candidate.url, sizeof(candidate.url));
  ssid.toCharArray(candidate.ssid, sizeof(candidate.ssid));
  wifiPassword.toCharArray(candidate.wifiPassword, sizeof(candidate.wifiPassword));
  username.toCharArray(candidate.webUsername, sizeof(candidate.webUsername));
  password.toCharArray(candidate.webPassword, sizeof(candidate.webPassword));
  if (!device_config::configured(candidate)) {
    web.send(400, "text/plain; charset=utf-8", "SSID y usuario obligatorios; clave Wi-Fi de 8 a 63 caracteres (o vacia para red abierta); clave de administracion de 12 a 64.");
    return;
  }
  if (!persistSettings(candidate)) {
    web.send(500, "text/plain", "No se pudo guardar. Intenta nuevamente.");
    return;
  }
  web.send(200, "text/html; charset=utf-8",
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<h1>Configuración guardada</h1><p>El Wemos se reiniciará. Vuelve a conectar tu teléfono "
           "a tu Wi-Fi y abre la IP que aparecerá en la pantalla del Wemos.</p>");
  restartPending = true;
  restartRequestedAt = millis();
}

void startProvisioning() {
  WiFi.mode(WIFI_AP);
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lx", static_cast<unsigned long>(ESP.getChipId() & 0xffffff));
  char password[13];
  snprintf(password, sizeof(password), "%08lx%04lx", static_cast<unsigned long>(ESP.random()),
           static_cast<unsigned long>(ESP.random() & 0xffff));
  setupSsid = "glucosa-" + String(suffix);
  setupPassword = password;
  const IPAddress setupIp(192, 168, 4, 1);
  if (!WiFi.softAPConfig(setupIp, setupIp, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(setupSsid.c_str(), setupPassword.c_str())) {
    setupMode = false;
    message = "Error al crear Wi-Fi";
    return;
  }
  setupMode = true;
  message = "Configuracion inicial";
  lastFrame = "";
}

void homePage() {
  if (setupMode) { configurationPage(); return; }
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
  page += F("<p><a href='/configure'>Configurar Wi-Fi y contraseñas</a></p>"
            "<p><a href='/update'>Actualizar firmware (.bin)</a></p>"
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
  // Changing only the URL must preserve the Wi-Fi and administrator credentials.
  device_config::Settings saved{};
  if (!device_config::withUpdatedUrl(settings, candidate.c_str(), saved)) {
    web.send(400, "text/plain", "Configuracion invalida.");
    return;
  }
  if (!persistSettings(saved)) {
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
  if (setupMode) {
    if (lastFrame == "setup") return;
    lastFrame = "setup";
    display.fillScreen(TFT_BLACK);
    display.drawCircle(120, 120, 116, TFT_CYAN);
    display.setTextDatum(MC_DATUM);
    display.setTextSize(1);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.drawString("CONFIGURAR", 120, 30, 2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString("Conectate a esta Wi-Fi:", 120, 57, 2);
    display.drawString(setupSsid, 120, 79, 2);
    display.drawString("Clave de acceso:", 120, 106, 2);
    display.drawString(setupPassword, 120, 128, 2);
    display.drawString("Abre en el navegador:", 120, 158, 2);
    display.drawString(WiFi.softAPIP().toString(), 120, 181, 2);
    display.drawString("Guarda tu red Wi-Fi", 120, 207, 2);
    return;
  }
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
  EEPROM.begin(device_config::STORAGE_SIZE);
  storageReady = EEPROM.length() == device_config::STORAGE_SIZE;
  settings.magic = device_config::MAGIC;
  strlcpy(settings.url, DEFAULT_NIGHTSCOUT_URL, sizeof(settings.url));
  strlcpy(settings.ssid, WIFI_SSID, sizeof(settings.ssid));
  strlcpy(settings.wifiPassword, WIFI_PASSWORD, sizeof(settings.wifiPassword));
  strlcpy(settings.webUsername, WEB_USERNAME, sizeof(settings.webUsername));
  strlcpy(settings.webPassword, WEB_PASSWORD, sizeof(settings.webPassword));
  if (storageReady) {
    device_config::Settings saved{};
    EEPROM.get(0, saved);
    if (device_config::validStoredSettings(saved)) settings = saved;
    else {
      device_config::LegacySettings legacy{};
      EEPROM.get(0, legacy);
      device_config::migrateLegacy(legacy, settings);
    }
  }
  baseUrl = settings.url;
  csrf = String(ESP.random(), HEX) + String(ESP.random(), HEX);
  WiFi.persistent(false);
  WiFi.hostname(DEVICE_HOSTNAME);
  WiFi.setAutoReconnect(true);
  webEnabled = device_config::validAdmin(settings);
  if (!device_config::configured(settings)) startProvisioning();
  else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.ssid, settings.wifiPassword);
    message = "Conectando Wi-Fi";
    configTime(0, 0, "pool.ntp.org", "time.google.com");
  }
  web.on("/", HTTP_GET, homePage);
  web.on("/configure", HTTP_GET, configurationPage);
  web.on("/configure", HTTP_POST, saveConfiguration);
  web.on("/settings", HTTP_POST, saveSettings);
  if (webEnabled) updater.setup(&web, "/update", settings.webUsername, settings.webPassword);
  web.begin();
  render();
}

void loop() {
  web.handleClient();
  if (restartPending && millis() - restartRequestedAt >= 1500) ESP.restart();
  if (!setupMode && WiFi.status() == WL_CONNECTED) {
    hasConnected = true;
    static bool mdnsStarted = false;
    if (!mdnsStarted) mdnsStarted = MDNS.begin(DEVICE_HOSTNAME);
    if (mdnsStarted) MDNS.update();
    if (!attemptedFetch || millis() - lastFetch >= POLL_MS) {
      attemptedFetch = true;
      fetchReading();
      lastFetch = millis();
    }
  } else if (!setupMode) {
    message = "Sin Wi-Fi";
    // Recover from wrong credentials at boot without having to recompile or use USB.
    if (!hasConnected && millis() >= 60000) startProvisioning();
  }
  if (millis() - lastRender >= 1000) {
    render();
    lastRender = millis();
  }
  delay(1);
}
