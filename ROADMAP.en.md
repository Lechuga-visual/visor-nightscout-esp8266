# Roadmap

[Español](ROADMAP.md) · **English**

The goal is a device that people can assemble or receive, configure without
programming knowledge, and use to check Nightscout clearly and reliably. We want
to grow from a simple local viewer into a more capable, connected device while
keeping users in control of their data.

This is a direction for future work, **not a list of available features or a
promised delivery schedule**. Priorities depend on hardware testing, real needs
and the capabilities of each board.

## Starting point

Firmware **0.8.0** uses a Wemos D1 mini **ESP8266** with a GC9A01. It includes a
physical/web viewer, three modes, a 30-minute graph, name, themes and custom
colors, guided setup, HTTPS, Wi-Fi updates, scheduled content dimming and saved
Spanish/English language selection. See the [current features](README.en.md#current-features)
and limitations before choosing hardware.

The ESP8266's memory limits history and concurrent work. We will consolidate
that base and prepare a platform with more headroom; changing the board alone
is not proof that every reliability problem is solved.

## 1. Consolidate the current experience

- Complete physical validation: first setup, language persistence, display text
  fitting, night schedule, settings storage and updates that retain preferences.
- Run prolonged checks with weak Wi-Fi, Internet outages, missing readings,
  authentication failures and large or incomplete server responses.
- Improve recovery messages so people can distinguish Wi-Fi, time, permissions
  and server problems.
- Keep both languages, guides, screenshots and wiring instructions aligned.
  Use feedback from people who do not program to improve the setup flow.

**Ready when:** someone can follow the guide on a real device, get a reading and
recover connectivity without recompiling; outages never make old data look new.

## 2. Bring the viewer to ESP32

The aim is more memory and processing capacity for additional functions.

| Planned work | Expected outcome |
| --- | --- |
| Select a reference board | Document its model, memory, power, availability and display compatibility; consider PSRAM if measurements justify it. |
| Separate ESP32 build | Clearly identify ESP8266 and ESP32 downloads to avoid flashing the wrong board. |
| Adapt display, storage and Wi-Fi | Define ESP32 wiring; Wemos D0/D1 labels cannot be transferred directly. |
| Measure memory and timings | Verify that HTTPS, local web access and drawing remain stable together. |
| Design settings migration | Make device replacement easier without exposing credentials in example files. |

No ESP32 board has been selected and no ESP32 firmware is available yet. First
preserve the viewer experience, then expand it. Continued ESP8266 maintenance
will depend on testing; features beyond its memory may be ESP32-only.

**Ready when:** the chosen board has an identified binary, its own diagram, a
validated guide and hardware checks, including recovery through USB.

## 3. Use the additional capacity

After measuring the ESP32 version, investigate:

- Longer, more detailed history with memory limits and explicit gaps. Persistent
  storage would require retention, deletion and flash-wear policies.
- mmol/L units, configurable visual ranges, accessibility options and additional
  languages/font sizes. Spanish and English are already available.
- **Real backlight control** using a display with BL/BLK and a suitable circuit.
  This requires new wiring and electrical verification; the current seven-pin
  module only supports dimming its drawn content.
- Easier clock synchronization and HTTPS maintenance without giving up server
  authentication. Automatic key management is deferred to a later stage.
- Easier update identification and recovery: evaluate signed images and rollback
  when the hardware supports them.

History length, extra displays and performance targets will be chosen after
measurement, rather than promised in advance.

## 4. Higher-level integration

The aim is documented connections and clear permissions for a wider system.

| Direction to investigate | Decisions needed first |
| --- | --- |
| Stable local API | Versioning, authentication, fields and query rates. Today's `/api/state` is an internal route without that compatibility guarantee. |
| Home automation and other dashboards | Evaluate MQTT/Home Assistant, explicit access and control over data recipients. |
| Multiple devices or people | Separate identities, servers and permissions; decide what belongs on the viewer or in an additional component. |
| Optional notifications | Define events, recipients, delays, outages and consent. Informational messages must not be presented as guaranteed clinical alarms. |
| Secure remote access | Evaluate a protected connection or companion service instead of exposing local HTTP administration. |
| Broader Nightscout compatibility | Document supported versions/permissions and evaluate newer APIs while preserving age checks. |

These integrations **are not implemented**. They should avoid sharing data by
default and use read-only permissions when sufficient. Insulin control and
automatic treatment decisions remain outside this plan.

## Suggest a change

[Open an issue](https://github.com/Lechuga-visual/visor-nightscout-esp8266/issues)
explaining the need, when you would use it and your board/display model. Do not
post passwords, tokens or personal health data. Before adding a feature, aim for
a reviewable visual/technical proposal and a way to verify it on hardware.

[Project](README.en.md) · [Installation](docs/INSTALLATION.en.md) ·
[Release history](docs/HISTORY.en.md)
