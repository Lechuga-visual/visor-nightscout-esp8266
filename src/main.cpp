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
#include <sys/time.h>
#include <coredecls.h>
#include <limits>
#include <sntp.h>
#include "device_clock.h"
#include "device_settings.h"
#include "public_ca_store.h"
#include "pinned_key.h"

#if __has_include("nightscout_config.h")
#include "nightscout_config.h"
#else
#include "nightscout_config.example.h"
#endif

namespace {
constexpr char VERSION[] = "0.4.0";
constexpr uint32_t POLL_MS = 30000;
constexpr time_t VALID_CLOCK = device_clock::EARLIEST_UNIX;
TFT_eSPI display;
ESP8266WebServer web(80);
ESP8266HTTPUpdateServer updater;
PublicCaStore publicRoots;
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
device_clock::SyncSchedule clockSchedule;
constexpr const char* NTP_SERVERS[] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};
uint32_t ntpAttempts = 0;
enum class ClockSource { Unknown, Ntp, Browser, Server };
ClockSource clockSource = ClockSource::Unknown;
WiFiEventHandler wifiDisconnectedHandler;
uint8_t lastWifiReason = 0;
uint32_t wifiDisconnects = 0;
uint32_t lastWifiReconnect = 0;
bool webRecentlyUsed = false;
uint32_t lastWebActivity = 0;
int lastTlsError = 0;
uint32_t heapBeforeTls = 0;
uint32_t largestBlockBeforeTls = 0;

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
  if (device_config::pinnedPublicKey(settings.customCa)) sntp_stop();
  return true;
}

