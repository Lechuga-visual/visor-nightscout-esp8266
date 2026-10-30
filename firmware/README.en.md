# Firmware for Wemos D1 mini ESP8266

[Español](README.md) · **English**

**0.8.1 · Spanish + English in one image** · **4 MB flash** · **GC9A01 SPI 240 × 240**.
This is not ESP32 firmware. Use the documented pin mapping.

| File | Purpose |
| --- | --- |
| [nightscout-d1-mini-v0.8.1.bin](nightscout-d1-mini-v0.8.1.bin) | Ready-to-install program. On GitHub use **Download raw file**. |
| [SHA256SUMS](SHA256SUMS) | Download checksums. |
| [build-info.json](build-info.json) | Source commit, build tools, memory and validation details. |

Start with the [English installation guide](../docs/INSTALLATION.en.md). It covers
wiring, USB installation, phone configuration, HTTPS and troubleshooting.

- First install: USB, address **`0x00000`**, **DIO**, **4 MB**.
- Later updates: Wemos web page → **Update** → upload the `.bin` under **Firmware**,
  not FileSystem.
- No Wi-Fi credentials, tokens or admin passwords are embedded. Enter them in setup.
- Normal updates retain settings. Full flash erase deletes them. No automatic rollback.

## Fixed in 0.8.1

The physical display now says **2 min ago**, instead of **Age 2 min**, in all
three display modes. Old readings retain their warning: **OLD 10 min ago**.
Spanish wording, the web viewer and saved settings are unchanged.

## Choose English

On initial setup or in Settings (Ajustes), find **Idioma / Language**, choose **English**
and press **Guardar idioma / Save language**. The physical and web displays
change together, without restarting. Choose it before entering other unsaved form
fields, because saving the language reloads the page.

The saved selection survives restart and normal updates. Existing configurations
default to Spanish until changed. Units remain mg/dL and the time zone stays as
configured; language selection does not infer your location.

## Included features

Three display modes, 30-minute graph, large name, five themes/custom colors,
real startup progress, guided setup, HTTPS and scheduled content dimming.
Night scheduling is disabled by default. **Drawn colors dim; the backlight stays
unchanged** on the seven-pin module. The web viewer keeps its normal intensity.

Firmware builds and native/browser checks are recorded in `build-info.json`.
Full SPI/Wi-Fi/EEPROM/OTA verification on the physical board remains pending.
See [release history](../docs/HISTORY.en.md) and [technical reference](../docs/TECHNICAL.en.md).

Image size, SHA-256 and memory measurements are in `build-info.json` and `SHA256SUMS`.
