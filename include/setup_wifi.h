#pragma once

#include <stdint.h>
#include <stdio.h>

namespace setup_wifi {
constexpr char PASSWORD[] = "nglucosa";
constexpr size_t SSID_SIZE = 15;

inline void makeSsid(char (&ssid)[SSID_SIZE], uint32_t random) {
  snprintf(ssid, sizeof(ssid), "glucosa-%06lx", static_cast<unsigned long>(random & 0xFFFFFF));
}
}  // namespace setup_wifi
