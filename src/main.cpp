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
#include "reading_json.h"
#include "generated/web_assets.h"

#if __has_include("nightscout_config.h")
#include "nightscout_config.h"
#else
#include "nightscout_config.example.h"
#endif

namespace {
constexpr char VERSION[] = "0.5.0";
constexpr uint32_t POLL_MS = 30000;
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
uint32_t bootId = 0;
const char* lastOperation = "boot";

struct Reading {
  bool available = false;
  bool synthetic = false;
  int sgv = 0;
  int64_t dateMs = 0;
  String direction;
} reading;
reading_ui::History history;
uint32_t historyVersion = 0;

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

bool authenticated(bool interactive = true) {
  // Read-only dashboard polling must not postpone Nightscout indefinitely.
  if (interactive) { webRecentlyUsed = true; lastWebActivity = millis(); }
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

bool currentForm() {
  if (web.hasArg("csrf") && web.arg("csrf") == csrf) return true;
  lastOperation = "stale_form";
  Serial.printf_P(PSTR("Formulario rechazado antes de validar PEM: sesion=%08x uptime=%lu\n"),
                  bootId, static_cast<unsigned long>(millis() / 1000));
  // A stale form must remain rejected. Give the user a fresh page instead of
  // silently resubmitting old settings or removing CSRF protection.
  web.sendHeader("Cache-Control", "no-store, max-age=0");
  web.send_P(403, PSTR("text/html; charset=utf-8"),
           PSTR("<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<h1>Recarga el formulario</h1><p>Esta página ya no corresponde a la sesión del Wemos. "
           "Puede haber ocurrido un reinicio o una actualización mientras estaba abierta. "
           "La solicitud fue rechazada antes de validar el certificado o la clave y no cambió la configuración.</p>"
           "<p><a href='/'>Abrir página actualizada</a></p>"
           "<p>En Ajustes, vuelve a pegar la clave pública y pulsa Guardar servidor. No uses Atrás para recuperar el formulario.</p>"));
  return false;
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
  if (!currentForm()) return false;
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
  web.sendHeader("Location", "/settings");
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
  lastOperation = "validating_settings";
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
                  "<meta name='color-scheme' content='dark'><link rel='stylesheet' href='/assets/ui.css'>"
                  "<title>Configurar pantalla</title><main class='settings'><a class='back' href='/'>← Volver al panel</a>"
                  "<p class='eyebrow'>TU DISPOSITIVO</p><h1>Configurar pantalla</h1>"
                  "<p class='intro'>Guarda tu red Wi-Fi de 2,4 GHz y el servidor de datos.</p>"
                  "<form class='card' method='post' action='/configure'><label>Nombre de tu Wi-Fi (SSID)"
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
            "<p>Las contraseñas se guardan en el Wemos. No se muestran en esta página.</p></main></html>");
  web.send(200, "text/html; charset=utf-8", page);
}

void saveConfiguration() {
  if (!setupMode && !authenticated()) return;
  web.sendHeader("Cache-Control", "no-store");
  if (!currentForm()) return;
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
  lastOperation = "saving_configuration";
  if (!persistSettings(*candidate)) {
    web.send(500, "text/plain", "No se pudo guardar. Intenta nuevamente.");
    return;
  }
  web.send(200, "text/html; charset=utf-8",
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<h1>Configuración guardada</h1><p>El Wemos se reiniciará. Vuelve a conectar tu teléfono "
           "a tu Wi-Fi y abre la IP que aparecerá en la pantalla del Wemos.</p>");
  restartPending = true;
  lastOperation = "restart_requested";
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

void settingsPage() {
  if (!authenticated()) return;
  String page = F("<!doctype html><html lang='es'><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<meta name='color-scheme' content='dark'><link rel='stylesheet' href='/assets/ui.css'>"
                  "<title>Ajustes · Glucosa</title><main class='settings'><a class='back' href='/'>← Volver al panel</a>"
                  "<p class='eyebrow'>TU DISPOSITIVO</p><h1>Ajustes</h1>"
                  "<p class='intro'>Conexión, servidor y mantenimiento de tu pantalla.</p><section class='card'>");
  page += "<p>Firmware " + String(VERSION) + " · IP " + WiFi.localIP().toString() + "</p>";
  page += "<p>Sesión " + String(bootId, HEX) + " · encendido hace " + String(millis() / 1000) + " s</p>";
  page += "<p>Estado: " + htmlEscape(message) + "</p>";
  page += "<p>Wi-Fi: " + String(WiFi.RSSI()) + " dBm · desconexiones: " + String(wifiDisconnects) +
          " · último motivo: " + String(lastWifiReason) + "</p>";
  page += F("</section><form class='card' method='post' action='/settings'><h2>Servidor de lecturas</h2>");
  appendServerFields(page);
  page += "<input type='hidden' name='csrf' value='" + csrf + "'>";
  page += F("<button>Guardar servidor</button>"
            "<p>Usa la IP del computador que ejecuta el simulador, por ejemplo "
            "<code>http://192.168.1.100:8000</code>.</p></form><section class='card'>");
  appendClockControls(page);
  if (!storageReady) page += F("<p>No se puede guardar la URL: EEPROM no esta disponible.</p>");
  page += F("</section><section class='card'><h2>Mantenimiento</h2><div class='actions'>"
            "<a class='button secondary' href='/configure'>Wi-Fi y contraseñas</a>"
            "<a class='button secondary' href='/firmware'>Actualizar firmware</a>"
            "<a class='button secondary' href='/diagnostics'>Diagnóstico</a></div>"
            "<p>Utiliza la administración y las actualizaciones en tu red local de confianza.</p></section></main></html>");
  web.send(200, "text/html; charset=utf-8", page);
}

void saveSettings() {
  if (!authenticated()) return;
  if (!currentForm()) return;
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
  lastOperation = "saving_settings";
  if (!persistSettings(*saved)) {
    web.send(500, "text/plain", "No se pudo guardar la configuracion.");
    return;
  }
  baseUrl = candidate;
  reading.available = false;
  history.count = 0;
  ++historyVersion;
  attemptedFetch = false;
  message = "Servidor cambiado";
  lastOperation = "settings_saved";
  String page = F("<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<meta charset='utf-8'><h1>Servidor guardado</h1><p>La configuración se guardó correctamente. "
                  "El Wi-Fi y los datos de administración se conservan.</p><p>Modo HTTPS: ");
  page += device_config::pinnedPublicKey(settings.customCa) ? F("clave pública fija, sin NTP") : F("certificados CA, requiere hora");
  page += F(".</p><p>La consulta se iniciará cuando se cierre esta conexión web, después de unos segundos.</p>"
            "<p><a href='/'>Volver a la página principal</a> · <a href='/diagnostics'>Ver diagnóstico</a></p>");
  web.send(200, "text/html; charset=utf-8", page);
}

__attribute__((noinline)) void parseReading(HTTPClient& http) {
  std::unique_ptr<reading_ui::ParsedReadings> parsed(new (std::nothrow) reading_ui::ParsedReadings);
  if (!parsed) { message = "Sin memoria para historial"; return; }
  const auto result = reading_ui::parseEntries(http.getStream(), static_cast<int64_t>(time(nullptr)), *parsed);
  if (result == reading_ui::ParseResult::Empty) {
    reading.available = false;
    history.count = 0;
    ++historyVersion;
    message = "Servidor sin lecturas";
    return;
  }
  if (result != reading_ui::ParseResult::Ok) {
    message = result == reading_ui::ParseResult::NoMemory ? "Sin memoria para JSON" : "JSON invalido";
    return;
  }
  history = parsed->history;
  ++historyVersion;
  reading.available = true;
  reading.sgv = parsed->value;
  reading.dateMs = parsed->date;
  reading.direction = parsed->direction;
  reading.synthetic = parsed->synthetic;
  message = "Conectado";
}

void fetchReading() {
  String endpoint = baseUrl + "/api/v1/entries/sgv.json?count=145";
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
    lastOperation = "preparing_tls";
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
    // HTTPClient applies its own timeout before connect(), overriding the
    // client's value. Set it here so the TLS handshake actually gets 5 s.
    http.setTimeout(5000);
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
  lastOperation = https ? "tls_request" : "http_request";
  Serial.printf("Consulta inicio: pila=%u\n", ESP.getFreeContStack());
  const int result = http.GET();
  lastOperation = "server_response";
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
  lastOperation = "request_finished";
}

void sendAsset(const char* type, const unsigned char* data, size_t length) {
  if (!setupMode && !authenticated(false)) return;
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("Content-Encoding", "gzip");
  web.send_P(200, type, reinterpret_cast<const char*>(data), length);
}

void homePage() {
  if (setupMode) { configurationPage(); return; }
  if (!authenticated()) return;
  sendAsset("text/html; charset=utf-8", web_assets::index_html, sizeof(web_assets::index_html));
}

void statePage() {
  if (!authenticated(false)) return;
  DynamicJsonDocument document(3072);
  if (!document.capacity()) { web.send(503, "text/plain", "Sin memoria para el visor"); return; }
  const auto now = static_cast<int64_t>(time(nullptr));
  const auto age = reading_ui::ageSeconds(reading.available, reading.dateMs, now);
  const auto style = reading_ui::tone(reading.available, reading.sgv, age,
                                    WiFi.status() == WL_CONNECTED && message == "Conectado");
  const auto trend = reading_ui::trend(reading.direction.c_str());
  document["firmware"] = VERSION;
  document["available"] = reading.available;
  if (reading.available) {
    document["glucose"] = reading.sgv;
    document["reading_date_ms"] = reading.dateMs;
  } else {
    document["glucose"] = nullptr;
    document["reading_date_ms"] = nullptr;
  }
  if (age >= 0) document["age_seconds"] = age;
  else document["age_seconds"] = nullptr;
  document["tone"] = reading_ui::toneName(style);
  document["trend_symbol"] = trend.symbol;
  document["trend_label"] = trend.label;
  document["synthetic"] = reading.available && reading.synthetic;
  document["clock_valid"] = device_clock::plausible(now);
  document["message"] = message;
  document["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  document["rssi_dbm"] = WiFi.RSSI();
  document["ip"] = WiFi.localIP().toString();
  if (attemptedFetch) document["checked_seconds_ago"] = (millis() - lastFetch) / 1000;
  else document["checked_seconds_ago"] = nullptr;
  const auto plot = reading_ui::plot(history, now);
  JsonObject graph = document.createNestedObject("plot");
  graph["low"] = plot.low;
  graph["high"] = plot.high;
  JsonArray points = graph.createNestedArray("points");
  for (size_t i = 0; i < plot.count; ++i) {
    JsonArray point = points.createNestedArray();
    point.add(plot.points[i].x);
    point.add(plot.points[i].y);
    point.add(plot.points[i].start);
  }
  if (document.overflowed()) { web.send(503, "text/plain", "Sin memoria para el visor"); return; }
  String body;
  if (!body.reserve(measureJson(document) + 1)) { web.send(503, "text/plain", "Sin memoria para el visor"); return; }
  serializeJson(document, body);
  web.send(200, "application/json", body);
}

void firmwarePage() {
  if (!authenticated()) return;
  web.send_P(200, PSTR("text/html; charset=utf-8"), PSTR(
      "<!doctype html><html lang='es'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta name='color-scheme' content='dark'><link rel='stylesheet' href='/assets/ui.css'>"
      "<title>Actualizar · Glucosa</title><main class='settings'><a class='back' href='/'>← Volver al panel</a>"
      "<p class='eyebrow'>MANTENIMIENTO</p><h1>Actualizar firmware</h1><p class='intro'>Instala una nueva versión desde tu red local.</p>"
      "<form class='card' method='post' action='/update' enctype='multipart/form-data'><h2>Selecciona el archivo .bin</h2>"
      "<label>Firmware para Wemos D1 mini<input type='file' name='firmware' accept='.bin' required></label>"
      "<p>Los ajustes guardados se conservan. Mantén el Wemos conectado a la alimentación hasta que termine y se reinicie.</p>"
      "<button>Instalar actualización</button></form><p>Utiliza el archivo de Firmware. No se necesita una imagen de FileSystem.</p></main></html>"));
}

void diagnostics() {
  if (!authenticated()) return;
  DynamicJsonDocument document(1024);
  document["firmware"] = VERSION;
  document["boot_id"] = String(bootId, HEX);
  document["reset_reason"] = ESP.getResetReason();
  document["last_operation"] = lastOperation;
  document["free_stack"] = ESP.getFreeContStack();
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

uint16_t readingColor(reading_ui::Tone tone) {
  switch (tone) {
    case reading_ui::Tone::Normal: return 0x7777;
    case reading_ui::Tone::Low: return 0xFC11;
    case reading_ui::Tone::High: case reading_ui::Tone::Old: return 0xFDEE;
    default: return 0x84F4;
  }
}

void drawTrend(uint16_t color) {
  const auto trend = reading_ui::trend(reading.direction.c_str());
  if (!trend.dx && !trend.dy) { display.drawString("-", 188, 88, 4); return; }
  const int dx = trend.dx * (trend.dy ? 16 : 22), dy = trend.dy * (trend.dx ? 16 : 22);
  for (int offset = -1; offset <= 1; ++offset) {
    display.drawLine(188 - dx / 2 + offset, 88 - dy / 2, 188 + dx / 2 + offset, 88 + dy / 2, color);
    display.drawLine(188 + dx / 2, 88 + dy / 2, 188 + dx / 2 - (dx + dy) / 3, 88 + dy / 2 - (dy - dx) / 3, color);
    display.drawLine(188 + dx / 2, 88 + dy / 2, 188 + dx / 2 - (dx - dy) / 3, 88 + dy / 2 - (dy + dx) / 3, color);
  }
  if (trend.doubled) display.drawString("2x", 188, 112, 1);
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
  const int64_t age = reading_ui::ageSeconds(reading.available, reading.dateMs, now);
  const bool stale = clockReady && reading.available && age >= 600;
  const String ageText = !reading.available ? "Sin lecturas" :
                         !clockReady ? "Hora sin sincronizar" :
                         "Hace " + String(static_cast<unsigned long>(age / 60)) + " min";
  String frame = String(reading.available) + reading.sgv + reading.direction + ageText +
                 message + WiFi.localIP().toString() + String(reading.synthetic) +
                 String(historyVersion) + String(static_cast<uint32_t>(now / 60));
  if (frame == lastFrame) return;
  lastFrame = frame;
  const auto tone = reading_ui::tone(reading.available, reading.sgv, age,
                                     WiFi.status() == WL_CONNECTED && message == "Conectado");
  const uint16_t accent = readingColor(tone);
  const uint16_t color = stale ? 0x84F4 : accent;
  constexpr uint16_t background = 0x0882;
  display.fillScreen(background);
  display.drawCircle(120, 120, 115, accent);
  display.drawCircle(120, 120, 114, accent);
  display.setTextDatum(MC_DATUM);
  display.setTextSize(1);
  display.setTextColor(0xCEFB, background);
  display.drawString(reading.available && reading.synthetic ? "DEMO - DATOS FICTICIOS" : "NIGHTSCOUT", 120, 28, 1);
  display.setTextColor(0x84F4, background);
  display.drawString("GLUCOSA", 120, 46, 1);
  display.setTextColor(color, background);
  display.setTextSize(reading.available && reading.sgv >= 1000 ? 2 : 3);
  display.drawString(reading.available ? String(reading.sgv) : "---", 104, 88, 4);
  display.setTextSize(1);
  display.setTextColor(0x84F4, background);
  display.drawString("mg/dL", 104, 124, 2);
  display.setTextColor(color, background);
  if (reading.available) drawTrend(color);
  const auto plot = reading_ui::plot(history, now);
  display.setTextColor(0x740F, background);
  display.drawString("-2 h", 50, 139, 1);
  display.drawString("ahora", 188, 139, 1);
  for (int value : {70, 180}) {
    if (value < plot.low || value > plot.high) continue;
    const int y = 148 + (plot.high - value) * 30 / (plot.high - plot.low);
    for (int x = 38; x < 202; x += 5) display.drawFastHLine(x, y, 2, 0x3207);
  }
  for (size_t i = 0; i < plot.count; ++i) {
    const int x = 38 + plot.points[i].x * 164 / 1000, y = 148 + plot.points[i].y * 30 / 1000;
    if (i && !plot.points[i].start)
      display.drawLine(38 + plot.points[i-1].x * 164 / 1000, 148 + plot.points[i-1].y * 30 / 1000, x, y, color);
    display.fillCircle(x, y, 1, color);
  }
  if (!plot.count) {
    display.setTextColor(0x84F4, background);
    display.drawString(clockReady ? "Esperando historial" : "Hora sin sincronizar", 120, 163, 1);
  }
  display.fillRoundRect(30, 184, 180, 21, 10, stale ? 0x3143 : 0x1944);
  display.setTextColor(stale ? 0xFDEE : 0xBF5A, stale ? 0x3143 : 0x1944);
  display.drawString(stale ? "ANTIGUA: " + ageText : ageText, 120, 195, 2);
  display.setTextColor(0x84F4, background);
  display.drawString(message.length() > 23 ? message.substring(0,21) + ".." : message, 120, 213, 1);
  display.drawString(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() :
                     "Sin Wi-Fi (" + String(lastWifiReason) + ")", 120, 226, 1);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  bootId = ESP.random();
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
  web.on("/api/state", HTTP_GET, statePage);
  web.on("/assets/ui.css", HTTP_GET, [] { sendAsset("text/css; charset=utf-8", web_assets::ui_css, sizeof(web_assets::ui_css)); });
  web.on("/assets/app.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", web_assets::app_js, sizeof(web_assets::app_js)); });
  web.on("/configure", HTTP_GET, configurationPage);
  web.on("/configure", HTTP_POST, saveConfiguration);
  web.on("/settings", HTTP_POST, saveSettings);
  web.on("/settings", HTTP_GET, settingsPage);
  web.on("/firmware", HTTP_GET, firmwarePage);
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
    const bool webIdle = (!webRecentlyUsed || millis() - lastWebActivity >= 3000) &&
                         !web.client().connected();
    if (millis() >= 5000 && webIdle && (!attemptedFetch || millis() - lastFetch >= POLL_MS)) {
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