bool authenticated() {
  webRecentlyUsed = true;
  lastWebActivity = millis();
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

bool startNtp() {
  if (setupMode || WiFi.status() != WL_CONNECTED) return false;
  const unsigned first = ntpAttempts % 3;
  configTime(0, 0, NTP_SERVERS[first], NTP_SERVERS[(first + 1) % 3], NTP_SERVERS[(first + 2) % 3]);
  ++ntpAttempts;
  clockSchedule.restarted(millis());
  return true;
}

void appendClockControls(String& page) {
  page += F("<section><h2>Hora del dispositivo</h2><p>");
  const time_t now = time(nullptr);
  if (device_clock::plausible(now)) {
    struct tm utc;
    char formatted[32];
    gmtime_r(&now, &utc);
    strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S UTC", &utc);
    page += formatted;
    page += clockSource == ClockSource::Ntp ? F(" · NTP") :
            clockSource == ClockSource::Browser ? F(" · hora del navegador") :
            clockSource == ClockSource::Server ? F(" · hora del servidor HTTPS") : F(" · reloj disponible");
  } else page += device_config::pinnedPublicKey(settings.customCa)
                    ? F("Sin hora. La clave pública fija permite consultar HTTPS igualmente.")
                    : F("Hora sin sincronizar. HTTPS con certificados CA espera una hora válida.");
  page += F("</p><p>Intentos NTP: ");
  page += String(ntpAttempts);
  page += F(". Servidores: pool.ntp.org, time.google.com y time.cloudflare.com (UDP 123).</p>"
            "<form method='post' action='/clock/retry'><input type='hidden' name='csrf' value='");
  page += csrf;
  page += F("'><button>Reintentar NTP</button></form>"
            "<p>Si NTP no responde, comprueba que la fecha y hora de este teléfono o computador "
            "sean correctas y usa su reloj. Se mantienen las verificaciones HTTPS.</p>"
            "<form method='post' action='/clock/browser' onsubmit=\"document.getElementById('browser-epoch').value="
            "Math.floor(Date.now()/1000)\"><input type='hidden' id='browser-epoch' name='epoch'>"
            "<input type='hidden' name='csrf' value='");
  page += csrf;
  page += F("'><button>Usar hora de este teléfono/PC</button></form>"
            "<p>Esta hora dura hasta el próximo reinicio.</p>");
  page += device_config::pinnedPublicKey(settings.customCa)
              ? F("<p>En modo clave fija no se inicia NTP automáticamente. Se toma la hora de la respuesta HTTPS autenticada del servidor.</p></section>")
              : F("<p>NTP sigue intentando sincronizar en segundo plano.</p></section>");
}

bool clockRequestAllowed() {
  if (!authenticated()) return false;
  if (!web.hasArg("csrf") || web.arg("csrf") != csrf) {
    web.send(403, "text/plain", "Formulario caducado. Recarga la pagina.");
    return false;
  }
  if (setupMode || WiFi.status() != WL_CONNECTED) {
    web.send(409, "text/plain", "Conecta primero el Wemos a tu Wi-Fi.");
    return false;
  }
  return true;
}

void retryNtp() {
  if (!clockRequestAllowed()) return;
  startNtp();
  attemptedFetch = false;
  web.sendHeader("Location", "/");
  web.send(303);
}

void useBrowserClock() {
  if (!clockRequestAllowed()) return;
  int64_t epoch = 0;
  if (!device_clock::parseBrowserTime(web.arg("epoch").c_str(), epoch) ||
      epoch > static_cast<int64_t>(std::numeric_limits<time_t>::max())) {
    web.send(400, "text/plain; charset=utf-8", "Hora no válida. Comprueba la fecha y hora del teléfono/PC.");
    return;
  }
  struct timeval value{};
  value.tv_sec = static_cast<time_t>(epoch);
  if (settimeofday(&value, nullptr) != 0) {
    web.send(500, "text/plain", "No se pudo ajustar el reloj.");
    return;
  }
  clockSource = ClockSource::Browser;
  attemptedFetch = false;
  lastFrame = "";
  message = "Hora del navegador";
  web.send(200, "text/html; charset=utf-8",
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<h1>Hora ajustada</h1><p>El reloj del Wemos recibió la hora del navegador. "
           "La consulta al servidor se reanudará en unos segundos.</p><p><a href='/'>Volver</a></p>");
}

void appendServerFields(String& page) {
  page += F("<label>URL base del simulador o Nightscout"
            "<input name='url' type='url' maxlength='200' required value='");
  page += htmlEscape(baseUrl);
  page += F("'></label><p>Solo la URL, sin /api/v1 ni API secret.</p>"
            "<label>Token Nightscout de solo lectura (opcional)"
            "<input name='token' type='password' maxlength='256' autocomplete='off'></label>");
  page += settings.token[0] ? F("<p>Hay un token guardado; dejar vacío lo conserva para el mismo servidor.</p>")
                            : F("<p>Sin token guardado. El simulador no lo necesita.</p>");
  page += F("<label><input name='clear_token' type='checkbox' value='1' style='width:auto'>"
            " Borrar token guardado</label><p>El token solo se envía por HTTPS. "
            "Al cambiar de servidor se borra el anterior; introduce el token del nuevo servidor.</p>"
            "<details><summary>Seguridad HTTPS y opción sin NTP</summary><p>El firmware incluye ");
  page += String(PublicCaStore::rootCount());
  page += F(" autoridades de confianza. Para servidores públicos normalmente no necesitas pegar un certificado.</p>"
            "<label>CA privada o clave pública fija del servidor (PEM, opcional)"
            "<textarea name='custom_ca' maxlength='3072' rows='6' style='width:100%;box-sizing:border-box' "
            "placeholder='-----BEGIN CERTIFICATE-----'></textarea></label>");
  if (device_config::pinnedPublicKey(settings.customCa))
    page += F("<p>Modo actual: clave pública fija, sin requerir NTP. Dejar vacío la conserva para el mismo servidor.</p>");
  else page += settings.customCa[0] ? F("<p>Hay una CA privada guardada. Dejar vacío la conserva para el mismo servidor.</p>")
                                  : F("<p>Se utilizarán los certificados públicos incluidos.</p>");
  page += F("<label><input name='clear_ca' type='checkbox' value='1' style='width:auto'>"
            " Borrar CA o clave fija y usar certificados públicos</label>"
            "<p>Para consultar sin NTP, pega la clave pública verificada del servidor, con "
            "BEGIN PUBLIC KEY, obtenida desde un PC con tools/export_server_key.py. "
            "HTTPS exige que el servidor demuestre que posee esa clave; en este modo "
            "la identidad se fija por clave, sin comprobar la cadena, el nombre ni las fechas del certificado. "
            "Si el proveedor cambia la clave, deberás actualizarla. No se acepta automáticamente una clave nueva.</p>"
            "<p>Con certificados CA se comprueban cadena, nombre y hora. Nunca pegues una clave privada.</p></details>");
}

bool validPinnedKey(BearSSL::PublicKey& key) {
  if (key.isRSA()) return true;
  if (!key.isEC()) return false;
  // PublicKey owns a mutable malloc-backed point despite its const accessor.
  return normalizeOwnedPinnedEcPoint(*const_cast<br_ec_public_key*>(key.getEC()));
}

bool validCustomTrust(const char* pem) {
  if (!pem[0]) return true;
  if (!device_config::trustPemEnvelope(pem)) return false;
  if (device_config::pinnedPublicKey(pem)) {
    BearSSL::PublicKey key;
    return key.parse(pem) && validPinnedKey(key);
  }
  BearSSL::X509List certificate;
  return certificate.append(pem) && certificate.getCount() == 1 &&
         (certificate.getTrustAnchors()[0].flags & BR_X509_TA_CA);
}

bool serverFields(device_config::Settings& candidate, const String& url) {
  String token = web.arg("token");
  token.trim();
  String ca = web.arg("custom_ca");
  ca.trim();
  return device_config::updateServer(settings, url.c_str(), token.c_str(),
                                    web.arg("clear_token") == "1", ca.c_str(),
                                    web.arg("clear_ca") == "1", candidate) &&
         validCustomTrust(candidate.customCa);
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
            "</label><p>Introduce de nuevo la clave al guardar. Déjala vacía solo si la red es abierta.</p>");
  appendServerFields(page);
  page += F("<p>Simulador: http://192.168.1.100:8000 (IP de tu computador).</p>"
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
  // CA settings are several KB; allocate candidates on the heap, not the ESP's 4 KB stack.
  std::unique_ptr<device_config::Settings> candidate(new (std::nothrow) device_config::Settings);
  if (!candidate) { web.send(503, "text/plain", "Memoria insuficiente. Intenta nuevamente."); return; }
  if (!serverFields(*candidate, url)) {
    web.send(400, "text/plain; charset=utf-8", "Revisa el token y el PEM: una CA válida o una clave pública RSA/EC. Nunca una clave privada. El token requiere HTTPS. No combines borrar con introducir un valor nuevo.");
    return;
  }
  ssid.toCharArray(candidate->ssid, sizeof(candidate->ssid));
  wifiPassword.toCharArray(candidate->wifiPassword, sizeof(candidate->wifiPassword));
  username.toCharArray(candidate->webUsername, sizeof(candidate->webUsername));
  password.toCharArray(candidate->webPassword, sizeof(candidate->webPassword));
  if (!device_config::configured(*candidate)) {
    web.send(400, "text/plain; charset=utf-8", "SSID y usuario obligatorios; clave Wi-Fi de 8 a 63 caracteres (o vacia para red abierta); clave de administracion de 12 a 64.");
    return;
  }
  if (!persistSettings(*candidate)) {
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
  page += "<p>Wi-Fi: " + String(WiFi.RSSI()) + " dBm · desconexiones: " + String(wifiDisconnects) +
          " · último motivo: " + String(lastWifiReason) + "</p>";
  appendClockControls(page);
  page += F("<form method='post' action='/settings'>");
  appendServerFields(page);
  page += "<input type='hidden' name='csrf' value='" + csrf + "'>";
  page += F("<button>Guardar servidor</button></form>"
            "<p>Usa la IP del computador que ejecuta el simulador, por ejemplo "
            "<code>http://192.168.1.100:8000</code>.</p>");
  if (!storageReady) page += F("<p>No se puede guardar la URL: EEPROM no esta disponible.</p>");
  page += F("<p><a href='/configure'>Configurar Wi-Fi y contraseñas</a></p>"
            "<p><a href='/update'>Actualizar firmware (.bin)</a> · <a href='/diagnostics'>Diagnóstico</a></p>"
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
  std::unique_ptr<device_config::Settings> saved(new (std::nothrow) device_config::Settings);
  if (!saved) { web.send(503, "text/plain", "Memoria insuficiente."); return; }
  if (!serverFields(*saved, candidate)) {
    web.send(400, "text/plain; charset=utf-8", "Revisa el token, HTTPS y el PEM de CA o clave pública.");
    return;
  }
  if (!persistSettings(*saved)) {
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
  // Other Nightscout plugins may add large fields that the display does not need.
  StaticJsonDocument<256> filter;
  filter[0]["sgv"] = true;
  filter[0]["date"] = true;
  filter[0]["direction"] = true;
  filter[0]["synthetic"] = true;
  const auto error = deserializeJson(document, http.getStream(), DeserializationOption::Filter(filter));
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
  const bool pinned = device_config::pinnedPublicKey(settings.customCa);
  // Avoid allocating the TLS stack and buffers until a connection is possible.
  if (https && !pinned && !device_clock::plausible(time(nullptr))) {
    message = ntpAttempts < 2 ? "Esperando hora NTP" : "Sin hora: abre la web";
    return;
  }
  if (settings.token[0]) {
    if (!https) {
      message = "Token requiere HTTPS";
      return;
    }
    endpoint += "&token=" + urlEncode(settings.token);
  }
  // Destruction order matters: HTTPClient -> TLS client -> pinned key / trust anchors.
  WiFiClient plain;
  std::unique_ptr<BearSSL::X509List> trust;
  std::unique_ptr<BearSSL::PublicKey> key;
  std::unique_ptr<BearSSL::WiFiClientSecure> secure;
  HTTPClient http;
  http.useHTTP10(true);  // A non-chunked body can be parsed straight from the stream.
  http.setTimeout(2500);
  http.setReuse(false);
  if (https) {
    heapBeforeTls = ESP.getFreeHeap();
    largestBlockBeforeTls = ESP.getMaxFreeBlockSize();
    secure.reset(new (std::nothrow) BearSSL::WiFiClientSecure);
    if (!secure) { message = "Sin memoria para TLS"; return; }
    if (pinned) {
      key.reset(new (std::nothrow) BearSSL::PublicKey(settings.customCa));
      if (!key || !validPinnedKey(*key)) { message = "Clave publica invalida"; return; }
      secure->setKnownKey(key.get());
    } else if (settings.customCa[0]) {
      trust.reset(new (std::nothrow) BearSSL::X509List(settings.customCa));
      if (!trust || trust->getCount() != 1) { message = "CA propia invalida"; return; }
      secure->setTrustAnchors(trust.get());
    } else secure->setCertStore(&publicRoots);
    secure->setTimeout(5000);
    Serial.printf("TLS inicio: modo=%s heap=%u bloque=%u RSSI=%d\n",
                  pinned ? "clave" : "CA", heapBeforeTls, largestBlockBeforeTls, WiFi.RSSI());
    if (!http.begin(*secure, endpoint)) {
      message = "URL HTTPS invalida";
      return;
    }
  } else if (!http.begin(plain, endpoint)) {
    message = "URL HTTP invalida";
    return;
  }
  const char* responseHeaders[] = {"Date", "Age"};
  http.collectHeaders(responseHeaders, 2);
  http.addHeader("Cache-Control", "no-cache, no-store");
  const int result = http.GET();
  lastTlsError = secure ? secure->getLastSSLError() : 0;
  Serial.printf("Consulta: HTTP=%d TLS=%d WiFi=%d heap=%u\n", result, lastTlsError,
                WiFi.status(), ESP.getFreeHeap());
  if (result == HTTP_CODE_OK) {
    if (pinned && https) {
      // The pinned key authenticated TLS before any response headers were read.
      // Ignore cached responses; a reading's own date is never used as 'now'.
      const String age = http.header("Age");
      int64_t epoch = 0;
      if ((!age.length() || age == "0") && device_clock::parseHttpDate(http.header("Date").c_str(), epoch) &&
          epoch <= static_cast<int64_t>(std::numeric_limits<time_t>::max())) {
        struct timeval value{};
        value.tv_sec = static_cast<time_t>(epoch);
        if (settimeofday(&value, nullptr) == 0) clockSource = ClockSource::Server;
      }
    }
    parseReading(http);
  } else {
    // Never print the endpoint or token to Serial or the web UI.
    if (result == 401 || result == 403) message = "Revisa token (" + String(result) + ")";
    else if (result < 0 && lastTlsError != 0) message = "Error TLS " + String(lastTlsError);
    else message = result > 0 ? "Error HTTP " + String(result) : "Error de conexion";
  }
  http.end();
}

void diagnostics() {
  if (!authenticated()) return;
  StaticJsonDocument<512> document;
  document["firmware"] = VERSION;
  document["uptime_seconds"] = millis() / 1000;
  document["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  document["wifi_status"] = static_cast<int>(WiFi.status());
  document["rssi_dbm"] = WiFi.RSSI();
  document["wifi_disconnects"] = wifiDisconnects;
  document["last_wifi_reason"] = lastWifiReason;
  document["free_heap"] = ESP.getFreeHeap();
  document["largest_free_block"] = ESP.getMaxFreeBlockSize();
  document["heap_before_tls"] = heapBeforeTls;
  document["largest_block_before_tls"] = largestBlockBeforeTls;
  document["last_tls_error"] = lastTlsError;
  document["clock_valid"] = device_clock::plausible(time(nullptr));
  document["clock_source"] = clockSource == ClockSource::Server ? "https_server" :
                             clockSource == ClockSource::Browser ? "browser" :
                             clockSource == ClockSource::Ntp ? "ntp" : "unknown";
  document["ntp_attempts"] = ntpAttempts;
  document["tls_authentication"] = device_config::pinnedPublicKey(settings.customCa) ? "pinned_public_key" : "certificate_authority";
  String body;
  serializeJson(document, body);
  web.send(200, "application/json", body);
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
  const bool clockReady = device_clock::plausible(now);
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
  display.drawString(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() :
                     "Sin Wi-Fi (" + String(lastWifiReason) + ")", 120, 215, 2);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("\nNightscout %s; reinicio: %s\n", VERSION, ESP.getResetReason().c_str());
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
  strlcpy(settings.token, NIGHTSCOUT_TOKEN, sizeof(settings.token));
  strncpy_P(settings.customCa, NIGHTSCOUT_ROOT_CA, sizeof(settings.customCa) - 1);
  if (storageReady) {
    std::unique_ptr<device_config::Settings> saved(new (std::nothrow) device_config::Settings);
    if (saved) {
      EEPROM.get(0, *saved);
      if (device_config::validStoredSettings(*saved)) settings = *saved;
      else {
        device_config::V2Settings previous{};
        EEPROM.get(0, previous);
        if (!device_config::migrateV2(previous, settings)) {
          device_config::LegacySettings legacy{};
          EEPROM.get(0, legacy);
          device_config::migrateLegacy(legacy, settings);
        }
      }
    }
  }
  baseUrl = settings.url;
  csrf = String(ESP.random(), HEX) + String(ESP.random(), HEX);
  settimeofday_cb([](bool fromSntp) {
    if (fromSntp) { clockSource = ClockSource::Ntp; attemptedFetch = false; }
    lastFrame = "";
  });
  WiFi.persistent(false);
  WiFi.hostname(DEVICE_HOSTNAME);
  WiFi.setAutoReconnect(true);
  wifiDisconnectedHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
    lastWifiReason = static_cast<uint8_t>(event.reason);
    ++wifiDisconnects;
    lastFrame = "";
    Serial.printf("Wi-Fi desconectado: motivo=%u heap=%u\n", lastWifiReason, ESP.getFreeHeap());
  });
  webEnabled = device_config::validAdmin(settings);
  if (!device_config::configured(settings)) startProvisioning();
  else {
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.begin(settings.ssid, settings.wifiPassword);
    message = "Conectando Wi-Fi";
  }
  web.on("/", HTTP_GET, homePage);
  web.on("/configure", HTTP_GET, configurationPage);
  web.on("/configure", HTTP_POST, saveConfiguration);
  web.on("/settings", HTTP_POST, saveSettings);
  web.on("/clock/retry", HTTP_POST, retryNtp);
  web.on("/clock/browser", HTTP_POST, useBrowserClock);
  web.on("/diagnostics", HTTP_GET, diagnostics);
  if (webEnabled) updater.setup(&web, "/update", settings.webUsername, settings.webPassword);
  web.begin();
  render();
}

void loop() {
  web.handleClient();
  if (restartPending && millis() - restartRequestedAt >= 1500) ESP.restart();
  if (!setupMode && WiFi.status() == WL_CONNECTED) {
    hasConnected = true;
    // Start/retry DNS and SNTP only after the station has acquired a connection.
    if (!device_config::pinnedPublicKey(settings.customCa)) {
      if (clockSchedule.poll(true, device_clock::plausible(time(nullptr)), millis())) startNtp();
    } else clockSchedule.poll(false, device_clock::plausible(time(nullptr)), millis());
    static bool mdnsStarted = false;
    if (!mdnsStarted) mdnsStarted = MDNS.begin(DEVICE_HOSTNAME);
    if (mdnsStarted) MDNS.update();
    // Let the browser receive its clock/settings response before starting TLS.
    const bool webIdle = !webRecentlyUsed || millis() - lastWebActivity >= 2000;
    if (webIdle && (!attemptedFetch || millis() - lastFetch >= POLL_MS)) {
      attemptedFetch = true;
      fetchReading();
      lastFetch = millis();
    }
  } else if (!setupMode) {
    clockSchedule.poll(false, device_clock::plausible(time(nullptr)), millis());
    message = "Sin Wi-Fi";
    if (hasConnected && millis() - lastWifiReconnect >= 15000) {
      lastWifiReconnect = millis();
      WiFi.reconnect();
    }
    // Recover from wrong credentials at boot without having to recompile or use USB.
    if (!hasConnected && millis() >= 60000) startProvisioning();
  }
  if (millis() - lastRender >= 1000) {
    render();
    lastRender = millis();
  }
  delay(1);
}
