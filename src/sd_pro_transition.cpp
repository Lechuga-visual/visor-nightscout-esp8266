#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>

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
char apName[24];
char apPassword[13];
bool apReady = false;

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

  display.drawString(F("Red Wi-Fi"), 120, 53, 2);
  display.drawString(apName, 120, 73, 2);
  display.drawString(F("Clave"), 120, 99, 2);
  display.drawString(apPassword, 120, 119, 2);
  display.setTextColor(MINT, BACKGROUND);
  display.drawString(F("Abre en el navegador"), 120, 145, 2);
  display.drawString(WiFi.softAPIP().toString(), 120, 168, 4);

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
  page.reserve(2800);
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
  page += apPassword;
  page += F("</code></dd><dt>Dirección del panel</dt><dd><code>http://");
  page += WiFi.softAPIP().toString();
  page += F("/</code></dd><dt>Canal y clientes conectados</dt><dd>");
  page += String(AP_CHANNEL);
  page += F(" · ");
  page += String(WiFi.softAPgetStationNum());
  page += F("</dd><dt>Chip y flash detectada</dt><dd>ESP8266 · ");
  page += String(ESP.getFlashChipRealSize() / 1024);
  page += F(" KiB</dd><dt>RAM libre</dt><dd>");
  page += String(ESP.getFreeHeap());
  page += F(" bytes</dd></dl><p>La pantalla debe mostrar barras roja, verde, azul, blanca y negra.</p></section>"
            "<form action='/update' method='post' enctype='multipart/form-data'>"
            "<h2>Instalar Nightscout</h2><p>Selecciona el binario para SD PRO. Límite de esta etapa: <output id='limit'>");
  page += String(maximumUpload());
  page += F("</output> bytes.</p><label for='firmware'>Firmware .bin</label>"
            "<input type='file' id='firmware' name='firmware' accept='.bin' required>"
            "<button>Subir por OTA</button><p>Al terminar, el reloj se reinicia. Mantén la alimentación durante la carga.</p>"
            "<p id='status' role='status'></p></form></main>"
            "<script>const f=document.querySelector('form'),i=document.querySelector('input'),"
            "m=+document.querySelector('#limit').textContent,s=document.querySelector('#status');"
            "f.onsubmit=e=>{const b=i.files[0];if(!b||!b.name.toLowerCase().endsWith('.bin')||b.size>m){"
            "e.preventDefault();s.textContent='Selecciona un archivo .bin que quepa en el espacio disponible.';return}"
            "f.querySelector('button').disabled=true;s.textContent='Subiendo el firmware. Mantén el reloj conectado.'};</script></html>");
  web.send(200, "text/html; charset=utf-8", page);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(0);
  snprintf(apName, sizeof(apName), "SDPRO-OTA-%06lX", static_cast<unsigned long>(ESP.getChipId()));
  snprintf(apPassword, sizeof(apPassword), "%08lx%04lx", static_cast<unsigned long>(ESP.random()),
           static_cast<unsigned long>(ESP.random() & 0xffff));
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  apReady = WiFi.softAP(apName, apPassword, AP_CHANNEL);
  showConnection();
  if (!apReady) {
    Serial.println(F("No se pudo iniciar el punto de acceso."));
    return;
  }
  web.on("/", HTTP_GET, homePage);
  updater.setup(&web, "/update");
  web.begin();
  Serial.printf("SD PRO helper · AP %s · %s · OTA %u bytes\n", apName,
                WiFi.softAPIP().toString().c_str(), static_cast<unsigned>(maximumUpload()));
}

void loop() {
  if (apReady) web.handleClient();
  delay(2);
}
