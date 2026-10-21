#include "patient_label.h"
#include <assert.h>
#include <stdio.h>
#include <string>
using namespace patient_label;
int main() {
  // Original EEPROM offsets and size must remain compatible with v0.4/0.5.0.
  static_assert(sizeof(device_config::Settings) == 3732, "Existing layout changed");
  static_assert(offsetof(device_config::Settings, customCa) == 658, "Saved trust key moved");
  Label label;
  assert(valid(label) && !label.name[0]);
  label.magic = 0xFFFFFFFF;
  memset(label.name, 0xFF, sizeof(label.name));
  assert(!valid(label)); // Unused EEPROM in an older firmware.
  label = Label{};
  strcpy(label.name, "María Muñoz");
  assert(valid(label));
  char screen[25]; displayName(label, screen);
  assert(std::string(screen) == "Maria Munoz");
  assert(validName("ÁÉÍÓÚÜÑ áéíóúüñ"));
  strcpy(label.name, "ÁÉÍÓÚÜÑ áéíóúüñ"); displayName(label, screen);
  assert(std::string(screen) == "AEIOUUN aeiouun");
  assert(validName(std::string(24, 'a').c_str()));
  strcpy(label.name, std::string(24, 'W').c_str()); displayName(label, screen);
  assert(strlen(screen) == 20 && std::string(screen + 17) == "...");
  assert(!validName(std::string(25, 'a').c_str()));
  assert(!validName("line\nbreak") && !validName("bad\x7f") && !validName("\xC0\x80"));
  assert(!validName("\xED\xA0\x80") && !validName("\xF4\x90\x80\x80") && !validName("\xF0"));
  memset(label.name, 'a', sizeof(label.name)); assert(!valid(label));
  puts("Patient name storage and UTF-8 checks passed");
}
