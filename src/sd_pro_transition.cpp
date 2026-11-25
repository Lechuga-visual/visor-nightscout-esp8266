#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>
#include "setup_wifi.h"

namespace {
constexpr int AP_CHANNEL = 6;
constexpr size_t APP_LIMIT = 1044464;  // 4m1m application region, excluding image headers.
constexpr uint16_t BACKGROUND = 0x0842;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t MINT = 0xB76E;  // BGR565 for the calibrated SD PRO.
constexpr uint16_t BORDER = 0x6B4D;

ESP8266WebServer web(80);
ESP8266HTTPUpdateServer updater;
TFT_eSPI display;
char apName[setup_wifi::SSID_SIZE];
bool apReady = false;
bool joinRequested = false;
uint32_t joinStarted = 0;
enum class HomeWifi { NotConfigured, Connecting, Connected, Unavailable };
HomeWifi lastHomeWifi = HomeWifi::NotConfigured;
IPAddress lastHomeIp;

HomeWifi homeWifi() {
  if (!joinRequested) return HomeWifi::NotConfigured;
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress()) return HomeWifi::Connected;
  return millis() - joinStarted < 25000 ? HomeWifi::Connecting : HomeWifi::Unavailable;
}

String homeMessage(HomeWifi state) {
  switch (state) {
    case HomeWifi::Connected: return WiFi.localIP().toString();
    case HomeWifi::Connecting: return F("Conectando…");
    case HomeWifi::Unavailable: return F("Sin conexión. Revisa la red y la clave.");
    default: return F("Sin configurar");
  }
}

size_t maximumUpload() {
  // ESP8266HTTPUpdateServer keeps one extra erase sector beyond the upload.
  const size_t free = ESP.getFreeSketchSpace();
  const size_t staging = free > 4096 ? (free - 4096) & ~size_t(4095) : 0;
  return staging < APP_LIMIT ? staging : APP_LIMIT;
}

void showConnection() {
  display.fillScreen(BACKGROUND);
  display.drawRect(4, 4, 232, 232, MINT);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(MINT, BACKGROUND);
  display.drawString(F("SD PRO / TRANSICION"), 120, 23, 2);

  display.setTextColor(WHITE, BACKGROUND);
  if (!apReady) {
    display.drawString(F("No inicio la red Wi-Fi"), 120, 93, 2);
    display.drawString(F("Reinicia el dispositivo"), 120, 120, 2);
    return;
  }

  display.drawString(apName, 120, 54, 2);
  display.drawString(String(F("Clave: ")) + setup_wifi::PASSWORD, 120, 76, 2);
  display.drawString(String(F("Panel temporal: ")) + WiFi.softAPIP().toString(), 120, 98, 2);
  display.setTextColor(MINT, BACKGROUND);
  display.drawString(F("Panel para el PC"), 120, 130, 2);
  switch (homeWifi()) {
    case HomeWifi::Connected:
      display.drawString(WiFi.localIP().toString(), 120, 158, 4);
      break;
    case HomeWifi::Connecting:
      display.drawString(F("Conectando a tu Wi-Fi"), 120, 158, 2);
      break;
    case HomeWifi::Unavailable:
      display.drawString(F("Revisa la red y la clave"), 120, 158, 2);
      break;
    default:
      display.drawString(F("Conecta el reloj a tu Wi-Fi"), 120, 158, 2);
  }

  constexpr uint16_t test[] = {0x001F, 0x07E0, 0xF800, 0xFFFF, 0x0000};
  for (unsigned i = 0; i < 5; ++i) {
    const int x = 29 + i * 37;
    display.fillRect(x, 204, 33, 13, test[i]);
    display.drawRect(x, 204, 33, 13, BORDER);
  }
}

