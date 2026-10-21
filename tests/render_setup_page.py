"""Render actual C++ setup markup for browser checks; no HTTP/device simulator.

Only Arduino String/flash literals and the HTTP send sink are adapted for a host
compiler. Field templates, escaping and page assembly come from firmware source.
"""
import pathlib
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / 'src/main.cpp').read_text()


def function(start, following):
    return source[source.index(start):source.index(following, source.index(start))]


harness = r'''
#include <iostream>
#include <string>
#include <cstring>
#include "device_settings.h"
#include "patient_label.h"
#include "night_display.h"
#include <cstdio>
#include "generated/web_assets.h"
#define F(x) x
#define FPSTR(x) x
class String : public std::string {
public:
  using std::string::string;
  String(const char* value) : std::string(value) {}
  String(size_t value) : std::string(std::to_string(value)) {}
  void replace(const char* from, const char* to) {
    size_t position = 0;
    while ((position = find(from, position)) != npos) {
      std::string::replace(position, strlen(from), to); position += strlen(to);
    }
  }
};
struct PublicCaStore { static size_t rootCount() { return 120; } };
struct WebSink {
  void sendHeader(const char*, const char*) {}
  void send(int status, const char*, const String& page) { if (status != 200) std::abort(); std::cout << page; }
} web;
bool setupMode = true;
bool authenticated() { return true; }
device_config::Settings settings{};
patient_label::Label patient;
String baseUrl("https://tu-nightscout.example"), csrf("fixture-csrf-only");
'''
harness += function('String htmlEscape(', 'String urlEncode(')
harness += function('void appendServerFields(', 'bool validPinnedKey(')
harness += function('void configurationPage(', 'void saveConfiguration(')
if len(sys.argv) > 1 and sys.argv[1] == 'night':
    harness += 'night_display::Settings nightSettings; uint8_t screenIntensity = 100; int localMinute() { return 1350; }\n'
    harness += function('String minuteText(', 'void saveNightDisplay(')
    harness += r'''
int main() {
  std::cout << "<!doctype html><html lang='es'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><link rel='stylesheet' href='/assets/ui.css'><script src='/assets/night.js' defer></script><main class='settings'>";
  String page;appendNightControls(page);std::cout << page << "</main></html>";
}
'''
else:
    harness += r'''
int main() {
  strcpy(settings.webUsername, "admin");
  configurationPage();
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory)
    (path / 'Arduino.h').write_text('#pragma once\n#define PROGMEM\n')
    (path / 'render.cpp').write_text(harness)
    subprocess.run(['g++', '-std=c++11', '-I', str(path), '-I', str(ROOT / 'include'),
                    str(path / 'render.cpp'), '-o', str(path / 'render')], check=True)
    subprocess.run([str(path / 'render')], check=True)
