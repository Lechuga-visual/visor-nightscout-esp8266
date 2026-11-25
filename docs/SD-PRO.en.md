# Nightscout on the SD PRO

Use the `sd_pro` target for the clock with a 4 MB ESP-12F and a square 240 × 240 SPI ST7789. The `d1_mini` target is for the Wemos with a round GC9A01.

```sh
pio run -e sd_pro
```

The build produces `.pio/build/sd_pro/firmware.bin`. Build shared binaries without `include/nightscout_config.h`, which can hold private credentials.

## First installation over UART

Use a 3.3 V logic USB-to-TTL adapter. Connect common ground and cross TX and RX. Power the clock through USB-C. Hold GPIO0 to GND when powering on to enter the bootloader. You can back up the full flash before writing.

```sh
esptool.py --chip esp8266 --port COM3 --before no_reset --after no_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 .pio/build/sd_pro/firmware.bin
```

Replace COM3 with your serial port. After the write, power off, remove the GPIO0-to-GND jumper, and power on.

On the tested clock, the stock updater reported success and the next boot failed with `e:3`. Writing directly over UART restored boot. The documented stock OTA limit is about 550 KB. The intermediate OTA loader path remains untested with this viewer. The phone could see the initial setup access point but could not join it in that test. A private build with configured Wi-Fi confirmed LAN access to the settings page.

## HTTPS connection

In initial setup or Settings, **HTTPS without certificate verification** is an optional mode for HTTPS URLs. It keeps encryption and skips server identity verification. An intermediary could read the token or alter readings. CA and pinned-key modes are available when the option is unchecked. The clock is still useful for calculating reading age, even though this mode can open HTTPS without it. Diagnostics reports `tls_authentication: insecure` when active.
