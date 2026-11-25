# Firmware for D1 mini and SD PRO

[Español](README.md) · **English**

Both builds include Spanish and English and use ESP8266 with 4 MB flash. [SD PRO guide](../docs/SD-PRO.en.md).

| File | Purpose |
| --- | --- |
| [nightscout-d1-mini-v0.9.0.bin](nightscout-d1-mini-v0.9.0.bin) | Ready-to-install program. On GitHub use **Download raw file**. |
| [nightscout-sd-pro-v0.9.0.bin](nightscout-sd-pro-v0.9.0.bin) | SD PRO · ST7789 |
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

Image size: **744,400 bytes**. Static RAM: **38,012 bytes**, unchanged from 0.8.0.
SHA-256: `c057c01fc77944049626ad330141554b6745061870d57c11f516a83533f26587`.
