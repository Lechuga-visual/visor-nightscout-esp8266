# Install Nightscout on the SD PRO

For the SD PRO with a 4 MB ESP-12F and a square 240 × 240 ST7789 display. Download both files from the [v0.9.2 release][release].

| File | Where to upload it |
| --- | --- |
| `SDP_Transicion_Nightscout-v0.9.2-WiFi.bin` | **Firmware Update (OTA)** in the stock SD PRO web interface. Keep the `SDP` prefix. |
| `nightscout-sd-pro-v0.9.2-acceso.bin` | The transition firmware's installer. On a device already running Nightscout, upload it as firmware from **Update**. |

1. Open the clock's current IP address and upload the transition file through the stock firmware's OTA page. Keep the clock powered during the upload and restart.
2. Join **`glucosa-…`** with the name on the clock and the fixed password **`nglucosa`**. Stay connected if your phone or computer reports no internet.
3. Open **[192.168.4.1][panel]**. The transition panel opens without an administrator account. It shows connected clients, flash size and free RAM. The clock displays five color test bars.
4. To upload from a PC on your home network, select **Conectar el reloj a tu red** on the temporary network and enter your 2.4 GHz Wi-Fi details. Open the new IP shown on the clock from the PC. You can also upload from the temporary network. The transition firmware retains these Wi-Fi credentials only until it restarts.
5. Under **Instalar Nightscout**, upload **`nightscout-sd-pro-v0.9.2-acceso.bin`**. The second stage allows up to 1,044,464 bytes.
6. On first startup, join the new **`glucosa-…`** network with **`nglucosa`** and reopen **[192.168.4.1][panel]**. Set up Wi-Fi and Nightscout and create an administrator password. The suggested username is `admin` and can be changed.

The two binaries passed build, image format and checksum checks. The stock-to-transition-to-Nightscout path still needs an end-to-end hardware test. Directly uploading the full viewer from stock previously resulted in a boot failure (`e:3`), which direct UART flashing recovered. The [reference OTA guide][reference] documents the smaller stock limit.

## UART recovery or direct installation

Use a 3.3 V USB-to-TTL adapter, connect common ground and cross TX and RX. Power the clock via USB-C and keep the adapter VCC disconnected. Hold GPIO0 to GND during power-up. Back up the flash with `read_flash 0x0 0x400000 backup.bin` if you need a copy of the stock image.

```sh
esptool.py --chip esp8266 --port COM3 --before no_reset --after no_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 nightscout-sd-pro-v0.9.2-acceso.bin
```

Replace `COM3` with your serial port. Power off, remove the GPIO0 jumper and reboot after flashing.

## Settings and source builds

Use **Settings → Maintenance** to change the administrator password or clear settings and restart setup with **Factory reset**.

BGR with inversion enabled is the default. Display calibration and number size are under **Settings → Appearance**. Backlight brightness uses PWM on GPIO5. HTTPS encrypts the connection without verifying server identity. The read-only token is optional.

```sh
pio run -e sd_pro_transition
pio run -e sd_pro
```

Build shared binaries without `include/nightscout_config.h`. The results are in `.pio/build/<target>/firmware.bin`.

[release]: https://github.com/Lechuga-visual/visor-nightscout-esp8266/releases/tag/v0.9.2
[panel]: http://192.168.4.1/
[reference]: https://giovi321.github.io/smalltv-mod/getting-started/flashing/#sdpro-smart-weather-clock
