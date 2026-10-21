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
#include "http_body.h"
#include "patient_label.h"
#include "view_appearance.h"
#include "startup_view.h"
#include "generated/web_assets.h"

#if __has_include("nightscout_config.h")
#include "nightscout_config.h"
#else
#include "nightscout_config.example.h"
#endif

namespace {
constexpr char VERSION[] = "0.7.0";
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
patient_label::Label patient;
view_appearance::Settings appearance;
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
reading_ui::ParseDiagnostics parseDiagnostic;
int lastHttpStatus = 0;
int responseLength = -1;
int firstBodyByte = -1;
size_t bodyBytes = 0;
uint32_t heapBeforeJson = 0;
const char* bodyError = "none";
const char* transferMode = "unknown";
const char* contentEncoding = "unknown";
const char* historyErrorStage = "none";
bool historyRetryPending = false;
uint32_t historyFailedAt = 0;
bool lastReadingRequestOk = false;
bool latestOnlyRequest = false;

struct Reading {
  bool available = false;
  int sgv = 0;
  int64_t dateMs = 0;
  String direction;
} reading;
reading_ui::History history;
uint32_t historyVersion = 0;
startup_view::Progress startup;
void renderWelcome();

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

bool persistSettings(const device_config::Settings& candidate, const patient_label::Label& label) {
  if (!storageReady || !device_config::validStoredSettings(candidate) || !patient_label::valid(label)) return false;
  EEPROM.put(0, candidate);
  EEPROM.put(patient_label::OFFSET, label);
  if (!EEPROM.commit()) {
    EEPROM.put(0, settings);  // Restore the in-memory buffer after a failed write.
    EEPROM.put(patient_label::OFFSET, patient);
    return false;
  }
  settings = candidate;
  patient = label;
  baseUrl = settings.url;
  if (device_config::pinnedPublicKey(settings.customCa)) sntp_stop();
  return true;
}

bool authenticated(bool interactive = true) {
  // Read-only dashboard polling must not postpone Nightscout indefinitely.
  if (interactive) { webRecentlyUsed = true; lastWebActivity = millis(); }
  web.sendHeader("Cache-Control", "no-store");
  if (!webEnabled) {
    web.send(503, "text/plain; charset=utf-8", F("Completa primero la configuracion inicial."));
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
    web.send(409, "text/plain", F("Conecta primero el Wemos a tu Wi-Fi."));
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
    web.send(400, "text/plain; charset=utf-8", F("Hora no válida. Comprueba la fecha y hora del teléfono/PC."));
    return;
  }
  struct timeval value{};
  value.tv_sec = static_cast<time_t>(epoch);
  if (settimeofday(&value, nullptr) != 0) {
    web.send(500, "text/plain", F("No se pudo ajustar el reloj."));
    return;
  }
  clockSource = ClockSource::Browser;
  attemptedFetch = false;
  lastFrame = "";
  message = F("Hora del navegador");
  web.send(200, "text/html; charset=utf-8",
           F("<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<h1>Hora ajustada</h1><p>El reloj del Wemos recibió la hora del navegador. "
           "La consulta al servidor se reanudará en unos segundos.</p><p><a href='/'>Volver</a></p>"));
}

void appendServerFields(String& page) {
  page += F("<label>Nombre de la persona (opcional)"
            "<input name='patient_name' maxlength='24' autocomplete='off' value='");
  page += htmlEscape(patient.name);
  page += F("'></label><p>Hasta 24 caracteres. Aparece arriba en la pantalla y en el visor web. "
            "Vacío muestra NIGHTSCOUT. La pantalla adapta las tildes a su fuente.</p>"
            "<label>URL base de Nightscout"
            "<input name='url' type='url' maxlength='200' required value='");
  page += htmlEscape(baseUrl);
  page += F("'></label><p>Solo la URL, sin /api/v1 ni API secret.</p>"
            "<label>Token Nightscout de solo lectura (opcional)"
            "<input name='token' type='password' maxlength='256' autocomplete='off'></label>");
  page += settings.token[0] ? F("<p>Hay un token guardado; dejar vacío lo conserva para el mismo servidor.</p>")
                            : F("<p>Sin token guardado. Solo hace falta si tu Nightscout exige autenticación.</p>");
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

bool patientFields(patient_label::Label& label) {
  label = patient;
  if (!web.hasArg("patient_name")) return true;
  String name = web.arg("patient_name");
  name.trim();
  if (!patient_label::validName(name.c_str())) {
    web.send(400, "text/plain; charset=utf-8", F("Nombre inválido: usa hasta 24 caracteres, sin saltos de línea ni controles."));
    return false;
  }
  memset(label.name, 0, sizeof(label.name));
  name.toCharArray(label.name, sizeof(label.name));
  return true;
}

String colorHex(uint32_t color) {
  char value[8];
  snprintf(value, sizeof(value), "#%06lx", static_cast<unsigned long>(color));
  return String(value);
}

void appearanceOption(String& page, unsigned value, unsigned selected, const __FlashStringHelper* caption) {
  page += F("<option value='"); page += String(value);
  page += value == selected ? F("' selected>") : F("'>");
  page += caption; page += F("</option>");
}

void appearanceColor(String& page, const __FlashStringHelper* name, const __FlashStringHelper* caption, uint32_t color) {
  page += F("<label>"); page += caption;
  page += F("<input type='color' name='"); page += name;
  page += F("' value='"); page += colorHex(color); page += F("'></label>");
}

void appendAppearanceControls(String& page) {
  page += F("<form id='appearance-form' class='card' method='post' action='/appearance'>"
            "<h2>Apariencia</h2><input type='hidden' name='csrf' value='");
  page += csrf;
  page += F("'><label>Modo del visor<select name='mode'>");
  appearanceOption(page, 0, static_cast<unsigned>(appearance.mode), F("Con gráfica · 30 minutos"));
  appearanceOption(page, 1, static_cast<unsigned>(appearance.mode), F("Número grande · flecha lateral"));
  appearanceOption(page, 2, static_cast<unsigned>(appearance.mode), F("Número centrado · flecha debajo"));
  page += F("</select></label><label>Tema de colores<select name='theme'>");
  appearanceOption(page, 0, static_cast<unsigned>(appearance.theme), F("Menta"));
  appearanceOption(page, 1, static_cast<unsigned>(appearance.theme), F("Rojo"));
  appearanceOption(page, 2, static_cast<unsigned>(appearance.theme), F("Azul"));
  appearanceOption(page, 3, static_cast<unsigned>(appearance.theme), F("Rosado"));
  appearanceOption(page, 4, static_cast<unsigned>(appearance.theme), F("Celeste"));
  appearanceOption(page, 5, static_cast<unsigned>(appearance.theme), F("Personalizado"));
  page += F("</select></label><div class='color-fields'>");
  appearanceColor(page, F("accent"), F("Acento y flecha"), appearance.accent);
  appearanceColor(page, F("background"), F("Fondo del visor"), appearance.background);
  appearanceColor(page, F("number"), F("Número"), appearance.number);
  page += F("</div><p>Los colores elegidos se usan en Personalizado. Los avisos de lectura baja, alta o antigua mantienen texto y colores propios.</p>"
            "<div id='appearance-preview' class='appearance-preview' aria-label='Vista previa de colores'>"
            "<span>Vista previa · datos de ejemplo</span><strong>142 <svg viewBox='0 0 40 40' aria-hidden='true'><path d='M7 33 33 7M10 7H33V30'/></svg></strong><small>mg/dL</small></div>"
            "<p id='contrast-feedback' role='status'></p><button id='save-appearance'>Guardar apariencia</button> "
            "<button id='reset-theme' class='button secondary' type='button'>Volver a menta</button>"
            "<p>Se aplica a la pantalla y al visor web. El nombre se cambia en el formulario siguiente.</p></form>");
}

void saveAppearance() {
  if (!authenticated()) return;
  if (!currentForm()) return;
  const String mode = web.arg("mode"), theme = web.arg("theme");
  const String accent = web.arg("accent"), background = web.arg("background"), number = web.arg("number");
  view_appearance::Settings candidate;
  if (!view_appearance::update(appearance, web.hasArg("mode") ? mode.c_str() : nullptr,
      web.hasArg("theme") ? theme.c_str() : nullptr, web.hasArg("accent") ? accent.c_str() : nullptr,
      web.hasArg("background") ? background.c_str() : nullptr, web.hasArg("number") ? number.c_str() : nullptr, candidate)) {
    web.send(400, "text/plain; charset=utf-8", F("Revisa el modo y los colores. Elige un fondo oscuro y colores con más contraste para mantener legibles la lectura y los avisos."));
    return;
  }
  if (!storageReady) { web.send(503, "text/plain", F("No se puede guardar la apariencia.")); return; }
  const bool changed = candidate.mode != appearance.mode || candidate.theme != appearance.theme ||
                       candidate.accent != appearance.accent || candidate.background != appearance.background || candidate.number != appearance.number;
  if (changed) {
    EEPROM.put(view_appearance::OFFSET, candidate);
    if (!EEPROM.commit()) {
      EEPROM.put(view_appearance::OFFSET, appearance);
      web.send(500, "text/plain", F("No se pudo guardar la apariencia.")); return;
    }
    appearance = candidate;
    lastFrame = "";
  }
  if (web.arg("quick") == "1") web.send(200, "application/json", F("{\"ok\":true}"));
  else { web.sendHeader("Location", "/settings?appearance=saved#appearance-form"); web.send(303); }
}

void configurationPage() {
  if (!setupMode && !authenticated()) return;
  web.sendHeader("Cache-Control", "no-store");
  String page = FPSTR(web_assets::setup_html_0);
  if (!setupMode) page += F("<a class='back' href='/'>← Volver al panel</a>");
  page += FPSTR(web_assets::setup_html_1);
  page += setupMode ? F("PRIMER INICIO") : F("CAMBIAR CONEXIÓN");
  page += FPSTR(web_assets::setup_html_2);
  page += htmlEscape(settings.ssid);
  page += FPSTR(web_assets::setup_html_3);
  appendServerFields(page);
  page += FPSTR(web_assets::setup_html_4);
  page += htmlEscape(settings.webUsername);
  page += FPSTR(web_assets::setup_html_5);
  page += csrf;
  page += FPSTR(web_assets::setup_html_6);
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
    web.send(400, "text/plain; charset=utf-8", F("Revisa la URL y la longitud de los campos."));
    return;
  }
  // CA settings are several KB; allocate candidates on the heap, not the ESP's 4 KB stack.
  std::unique_ptr<device_config::Settings> candidate(new (std::nothrow) device_config::Settings);
  if (!candidate) { web.send(503, "text/plain", F("Memoria insuficiente. Intenta nuevamente.")); return; }
  if (!serverFields(*candidate, url)) {
    web.send(400, "text/plain; charset=utf-8", F("Revisa el token y el PEM: una CA válida o una clave pública RSA/EC. Nunca una clave privada. El token requiere HTTPS. No combines borrar con introducir un valor nuevo."));
    return;
  }
  ssid.toCharArray(candidate->ssid, sizeof(candidate->ssid));
  wifiPassword.toCharArray(candidate->wifiPassword, sizeof(candidate->wifiPassword));
  username.toCharArray(candidate->webUsername, sizeof(candidate->webUsername));
  password.toCharArray(candidate->webPassword, sizeof(candidate->webPassword));
  if (!device_config::configured(*candidate)) {
    web.send(400, "text/plain; charset=utf-8", F("SSID y usuario obligatorios; clave Wi-Fi de 8 a 63 caracteres (o vacia para red abierta); clave de administracion de 12 a 64."));
    return;
  }
  lastOperation = "saving_configuration";
  patient_label::Label label;
  if (!patientFields(label)) return;
  if (!persistSettings(*candidate, label)) {
    web.send(500, "text/plain", F("No se pudo guardar. Intenta nuevamente."));
    return;
  }
  web.send(200, "text/html; charset=utf-8",
           F("<!doctype html><html lang='es'><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<meta name='color-scheme' content='dark'><link rel='stylesheet' href='/assets/ui.css'>"
           "<title>Configuración guardada</title><main class='settings setup-page'><section class='card'>"
           "<p class='eyebrow'>CONFIGURACIÓN GUARDADA</p><h1>Todo listo</h1>"
           "<p>El Wemos se reiniciará. Vuelve a conectar tu teléfono "
           "a tu Wi-Fi y abre la IP que aparecerá en la pantalla del Wemos.</p></section></main></html>"));
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
    message = F("Error al crear Wi-Fi");
    return;
  }
  setupMode = true;
  message = F("Configuracion inicial");
  lastFrame = "";
}

