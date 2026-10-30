# Release history

[Español: detailed implementation notes](HISTORIAL.md) · **English**

This is an English overview of the released changes. Per-release measurements
and validation status belong to the corresponding firmware metadata, not a
claim that every version has been fully tested on physical hardware.

## 0.8.1 — Correct English reading age on the physical display

The round display says **2 min ago**, instead of **Age 2 min**, in all three
modes. Old readings keep their warning, for example **OLD 10 min ago**. Spanish
wording, the web viewer, settings and history are unchanged. Tests exercise the
actual formatter in both languages, the ten-minute threshold, missing clock and
missing readings.

## 0.8.0 — Spanish and English

- One bilingual firmware with a saved language choice for the physical display,
  initial setup, dashboard, settings, status/trend labels and update form.
- Language selector works without JavaScript. Normal administration requires
  authentication and the current session nonce; temporary setup uses its
  protected Wi-Fi and current nonce.
- Separate eight-byte language storage preserves every earlier settings offset.
  Previous devices start in Spanish; selecting English keeps units and time zone.
- English documentation, screenshots and key-export launchers alongside Spanish.
- Translations live in flash; history remains 30 minutes and seven points.

## 0.7.1 — Night schedule

Day/night content intensity, schedule/time zone and a five-second self-restoring
preview. Scheduling defaults to disabled. No LED backlight control on the
seven-pin module; no change to web-viewer intensity.

## 0.7.0 — Startup and setup

Startup with real Wi-Fi/server/first-reading stages and the selected design B.
Three-step configuration with validation, back navigation, a secret-free review
and one final save/restart. Saved theme and large name appear during startup.

## 0.6.1 and 0.6.0 — Views and themes

Larger name, graph and two number-first modes, five color themes and custom
colors with contrast checks. View mode and appearance are shared between the
physical and web displays and persist across restart.

## 0.5.2 — Smaller history

History reduced from two hours to 30 minutes, at most seven points and a request
of up to 31 entries. Static text moved to flash and JSON/history allocations
reduced to leave more ESP8266 heap available.

## 0.5.1 — Streaming HTTP decoding

Decode identity/chunked bodies before incremental JSON parsing, reject incomplete
responses and retain the previous reading on errors. Fall back to latest-reading
requests after history errors, retrying full history later. Added parser/body
diagnostics; removed the emulator and preview-server controls.

Earlier versions introduced the local web viewer, configurable server/token,
public trust roots, pinned public keys and clock/diagnostic improvements. The
[technical reference](TECHNICAL.en.md) describes their current behavior.