void homePage() {
  web.sendHeader(F("Cache-Control"), F("no-store"));
  String page;
  page.reserve(3800);
  page += F("<!doctype html><html lang='es'><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta name='color-scheme' content='dark'><title>SD PRO · Transición</title>"
            "<style>body{background:#10191a;color:#eff6f2;font:16px/1.5 system-ui;max-width:38rem;margin:auto;padding:1.25rem}"
            "h1{margin:0}h2{font-size:1.15rem}section,form{border-top:1px solid #465753;margin-top:1.4rem;padding-top:.5rem}"
            "dt{color:#afc4bb}dd{margin:0 0 .55rem;overflow-wrap:anywhere}code,output{color:#94eabf}"
            "input,button{font:inherit;max-width:100%;display:block;margin:.75rem 0}button{padding:.55rem .9rem}"
            "</style><main><h1>SD PRO · Transición</h1>"
            "<section><h2>Prueba del módulo</h2><dl><dt>Red Wi-Fi del dispositivo</dt><dd><code>");
  page += apName;
  page += F("</code></dd><dt>Clave Wi-Fi</dt><dd><code>");
  page += setup_wifi::PASSWORD;
  page += F("</code></dd><dt>Dirección del panel</dt><dd><code>http://");
  page += WiFi.softAPIP().toString();
  page += F("/</code></dd><dt>Canal y clientes conectados</dt><dd>");
  page += String(WiFi.channel());
  page += F(" · ");
  page += String(WiFi.softAPgetStationNum());
  page += F("</dd><dt>Chip y flash detectada</dt><dd>ESP8266 · ");
  page += String(ESP.getFlashChipRealSize() / 1024);
  page += F(" KiB</dd><dt>RAM libre</dt><dd>");
  page += String(ESP.getFreeHeap());
  page += F(" bytes</dd></dl><p>La pantalla debe mostrar barras roja, verde, azul, blanca y negra.</p></section>"
            "<section><h2>Conectar el reloj a tu red</h2><p>Usa tu Wi-Fi de 2,4 GHz. Luego abre la IP del reloj desde un PC en la misma red.</p>"
            "<p id='network' role='status'>");
  page += homeMessage(homeWifi());
  page += F("</p><a id='lan-link' hidden>Abrir panel en la red de casa</a>");
  if (web.client().localIP() == WiFi.softAPIP()) {
    page += F("<form id='wifi' action='/wifi' method='post'>"
              "<label for='ssid'>Nombre de tu red</label><input id='ssid' name='ssid' maxlength='32' required>"
              "<label for='password'>Clave de tu red</label><input id='password' name='password' type='password' maxlength='64' autocomplete='new-password'>"
              "<button>Conectar a Wi-Fi</button></form>");
  }
  page += F("</section><form id='ota' action='/update' method='post' enctype='multipart/form-data'>"
            "<h2>Instalar Nightscout</h2><p>Selecciona el binario para SD PRO. Límite de esta etapa: <output id='limit'>");
  page += String(maximumUpload());
  page += F("</output> bytes.</p><label for='firmware'>Firmware .bin</label>"
            "<input type='file' id='firmware' name='firmware' accept='.bin' required>"
            "<button>Subir por OTA</button><p>Al terminar, el reloj se reinicia. Mantén la alimentación durante la carga.</p>"
            "<p id='status' role='status'></p></form></main>"
            "<script>const f=document.querySelector('#ota'),i=document.querySelector('#firmware'),"
            "w=document.querySelector('#wifi'),n=document.querySelector('#network'),a=document.querySelector('#lan-link');"
            "let uploading=false,joining=false;"
            "async function status(){if(uploading||joining||document.hidden)return;try{"
            "const r=await fetch('/network',{cache:'no-store'});if(!r.ok)return;"
            "const t=await r.text();n.textContent=t;a.hidden=!/^\\d+(\\.\\d+){3}$/.test(t);"
            "if(!a.hidden)a.href='http://'+t+'/';}catch{}}"
            "if(w)w.onsubmit=async e=>{e.preventDefault();if(joining)return;joining=true;"
            "const b=w.querySelector('button');b.disabled=true;n.textContent='Conectando…';"
            "try{const r=await fetch('/wifi',{method:'POST',body:new URLSearchParams(new FormData(w))});"
            "n.textContent=await r.text();if(r.ok)w.reset();}catch{n.textContent='Comprueba la IP en la pantalla.';}"
            "finally{joining=false;b.disabled=false;}};"
            "status();setInterval(status,2000);const "
            "m=+document.querySelector('#limit').textContent,s=document.querySelector('#status');"
            "f.onsubmit=e=>{const b=i.files[0];if(!b||!b.name.toLowerCase().endsWith('.bin')||b.size>m){"
            "e.preventDefault();s.textContent='Selecciona un archivo .bin que quepa en el espacio disponible.';return}"
            "uploading=true;f.querySelector('button').disabled=true;s.textContent='Subiendo el firmware. Mantén el reloj conectado.'};</script></html>");
  web.send(200, "text/html; charset=utf-8", page);
}
void connectHomeWifi() {
  web.sendHeader(F("Cache-Control"), F("no-store"));
  if (web.client().localIP() != WiFi.softAPIP()) {
    web.send(403, "text/plain; charset=utf-8", F("Configura el Wi-Fi desde la red temporal del reloj."));
    return;
  }
  const String ssid = web.arg("ssid");
  const String password = web.arg("password");
  if (!ssid.length() || ssid.length() > 32 || strlen(ssid.c_str()) != ssid.length() ||
      (password.length() && password.length() < 8) || password.length() > 64 ||
      strlen(password.c_str()) != password.length()) {
    web.send(400, "text/plain; charset=utf-8", F("Revisa el nombre y la clave de la red."));
    return;
  }
  web.send(202, "text/plain; charset=utf-8", F("Conectando; mira la IP en la pantalla."));
  joinRequested = true;
  joinStarted = millis();
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
}

void networkPage() {
  web.sendHeader(F("Cache-Control"), F("no-store"));
  web.send(200, "text/plain; charset=utf-8", homeMessage(homeWifi()));
}
}  // namespace

void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(0);
  setup_wifi::makeSsid(apName, ESP.random());
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  apReady = WiFi.softAP(apName, setup_wifi::PASSWORD, AP_CHANNEL);
  showConnection();
  if (!apReady) {
    Serial.println(F("No se pudo iniciar el punto de acceso."));
    return;
  }
  web.on("/", HTTP_GET, homePage);
  web.on("/network", HTTP_GET, networkPage);
  web.on("/wifi", HTTP_POST, connectHomeWifi);
  updater.setup(&web, "/update");
  web.begin();
  Serial.printf("SD PRO helper · AP %s · %s · OTA %u bytes\n", apName,
                WiFi.softAPIP().toString().c_str(), static_cast<unsigned>(maximumUpload()));
}

void loop() {
  if (apReady) {
    web.handleClient();
    const HomeWifi state = homeWifi();
    const IPAddress ip = state == HomeWifi::Connected ? WiFi.localIP() : IPAddress();
    if (state != lastHomeWifi || ip != lastHomeIp) {
      lastHomeWifi = state;
      lastHomeIp = ip;
      if (state == HomeWifi::Connected) Serial.println(ip);
      showConnection();
    }
  }
  delay(2);
}