void settingsPage() {
  if (!authenticated()) return;
  String page = F("<!doctype html><html lang='es'><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<meta name='color-scheme' content='dark'><link rel='stylesheet' href='/assets/ui.css'>"
                  "<script src='/assets/appearance.js' defer></script>"
                  "<title>Ajustes · Glucosa</title><main class='settings'><a class='back' href='/'>← Volver al panel</a>"
                  "<p class='eyebrow'>TU DISPOSITIVO</p><h1>Ajustes</h1>"
                  "<p class='intro'>Conexión, servidor y mantenimiento de tu pantalla.</p><section class='card'>");
  page += "<p>Firmware " + String(VERSION) + " · IP " + WiFi.localIP().toString() + "</p>";
  page += "<p>Sesión " + String(bootId, HEX) + " · encendido hace " + String(millis() / 1000) + " s</p>";
  page += "<p>Estado: " + htmlEscape(message) + "</p>";
  page += "<p>Wi-Fi: " + String(WiFi.RSSI()) + " dBm · desconexiones: " + String(wifiDisconnects) +
          " · último motivo: " + String(lastWifiReason) + "</p>";
  page += F("</section>");
  if (web.arg("appearance") == "saved") page += F("<p class='notice' role='status'>Apariencia guardada.</p>");
  appendAppearanceControls(page);
  page += F("<form class='card' method='post' action='/settings'><h2>Persona y servidor de lecturas</h2>");
  appendServerFields(page);
  page += "<input type='hidden' name='csrf' value='" + csrf + "'>";
  page += F("<button>Guardar servidor</button>"
            "<p>Introduce solo la dirección de tu servidor Nightscout, sin rutas API.</p></form><section class='card'>");
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
    web.send(400, "text/plain", F("URL HTTP(S) invalida. No incluyas credenciales, query ni /api/v1."));
    return;
  }
  // Changing only the URL must preserve the Wi-Fi and administrator credentials.
  std::unique_ptr<device_config::Settings> saved(new (std::nothrow) device_config::Settings);
  if (!saved) { web.send(503, "text/plain", F("Memoria insuficiente.")); return; }
  if (!serverFields(*saved, candidate)) {
    web.send(400, "text/plain; charset=utf-8", F("Revisa el token, HTTPS y el PEM de CA o clave pública."));
    return;
  }
  lastOperation = "saving_settings";
  patient_label::Label label;
  if (!patientFields(label)) return;
  if (!persistSettings(*saved, label)) {
    web.send(500, "text/plain", F("No se pudo guardar la configuracion."));
    return;
  }
  baseUrl = candidate;
  reading.available = false;
  history.count = 0;
  ++historyVersion;
  historyRetryPending = false;
  historyErrorStage = "none";
  lastReadingRequestOk = false;
  attemptedFetch = false;
  message = F("Servidor cambiado");
  lastOperation = "settings_saved";
  String page = F("<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<meta charset='utf-8'><h1>Servidor guardado</h1><p>La configuración se guardó correctamente. "
                  "El Wi-Fi y los datos de administración se conservan.</p><p>Modo HTTPS: ");
  page += device_config::pinnedPublicKey(settings.customCa) ? F("clave pública fija, sin NTP") : F("certificados CA, requiere hora");
  page += F(".</p><p>La consulta se iniciará cuando se cierre esta conexión web, después de unos segundos.</p>"
            "<p><a href='/'>Volver a la página principal</a> · <a href='/diagnostics'>Ver diagnóstico</a></p>");
  web.send(200, "text/html; charset=utf-8", page);
}

