# Install Nightscout on the SD PRO

For the SD PRO with a 4 MB ESP-12F and a square 240 × 240 ST7789 display. Download both files from the [v0.9.1 release][release].

| File | Where to upload it |
| --- | --- |
| `SDP_Transicion_Nightscout.bin` | **Firmware Update (OTA)** in the stock SD PRO web interface. Keep the `SDP` prefix. |
| `nightscout-sd-pro-v0.9.1.bin` | The transition firmware's installer. On a device already running Nightscout, upload it as firmware from **Update**. |

1. Open the clock's current IP address and upload the transition file through the stock firmware's OTA page. Keep the clock powered during the upload and restart.
2. Join **`SDPRO-OTA-…`** using the password shown on the clock. Stay connected if your phone reports no internet.
3. Open **[192.168.4.1][panel]**. The page reports Wi‑Fi details, connected clients, flash size and free RAM. The clock shows red, green, blue, white and black test bars.
4. Select the Nightscout binary under **Instalar Nightscout** and upload it. The second-stage installer allows up to 1,044,464 bytes.
5. After reboot, a fresh installation displays a **`glucosa-…`** setup network and password. Join it and reopen **[192.168.4.1][panel]** to set up Wi‑Fi, the Nightscout URL and an administrator password. Enter your Wi‑Fi details again during Nightscout setup.

The two binaries passed build, image format and checksum checks. The stock-to-transition-to-Nightscout path still needs an end-to-end hardware test. Directly uploading the full viewer from stock previously resulted in a boot failure (`e:3`), which direct UART flashing recovered. The [reference OTA guide][reference] documents the smaller stock limit.

## UART recovery or direct installation

Use a 3.3 V USB-to-TTL adapter, connect common ground and cross TX and RX. Power the clock via USB-C and keep the adapter VCC disconnected. Hold GPIO0 to GND during power-up. Back up the flash with `read_flash 0x0 0x400000 backup.bin` if you need a copy of the stock image.

```sh
esptool.py --chip esp8266 --port COM3 --before no_reset --after no_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 nightscout-sd-pro-v0.9.1.bin
```

Replace `COM3` with your serial port. Power off, remove the GPIO0 jumper and reboot after flashing.

## Settings and source builds

BGR with inversion enabled is the default. Display calibration and number size are under **Settings → Appearance**. Backlight brightness uses PWM on GPIO5. HTTPS encrypts the connection without verifying server identity. The read-only token is optional.

```sh
pio run -e sd_pro_transition
pio run -e sd_pro
```

Build shared binaries without `include/nightscout_config.h`. The results are in `.pio/build/<target>/firmware.bin`.

[release]: https://github.com/Lechuga-visual/visor-nightscout-esp8266/releases/tag/v0.9.1
[panel]: http://192.168.4.1/
[reference]: https://giovi321.github.io/smalltv-mod/getting-started/flashing/#sdpro-smart-weather-clock
