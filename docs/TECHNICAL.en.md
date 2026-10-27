# Technical reference

[Español](REFERENCIA-TECNICA.md) · **English**

For assembly and everyday use, start with [Installation](INSTALLATION.en.md).
Current hardware: Wemos D1 mini ESP8266, 4 MB flash, GC9A01 SPI 240 × 240.
The exact pins are defined in [platformio.ini](../platformio.ini).

## Build from source

Use PlatformIO Core **6.1.18**, or the VS Code PlatformIO extension:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.18
.venv/bin/pio run -e d1_mini
.venv/bin/pio run -e d1_mini -t upload
.venv/bin/pio device monitor -b 115200
```

On Windows, use `.venv\Scripts\pio.exe`. Commands run from the repository root.
First upload requires USB. Output is `.pio/build/d1_mini/firmware.bin`.

No private configuration file is required. Optionally copy
`include/nightscout_config.example.h` to ignored `include/nightscout_config.h`
and set Wi-Fi, default Nightscout URL and admin credentials. There is no default
admin password. Saved web settings take precedence over compile-time defaults.
Never commit private configuration, tokens or keys.

The pinned platform is espressif8266 4.2.1 / Arduino core 3.1.2, TFT_eSPI 2.5.43
and ArduinoJson 6.21.5. SPI runs at 27 MHz. No full-screen framebuffer is allocated.

## Languages and generated assets

Firmware **0.8.0** contains Spanish and English in one image. The language selector
in initial setup and Settings saves a tagged, **8-byte** extension at EEPROM
offset **3872**, after the existing 16-byte night settings. Erased/invalid storage
defaults to Spanish. Previous Wi-Fi, server, token, PEM, name, appearance and
schedule layouts are unchanged. Switching language does not change units,
time zone, network identity or secrets.

- `web/` contains the Spanish source; `web/en/` contains generated English assets.
- `translations/en.json` maps fixed source text to English.
- `tools/localization.py` translates only build-time literals/templates. User
  names, passwords, tokens and server URLs are never rewritten.
- `include/generated/ui_translations.h` holds English C++ strings in flash.
  Fixed UI literals are selected by a generated hash switch; the generator rejects
  collisions among its known keys. Spanish strings remain available as fallback.
- Internal network statuses keep their canonical representation. Only displayed
  copies and API human-readable labels are localized; machine field names and
  diagnostic codes remain unchanged.
- `include/generated/web_assets.h` embeds the shared CSS and both languages'
  gzip assets in flash. Nothing is loaded from a CDN or uploaded as FileSystem.

After changing source UI text or translations:

```bash
python3 tools/embed_web_assets.py
python3 tools/package_key_tools.py
```

Commit generated assets/English launchers and rerun the relevant checks. Do not
edit `web/en/` or generated headers directly. The language form reloads its page;
choose the language before entering other unsaved form values. Open dashboard
tabs detect a changed language through `/api/state` and reload; other pages can
be reloaded manually. The device-wide selection applies to all browsers.

English key launchers are generated from the same Spanish script sources and
catalog. They retain certificate verification, supported curves, no-overwrite
behavior and read-only public-key export. Their English ZIPs are separate from
the firmware; installing them does not configure the Wemos automatically.

## Reading and memory behavior

Nightscout v1 is polled every 30 seconds. Up to 31 entries are streamed into at
most seven five-minute buckets spanning 30 minutes. The most recent sample of
a bucket is retained. Gaps over 15 minutes are not joined. History lives in RAM
and is requested again after boot, without persistent writes for each reading.

The body reader uses a 512-byte buffer and accepts Content-Length, connection
close and chunked transfer framing. Compressed, truncated, oversized or malformed
responses are rejected before replacing the current reading. JSON is decoded
incrementally rather than retaining the entire response. Parsing is not started
with less than 3072 bytes of free heap; this guard is not a guarantee against all
allocation failures elsewhere in the networking stack.

On history failure, latest-reading requests continue every 30 seconds; full history
is retried after five minutes. Previous data is retained with a partial-history/error
status. Empty valid responses mean no readings. A request error is never a new measurement.

Only mg/dL is supported. The visual limits are below 70 / above 180; readings at
least ten minutes old are marked old. No clock means unknown age rather than fresh
data. The browser refreshes local state every five seconds; refresh does not force
a Nightscout request. Browser disconnection keeps the last value with a warning.

The person's name stores up to 24 valid UTF-8 characters. The web retains UTF-8;
the small built-in TFT font transliterates common Spanish letters and substitutes
unsupported characters. Long displayed names are shortened without changing storage.

Appearance uses a tagged 20-byte extension at offset 3836; the schedule occupies
16 bytes at 3856. The language extension follows at 3872. Night intensity scales
RGB565 content, not the module's LED backlight. The preview restores itself after
five seconds, including after `millis()` rollover, and blocking TLS is deferred
until restoration. Without clock time, day intensity is used.

Refer to [build-info.json](../firmware/build-info.json) for measured image/RAM
sizes and the exact source commit. Static RAM is not the free heap during TLS.

## Local HTTP routes

| Route | Purpose |
| --- | --- |
| `/` | Dashboard, or initial setup in temporary AP mode. |
| `/api/state` | Authenticated viewer JSON, including `language`, translated status/trend labels and session nonce; no passwords or Nightscout token. |
| `/settings` | Server, name, appearance, language, clock and night preferences. |
| `/configure` | Wi-Fi, server and admin setup; saving restarts the device. |
| `/language` | POST only; strict `es`/`en`, current CSRF nonce and admin authentication outside temporary setup. Saves only the language extension. |
| `/diagnostics` | Authenticated network, memory, HTTP/TLS, clock and reset details, without glucose values or credentials. |
| `/firmware` | Local firmware-update form. |
| `/update` | ESP8266 core updater; use Firmware, not FileSystem. |

Normal pages use administrator Basic authentication over local HTTP. Temporary
setup allows configuration without that login, but access to its Wi-Fi requires
a random 12-character on-screen password. A per-boot CSRF nonce protects changes,
including language selection. `/api/state` is currently an internal interface,
not a versioned integration contract. Never expose the admin port to the Internet.

## HTTPS and authentication

Use the Nightscout base URL without API paths, credentials or query parameters.
A read-only token is optional depending on the server. Tokens are not sent over
HTTP, and the Nightscout administrator `API_SECRET` is not needed.

Blank token/PEM inputs keep saved values only for the same HTTPS origin. Changing
origin clears the old token and custom trust material. Explicit clear checkboxes
remove them; clear plus a new value is rejected. This prevents forwarding old
credentials to another server.

### Public or private CA

The default trust store contains 120 public roots from Mozilla via certifi
2026.10.19. DER/index data stay in flash and only the needed root is decoded into
RAM. CA mode checks chain, hostname and certificate dates, and waits for valid
clock time. Firmware updates can refresh roots; certificate renewal under an
accepted root normally needs no manual change.

An optional single public private-CA certificate, up to 3072 bytes of PEM, replaces
the public store for that server. It must be a CA, not a private key. It does not
disable verification. Clear custom trust to return to the bundled public store.
The [trust-store reference](../certs/README.md) records provenance and reproduction.

### Pinned public key without NTP

An explicitly saved RSA or supported EC public key authenticates the server by
proof of possession of the matching private key. TLS remains encrypted, but
certificate-chain, hostname and date checks are not performed in this mode.
Unknown replacement keys are never accepted automatically. Keys are bound to the
configured HTTPS origin and cleared on origin changes.

Obtain a key only after normal site verification on a trusted computer with
correct time. The [desktop tools](../tools/key-export/README.en.md) do that without
requesting a token or readings. Alternatively:

```bash
python -m pip install cryptography
python tools/export_server_key.py https://your-nightscout.example --output server-key.pem
```

A trusted private CA can be passed as `--ca-file your-ca.pem`. Paste the entire
public PEM into the device; never paste a private key. Key rotation requires an
explicit update; certificate renewal with the same key does not.

Only a successful pinned HTTPS response can bootstrap time from a fresh `Date`
header. Responses declaring a positive cache age do not set the clock. Reading
timestamps and unverified HTTP responses are never used as the current time.
No valid Date means unknown age, and browser time remains available. NTP does
not start automatically in pinned-key mode.

### Clock and connectivity diagnostics

CA mode starts/retries NTP after Wi-Fi connects, alternating `pool.ntp.org`,
`time.google.com` and `time.cloudflare.com` over UDP 123 every 30 seconds while
unsynchronized. Browser time requires a valid bounded epoch. It lasts until
restart unless another authenticated source synchronizes the clock.

Wi-Fi sleep is disabled and reconnect attempts occur every 15 seconds after an
initial successful connection. At boot, failure to connect for one minute opens
temporary setup. Later outages reconnect without opening setup mode.

`/diagnostics` reports signal, disconnect count/reason, reset details, last
operation, stack/heap margins, HTTP response/body framing and JSON decoding stage.
Examples of Wi-Fi reasons: 200 beacon timeout, 201 access point not found, 202
authentication failure, 204 handshake timeout. Reason 0 means no cause recorded.
A stale form rejection occurs before PEM validation; reload after restart/update.

### Updates

The `.bin` starts at address `0x00000`, DIO, 4 MB. A normal OTA preserves settings;
full flash erase removes them. The included updater does not provide manufacturer
signature verification or automatic rollback. A failed boot can require USB.
Use it only on a trusted LAN with stable power.

## Verification

```bash
python3 -m unittest discover -s tests -v
.venv/bin/pio run -e d1_mini
CHROMIUM_PATH=/usr/bin/chromium node tests/test_web_views.js
CHROMIUM_PATH=/usr/bin/chromium node tests/test_setup_views.js
CHROMIUM_PATH=/usr/bin/chromium node tests/test_night_views.js
CHROMIUM_PATH=/usr/bin/chromium node tests/test_language_views.js
```

Native tests need `g++`. TLS tests also use cryptography, make, native compilers
and the installed ESP8266 core's BearSSL sources; unavailable dependencies are
reported as skips. Browser checks require Node, Playwright and Chromium. Set
`KEY_EXPORT_PWSH` if the PowerShell test runner is installed outside PATH.
Key-tool CI validates native Windows, Linux and macOS behavior separately.

Coverage includes settings migration, bounds, HTTP framing, incremental JSON,
history, trust roots, TLS rejection, pinned keys, clock handling, name validation,
appearance contrast, startup, schedule/DST, language persistence and protection
against unauthorized or stale language changes. Browser requests are intercepted
against local assets and production C++ form output; no preview/emulator server
is started. Screenshots use synthetic readings and generic names.

These checks do not replace physical validation of SPI, Wi-Fi, EEPROM writes,
real screen text, brightness or OTA on the Wemos. Hardware validation status is
recorded separately in the firmware metadata.