struct HttpInput {
  WiFiClient& client;
  size_t available() {
    const int count = client.available();
    return count > 0 ? static_cast<size_t>(count) : 0;
  }
  size_t readBytes(char* buffer, size_t length) {
    if (!client.available() && !client.connected()) return 0;
    return client.readBytes(buffer, length);
  }
};

__attribute__((noinline)) bool parseReading(HTTPClient& http, bool latestOnly, bool chunked) {
  lastOperation = "parsing_body";
  heapBeforeJson = ESP.getFreeHeap();
  parseDiagnostic = reading_ui::ParseDiagnostics{};
  bodyBytes = 0;
  firstBodyByte = -1;
  bodyError = "none";
  // Leave headroom for the parser and Wi-Fi's receive buffers together.
  // A failed allocation inside a networking dependency can otherwise reset
  // the device instead of producing a recoverable JSON error.
  if (heapBeforeJson < 3072) {
    parseDiagnostic.stage = "low_heap";
    message = F("Memoria baja para lectura");
    return false;
  }
  std::unique_ptr<reading_ui::ParsedReadings> parsed(new (std::nothrow) reading_ui::ParsedReadings);
  HttpInput input{http.getStream()};
  using Reader = nightscout_http::BodyReader<HttpInput>;
  std::unique_ptr<Reader> body(new (std::nothrow) Reader(input, chunked, http.getSize()));
  if (!parsed || !body) {
    parseDiagnostic.stage = "allocation";
    message = F("Sin memoria para historial");
    return false;
  }
  const auto result = reading_ui::parseEntries(*body, static_cast<int64_t>(time(nullptr)), *parsed, parseDiagnostic);
  const bool complete = (result == reading_ui::ParseResult::Ok || result == reading_ui::ParseResult::Empty)
                            ? body->finish() : false;
  bodyBytes = body->bytes();
  firstBodyByte = body->firstByte();
  bodyError = nightscout_http::errorName(body->error());
  if (!complete || (result != reading_ui::ParseResult::Ok && result != reading_ui::ParseResult::Empty)) {
    if (result == reading_ui::ParseResult::NoMemory) message = F("Sin memoria para JSON");
    else if (body->error() == nightscout_http::BodyError::Truncated ||
             !strcmp(parseDiagnostic.jsonError, "IncompleteInput") ||
             !strcmp(parseDiagnostic.jsonError, "EmptyInput")) message = F("Respuesta incompleta");
    else if (!strcmp(parseDiagnostic.stage, "invalid_reading")) message = F("Lectura no valida");
    else if (body->error() != nightscout_http::BodyError::None) message = F("Error en cuerpo HTTP");
    else message = "JSON: " + String(parseDiagnostic.stage);
    Serial.printf_P(PSTR("Lectura rechazada: fase=%s json=%s cuerpo=%s bytes=%u entradas=%u primero=%d\n"),
                  parseDiagnostic.stage, parseDiagnostic.jsonError, bodyError,
                  static_cast<unsigned>(bodyBytes), static_cast<unsigned>(parseDiagnostic.entries), firstBodyByte);
    return false;
  }
  if (result == reading_ui::ParseResult::Empty) {
    reading.available = false;
    history.count = 0;
    ++historyVersion;
    message = F("Servidor sin lecturas");
    return true;
  }
  if (latestOnly) history.add(parsed->date, static_cast<uint16_t>(parsed->value));
  else history = parsed->history;
  ++historyVersion;
  reading.available = true;
  reading.sgv = parsed->value;
  reading.dateMs = parsed->date;
  reading.direction = parsed->direction;
  lastReadingRequestOk = true;
  message = latestOnly ? "Lectura; historial parcial" : "Conectado";
  return true;
}

void fetchReading() {
  lastReadingRequestOk = false;
  parseDiagnostic = reading_ui::ParseDiagnostics{};
  bodyBytes = 0;
  firstBodyByte = -1;
  bodyError = "none";
  heapBeforeJson = 0;
  lastHttpStatus = 0;
  responseLength = -1;
  transferMode = "unknown";
  contentEncoding = "unknown";
  latestOnlyRequest = historyRetryPending && millis() - historyFailedAt < 300000;
  String endpoint = baseUrl;
  endpoint += latestOnlyRequest ? F("/api/v1/entries.json?count=1") : F("/api/v1/entries/sgv.json?count=");
  if (!latestOnlyRequest) endpoint += String(reading_ui::SOURCE_READINGS);
  const bool https = baseUrl.startsWith("https://");
  const bool pinned = device_config::pinnedPublicKey(settings.customCa);
  // Avoid allocating the TLS stack and buffers until a connection is possible.
  if (https && !pinned && !device_clock::plausible(time(nullptr))) {
    message = ntpAttempts < 2 ? "Esperando hora NTP" : "Sin hora: abre la web";
    return;
  }
  if (settings.token[0]) {
    if (!https) {
      message = F("Token requiere HTTPS");
      return;
    }
    endpoint += "&token=" + urlEncode(settings.token);
  }
  startup.beginRequest();
  if (!startup.finished) { message = F("Consultando servidor"); renderWelcome(); }
  // Destruction order matters: HTTPClient -> TLS client -> pinned key / trust anchors.
  WiFiClient plain;
  std::unique_ptr<BearSSL::X509List> trust;
  std::unique_ptr<BearSSL::PublicKey> key;
  std::unique_ptr<BearSSL::WiFiClientSecure> secure;
  HTTPClient http;
  http.useHTTP10(true);  // Still decode chunked responses from HTTP/1.1 proxies.
  http.setTimeout(2500);
  http.setReuse(false);
  if (https) {
    lastOperation = "preparing_tls";
    heapBeforeTls = ESP.getFreeHeap();
    largestBlockBeforeTls = ESP.getMaxFreeBlockSize();
    secure.reset(new (std::nothrow) BearSSL::WiFiClientSecure);
    if (!secure) { message = F("Sin memoria para TLS"); return; }
    if (pinned) {
      key.reset(new (std::nothrow) BearSSL::PublicKey(settings.customCa));
      if (!key || !validPinnedKey(*key)) { message = F("Clave publica invalida"); return; }
      secure->setKnownKey(key.get());
    } else if (settings.customCa[0]) {
      trust.reset(new (std::nothrow) BearSSL::X509List(settings.customCa));
      if (!trust || trust->getCount() != 1) { message = F("CA propia invalida"); return; }
      secure->setTrustAnchors(trust.get());
    } else secure->setCertStore(&publicRoots);
    // HTTPClient applies its own timeout before connect(), overriding the
    // client's value. Set it here so the TLS handshake actually gets 5 s.
    http.setTimeout(5000);
    Serial.printf_P(PSTR("TLS inicio: modo=%s heap=%u bloque=%u RSSI=%d\n"),
                  pinned ? "clave" : "CA", heapBeforeTls, largestBlockBeforeTls, WiFi.RSSI());
    if (!http.begin(*secure, endpoint)) {
      message = F("URL HTTPS invalida");
      return;
    }
  } else if (!http.begin(plain, endpoint)) {
    message = F("URL HTTP invalida");
    return;
  }
  const char* responseHeaders[] = {"Date", "Age", "Transfer-Encoding", "Content-Encoding"};
  http.collectHeaders(responseHeaders, 4);
  http.addHeader("Cache-Control", "no-cache, no-store");
  http.addHeader("Accept-Encoding", "identity");
  lastOperation = https ? "tls_request" : "http_request";
  Serial.printf_P(PSTR("Consulta inicio: pila=%u\n"), ESP.getFreeContStack());
  const int result = http.GET();
  lastHttpStatus = result;
  startup.response(result);
  responseLength = http.getSize();
  String transfer = http.header("Transfer-Encoding");
  transfer.trim(); transfer.toLowerCase();
  String encoding = http.header("Content-Encoding");
  encoding.trim(); encoding.toLowerCase();
  const bool chunked = transfer == "chunked";
  transferMode = chunked ? "chunked" : !transfer.length() || transfer == "identity" ? "identity" : "unsupported";
  contentEncoding = !encoding.length() || encoding == "identity" ? "identity" : "unsupported";
  lastOperation = "server_response";
  lastTlsError = secure ? secure->getLastSSLError() : 0;
  Serial.printf_P(PSTR("Consulta: HTTP=%d TLS=%d WiFi=%d heap=%u\n"), result, lastTlsError,
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
    if (!startup.finished) { message = F("Esperando lectura"); renderWelcome(); }
    bool parsedOk = false;
    if (!strcmp(transferMode, "unsupported") || !strcmp(contentEncoding, "unsupported")) {
      parseDiagnostic.stage = "unsupported_encoding";
      message = F("Codificacion HTTP no admitida");
    } else parsedOk = parseReading(http, latestOnlyRequest, chunked);
    startup.parsed(parsedOk, reading.available);
    if (parsedOk) {
      if (!latestOnlyRequest) { historyRetryPending = false; historyErrorStage = "none"; }
    } else if (!latestOnlyRequest) {
      // Keep attempting the last-known working, single-reading API every 30 s.
      // Retry the full graph after five minutes; never accept partial JSON.
      historyRetryPending = true;
      historyFailedAt = millis();
      historyErrorStage = strcmp(bodyError, "none") ? bodyError :
                          strcmp(parseDiagnostic.jsonError, "Ok") ? parseDiagnostic.jsonError : parseDiagnostic.stage;
    }
  } else {
    // Never print the endpoint or token to Serial or the web UI.
    if (result == 401 || result == 403) message = "Revisa token (" + String(result) + ")";
    else if (result < 0 && lastTlsError != 0) message = "Error TLS " + String(lastTlsError);
    else message = result > 0 ? "Error HTTP " + String(result) : "Error de conexion";
  }
  // Include TLS receive errors encountered while parsing the body, not just
  // errors already present when the response headers arrived.
  if (secure) lastTlsError = secure->getLastSSLError();
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
  static_assert(JSON_OBJECT_SIZE(24) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(3) +
                JSON_ARRAY_SIZE(reading_ui::HISTORY_SIZE) + reading_ui::HISTORY_SIZE * JSON_ARRAY_SIZE(3) + 768 <= 2048,
                "State JSON needs room for appearance, patient name and all history points");
  DynamicJsonDocument document(2048);
  if (!document.capacity()) { web.send(503, "text/plain", F("Sin memoria para el visor")); return; }
  const auto now = static_cast<int64_t>(time(nullptr));
  const auto age = reading_ui::ageSeconds(reading.available, reading.dateMs, now);
  const auto style = reading_ui::tone(reading.available, reading.sgv, age,
                                    WiFi.status() == WL_CONNECTED && lastReadingRequestOk);
  const auto trend = reading_ui::trend(reading.direction.c_str());
  document[F("firmware")] = VERSION;
  document[F("patient_name")] = patient.name;
  document[F("csrf")] = csrf;
  const auto colors = view_appearance::palette(appearance);
  JsonObject view = document.createNestedObject(F("appearance"));
  view[F("mode")] = static_cast<unsigned>(appearance.mode);
  view[F("theme")] = static_cast<unsigned>(appearance.theme);
  view[F("accent")] = colorHex(colors.accent);
  view[F("background")] = colorHex(colors.background);
  view[F("number")] = colorHex(colors.number);
  document[F("available")] = reading.available;
  if (reading.available) {
    document[F("glucose")] = reading.sgv;
    document[F("reading_date_ms")] = reading.dateMs;
  } else {
    document[F("glucose")] = nullptr;
    document[F("reading_date_ms")] = nullptr;
  }
  if (age >= 0) document[F("age_seconds")] = age;
  else document[F("age_seconds")] = nullptr;
  document[F("tone")] = reading_ui::toneName(style);
  document[F("trend_symbol")] = trend.symbol;
  document[F("trend_label")] = trend.label;
  document[F("clock_valid")] = device_clock::plausible(now);
  document[F("message")] = message;
  document[F("wifi_connected")] = WiFi.status() == WL_CONNECTED;
  document[F("rssi_dbm")] = WiFi.RSSI();
  document[F("ip")] = WiFi.localIP().toString();
  if (attemptedFetch) document[F("checked_seconds_ago")] = (millis() - lastFetch) / 1000;
  else document[F("checked_seconds_ago")] = nullptr;
  const auto plot = reading_ui::plot(history, now);
  JsonObject graph = document.createNestedObject(F("plot"));
  graph[F("low")] = plot.low;
  graph[F("high")] = plot.high;
  JsonArray points = graph.createNestedArray(F("points"));
  for (size_t i = 0; i < plot.count; ++i) {
    JsonArray point = points.createNestedArray();
    point.add(plot.points[i].x);
    point.add(plot.points[i].y);
    point.add(plot.points[i].start);
  }
  if (document.overflowed()) { web.send(503, "text/plain", F("Sin memoria para el visor")); return; }
  String body;
  if (!body.reserve(measureJson(document) + 1)) { web.send(503, "text/plain", F("Sin memoria para el visor")); return; }
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
  DynamicJsonDocument document(3072);
  if (!document.capacity()) { web.send(503, "text/plain", F("Sin memoria para diagnostico")); return; }
  document[F("firmware")] = VERSION;
  document[F("boot_id")] = String(bootId, HEX);
  document[F("reset_reason")] = ESP.getResetReason();
  String resetInfo = ESP.getResetInfo();
  if (resetInfo.length() > 512) resetInfo.remove(512);
  document[F("reset_info")] = resetInfo;
  document[F("last_operation")] = lastOperation;
  document[F("free_stack")] = ESP.getFreeContStack();
  document[F("uptime_seconds")] = millis() / 1000;
  document[F("wifi_connected")] = WiFi.status() == WL_CONNECTED;
  document[F("wifi_status")] = static_cast<int>(WiFi.status());
  document[F("rssi_dbm")] = WiFi.RSSI();
  document[F("wifi_disconnects")] = wifiDisconnects;
  document[F("last_wifi_reason")] = lastWifiReason;
  document[F("free_heap")] = ESP.getFreeHeap();
  document[F("largest_free_block")] = ESP.getMaxFreeBlockSize();
  document[F("heap_before_tls")] = heapBeforeTls;
  document[F("largest_block_before_tls")] = largestBlockBeforeTls;
  document[F("last_tls_error")] = lastTlsError;
  document[F("http_status")] = lastHttpStatus;
  document[F("response_length")] = responseLength;
  document[F("transfer_encoding")] = transferMode;
  document[F("content_encoding")] = contentEncoding;
  document[F("body_bytes")] = bodyBytes;
  document[F("first_body_byte")] = firstBodyByte;
  document[F("body_error")] = bodyError;
  document[F("json_stage")] = parseDiagnostic.stage;
  document[F("json_error")] = parseDiagnostic.jsonError;
  document[F("json_entries")] = parseDiagnostic.entries;
  document[F("heap_before_json")] = heapBeforeJson;
  document[F("latest_only_request")] = latestOnlyRequest;
  document[F("history_retry_pending")] = historyRetryPending;
  document[F("history_error_stage")] = historyErrorStage;
  document[F("clock_valid")] = device_clock::plausible(time(nullptr));
  document[F("clock_source")] = clockSource == ClockSource::Server ? "https_server" :
                             clockSource == ClockSource::Browser ? "browser" :
                             clockSource == ClockSource::Ntp ? "ntp" : "unknown";
  document[F("ntp_attempts")] = ntpAttempts;
  document[F("tls_authentication")] = device_config::pinnedPublicKey(settings.customCa) ? "pinned_public_key" : "certificate_authority";
  if (document.overflowed()) { web.send(503, "text/plain", F("Sin memoria para diagnostico")); return; }
  String body;
  serializeJson(document, body);
  web.send(200, "application/json", body);
}

uint16_t readingColor(reading_ui::Tone tone, const view_appearance::Palette& colors) {
  switch (tone) {
    case reading_ui::Tone::Normal: return view_appearance::rgb565(colors.number);
    case reading_ui::Tone::Low: return view_appearance::rgb565(0xFF828B);
    case reading_ui::Tone::High: case reading_ui::Tone::Old: return view_appearance::rgb565(0xFFBD73);
    default: return view_appearance::rgb565(0x879DA5);
  }
}

void drawTrend(uint16_t color, int x, int y, int size) {
  const auto trend = reading_ui::trend(reading.direction.c_str());
  if (!trend.dx && !trend.dy) { display.drawFastHLine(x - 6, y, 12, color); return; }
  const int dx = trend.dx * (trend.dy ? size * 7 / 10 : size);
  const int dy = trend.dy * (trend.dx ? size * 7 / 10 : size);
  const int arrows = trend.doubled ? 2 : 1;
  for (int arrow = 0; arrow < arrows; ++arrow) {
    const int center = x + (arrows == 2 ? (arrow ? 5 : -5) : 0);
    for (int offset = -1; offset <= 1; ++offset) {
      display.drawLine(center - dx / 2 + offset, y - dy / 2, center + dx / 2 + offset, y + dy / 2, color);
      display.drawLine(center + dx / 2 + offset, y + dy / 2, center + dx / 2 - (dx + dy) / 3 + offset, y + dy / 2 - (dy - dx) / 3, color);
      display.drawLine(center + dx / 2 + offset, y + dy / 2, center + dx / 2 - (dx - dy) / 3 + offset, y + dy / 2 - (dy + dx) / 3, color);
    }
  }
}

void drawPatientName(uint16_t background, int y = 43, int width = 120) {
  char label[25];
  patient_label::displayName(patient, label);
  if (!label[0]) strlcpy(label, "NIGHTSCOUT", sizeof(label));
  const bool named = patient.name[0] != '\0';
  display.setTextSize(named ? 2 : 1);
  const int maximumWidth = named ? width : 144;
  size_t length = strlen(label);
  while (length > 3 && display.textWidth(label, 2) > maximumWidth) {
    --length;
    memcpy(label + length - 3, "...", 4);
  }
  display.setTextColor(view_appearance::rgb565(0xD3E0DD), background);
  display.drawString(label, 120, y, 2);
  display.setTextSize(1);
}

void drawValue(const String& value, int x, int y, int width, int height) {
  display.setTextSize(1);
  uint8_t scale = static_cast<uint8_t>(height / display.fontHeight(4));
  if (scale > 4) scale = 4;
  if (!scale) scale = 1;
  display.setTextSize(scale);
  while (scale > 1 && display.textWidth(value, 4) > width) display.setTextSize(--scale);
  display.drawString(value, x, y, 4);
  display.setTextSize(1);
}

void drawHistory(time_t now, uint16_t color, uint16_t background) {
  const auto plot = reading_ui::plot(history, now);
  display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
  display.drawString(F("-30 min"), 50, 139, 1);
  display.drawString(F("ahora"), 188, 139, 1);
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
  if (!plot.count) display.drawString(device_clock::plausible(now) ? F("Esperando historial") : F("Hora sin sincronizar"), 120, 163, 1);
}

void drawStartupStage(startup_view::Stage stage, int y) {
  const auto colors = view_appearance::palette(appearance);
  const uint16_t background = view_appearance::rgb565(colors.background);
  const uint16_t accent = view_appearance::rgb565(colors.accent);
  const uint16_t color = stage == startup_view::Stage::Error ? view_appearance::rgb565(0xFFBD73) :
                         stage == startup_view::Stage::Pending ? view_appearance::rgb565(0x52666D) : accent;
  if (stage == startup_view::Stage::Done) {
    display.fillCircle(53, y, 5, accent);
    display.drawLine(50, y, 52, y + 2, background);
    display.drawLine(52, y + 2, 56, y - 2, background);
  } else {
    display.drawCircle(53, y, 5, color);
    if (stage == startup_view::Stage::Error) {
      display.drawLine(53, y - 2, 53, y, color);
      display.drawPixel(53, y + 2, color);
    }
  }
  display.setTextColor(stage == startup_view::Stage::Done ? view_appearance::rgb565(0xD3E0DD) : color, background);
}

// Used before TLS allocation and between response headers and parsing; no framebuffer.
void renderWelcome() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const String frame = String(F("boot:")) + String(connected) + String(startup.responseCode) +
                       String(startup.parseFailed) + message;
  if (lastFrame == frame) return;
  lastFrame = frame;
  const auto colors = view_appearance::palette(appearance);
  const uint16_t background = view_appearance::rgb565(colors.background);
  const uint16_t accent = view_appearance::rgb565(colors.accent);
  display.fillScreen(background);
  display.drawCircle(120, 120, 115, accent);
  display.setTextDatum(MC_DATUM);
  display.setTextSize(1);
  display.drawLine(106, 47, 112, 47, accent);
  display.drawLine(112, 47, 117, 37, accent);
  display.drawLine(117, 37, 123, 55, accent);
  display.drawLine(123, 55, 128, 47, accent);
  display.drawLine(128, 47, 134, 47, accent);
  drawPatientName(background, 82, 170);
  display.setTextDatum(ML_DATUM);
  drawStartupStage(startup.wifi(connected), 121);
  display.drawString(connected ? F("Wi-Fi conectado") : F("Conectando Wi-Fi"), 67, 121, 2);
  drawStartupStage(startup.server(connected), 147);
  const bool waitingClock = baseUrl.startsWith("https://") && !device_config::pinnedPublicKey(settings.customCa) &&
                            !device_clock::plausible(time(nullptr));
  display.drawString(!connected ? F("Esperando Wi-Fi") :
                     startup.responseCode == 200 ? F("Servidor conectado") :
                     startup.responseCode ? F("Error de servidor") :
                     waitingClock ? F("Esperando hora") :
                     !attemptedFetch ? F("Esperando consulta") : F("Consultando servidor"), 67, 147, 2);
  drawStartupStage(startup.reading(connected), 173);
  display.drawString(startup.parseFailed ? F("Reintentando lectura") : F("Esperando lectura"), 67, 173, 2);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
  // Preserve specific clock, authentication and parse errors on the welcome screen.
  if (!connected || message == "Sin Wi-Fi" || message == "Conectando Wi-Fi") display.drawString(F("Preparando tu pantalla"), 120, 207, 1);
  else display.drawString(message.length() > 23 ? message.substring(0, 21) + F("..") : message, 120, 207, 1);
  if (connected) display.drawString(WiFi.localIP().toString(), 120, 220, 1);
}

void renderProvisioning() {
  if (lastFrame == "setup") return;
  lastFrame = "setup";
  const auto colors = view_appearance::palette(appearance);
  const uint16_t background = view_appearance::rgb565(colors.background);
  const uint16_t accent = view_appearance::rgb565(colors.accent);
  display.fillScreen(background);
  display.drawCircle(120, 120, 115, accent);
  display.setTextDatum(MC_DATUM);
  display.setTextSize(1);
  display.setTextColor(accent, background);
  display.drawString(F("CONFIGURAR"), 120, 30, 2);
  for (int i = 0; i < 3; ++i) {
    const int y = 72 + i * 50;
    display.fillCircle(42, y, 8, accent);
    display.setTextColor(background, accent);
    display.drawString(String(i + 1), 42, y, 1);
  }
  display.setTextDatum(ML_DATUM);
  display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
  display.drawString(F("Conecta a esta Wi-Fi"), 60, 63, 1);
  display.drawString(F("Usa esta clave"), 60, 113, 1);
  display.drawString(F("Abre en el navegador"), 60, 163, 1);
  display.setTextColor(view_appearance::rgb565(0xD3E0DD), background);
  display.drawString(setupSsid, 60, 80, 2);
  display.drawString(setupPassword, 60, 130, 2);
  display.drawString(WiFi.softAPIP().toString(), 60, 180, 2);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
  display.drawString(F("Continua en tu telefono"), 120, 208, 1);
}

void render() {
  if (setupMode) { renderProvisioning(); return; }
  if (!startup.finished) { renderWelcome(); return; }
  const time_t now = time(nullptr);
  const int64_t age = reading_ui::ageSeconds(reading.available, reading.dateMs, now);
  const bool stale = reading.available && age >= 600;
  const String ageText = !reading.available ? F("Sin lecturas") :
                         age < 0 ? F("Hora sin sincronizar") :
                         String(stale ? F("ANTIGUA ") : F("Hace ")) + String(static_cast<unsigned long>(age / 60)) + F(" min");
  String frame = String(reading.available) + reading.sgv + reading.direction + ageText +
                 message + WiFi.localIP().toString() + String(historyVersion) + String(static_cast<uint32_t>(now / 60));
  if (frame == lastFrame) return;
  lastFrame = frame;
  const auto tone = reading_ui::tone(reading.available, reading.sgv, age,
                                     WiFi.status() == WL_CONNECTED && lastReadingRequestOk);
  const auto colors = view_appearance::palette(appearance);
  const uint16_t background = view_appearance::rgb565(colors.background);
  const uint16_t statusColor = readingColor(tone, colors);
  const uint16_t valueColor = tone == reading_ui::Tone::Old ? view_appearance::rgb565(0xA4B4B6) : statusColor;
  const uint16_t accent = tone == reading_ui::Tone::Normal ? view_appearance::rgb565(colors.accent) : statusColor;
  const uint16_t trendColor = tone == reading_ui::Tone::Old ? valueColor : accent;
  display.fillScreen(background);
  display.drawCircle(120, 120, 115, accent);
  display.drawCircle(120, 120, 114, accent);
  display.setTextDatum(MC_DATUM);
  drawPatientName(background);
  display.setTextColor(valueColor, background);
  const String value = reading.available ? String(reading.sgv) : String(F("---"));
  if (appearance.mode == view_appearance::Mode::Graph) {
    drawValue(value, 104, 96, 150, 57);
    if (reading.available) drawTrend(trendColor, 188, 96, 22);
    display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
    display.drawString(F("mg/dL"), 104, 124, 2);
    drawHistory(now, valueColor, background);
  } else if (appearance.mode == view_appearance::Mode::Lateral) {
    drawValue(value, 100, 118, 164, 90);
    if (reading.available) drawTrend(trendColor, 201, 118, 29);
    display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
    display.drawString(F("mg/dL"), 100, 166, 2);
  } else {
    drawValue(value, 120, 116, 196, 105);
    display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
    display.drawString(F("mg/dL"), 102, 174, 2);
    if (reading.available) drawTrend(trendColor, 155, 173, 20);
  }
  const bool warning = tone == reading_ui::Tone::Low || tone == reading_ui::Tone::High || stale;
  const uint16_t badge = view_appearance::rgb565(tone == reading_ui::Tone::Low ? 0x332025 : warning ? 0x33291E : 0x192A24);
  display.fillRoundRect(48, 184, 144, 21, 10, badge);
  display.setTextColor(warning ? statusColor : view_appearance::rgb565(0xBFE9D7), badge);
  display.drawString(ageText, 120, 195, display.textWidth(ageText, 2) > 136 ? 1 : 2);
  display.setTextColor(view_appearance::rgb565(0x9AABB0), background);
  if (tone == reading_ui::Tone::Low) display.drawString(F("BAJA: menos de 70"), 120, 213, 1);
  else if (tone == reading_ui::Tone::High) display.drawString(F("ALTA: mas de 180"), 120, 213, 1);
  else if (tone == reading_ui::Tone::Normal && message == "Conectado") display.drawString(F("En intervalo"), 120, 213, 1);
  else display.drawString(message.length() > 23 ? message.substring(0,21) + F("..") : message, 120, 213, 1);
  display.drawString(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() :
                     String(F("Sin Wi-Fi (")) + String(lastWifiReason) + ')', 120, 226, 1);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  bootId = ESP.random();
  Serial.printf_P(PSTR("\nNightscout %s; reinicio: %s\n"), VERSION, ESP.getResetReason().c_str());
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
    EEPROM.get(view_appearance::OFFSET, appearance);
    if (!view_appearance::valid(appearance)) appearance = view_appearance::Settings{};
    EEPROM.get(patient_label::OFFSET, patient);
    if (!patient_label::valid(patient)) patient = patient_label::Label{};
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
    Serial.printf_P(PSTR("Wi-Fi desconectado: motivo=%u heap=%u\n"), lastWifiReason, ESP.getFreeHeap());
  });
  webEnabled = device_config::validAdmin(settings);
  if (!device_config::configured(settings)) startProvisioning();
  else {
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.begin(settings.ssid, settings.wifiPassword);
    message = F("Conectando Wi-Fi");
  }
  web.on("/", HTTP_GET, homePage);
  web.on("/api/state", HTTP_GET, statePage);
  web.on("/assets/ui.css", HTTP_GET, [] { sendAsset("text/css; charset=utf-8", web_assets::ui_css, sizeof(web_assets::ui_css)); });
  web.on("/assets/app.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", web_assets::app_js, sizeof(web_assets::app_js)); });
  web.on("/assets/appearance.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", web_assets::appearance_js, sizeof(web_assets::appearance_js)); });
  web.on("/assets/setup.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", web_assets::setup_js, sizeof(web_assets::setup_js)); });
  web.on("/configure", HTTP_GET, configurationPage);
  web.on("/configure", HTTP_POST, saveConfiguration);
  web.on("/settings", HTTP_POST, saveSettings);
  web.on("/settings", HTTP_GET, settingsPage);
  web.on("/appearance", HTTP_POST, saveAppearance);
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
    message = F("Sin Wi-Fi");
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
