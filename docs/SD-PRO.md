# Instalar Nightscout en el SD PRO

Para el SD PRO con **ESP-12F (ESP8266), flash de 4 MB y pantalla cuadrada ST7789 de 240 × 240**. Descarga ambos archivos de la [release v0.9.1][release].

| Archivo | Dónde se carga |
| --- | --- |
| `SDP_Transicion_Nightscout.bin` | En la pantalla **Firmware Update (OTA)** del firmware original del SD PRO. Conserva el prefijo `SDP`. |
| `nightscout-sd-pro-v0.9.1.bin` | Desde el panel del firmware de transición. Si ya tienes Nightscout, cárgalo en **Actualizar**, como firmware. |

## Desde el firmware original

1. Con el reloj en tu red de casa, abre su IP y ve a **Firmware Update (OTA)**. Sube **`SDP_Transicion_Nightscout.bin`**. Mantén el reloj alimentado durante la escritura y el reinicio.
2. El reloj mostrará **`SDPRO-OTA-…`**, una clave y la IP **`192.168.4.1`**. Conecta el teléfono o PC a esa red. Si el sistema avisa que no tiene Internet, mantén la conexión.
3. Abre **[http://192.168.4.1/][panel]**. El panel muestra la información Wi‑Fi, los clientes conectados, la flash detectada y la RAM libre. La pantalla muestra barras roja, verde, azul, blanca y negra.
4. En **Instalar Nightscout**, selecciona **`nightscout-sd-pro-v0.9.1.bin`** y pulsa **Subir por OTA**. El cargador tiene un límite de 1.044.464 bytes para el binario de la segunda etapa.
5. Tras el reinicio, si es una instalación nueva, conéctate a **`glucosa-…`** con la clave de la pantalla y vuelve a abrir **[192.168.4.1][panel]**. El asistente pide la red Wi‑Fi de 2,4 GHz, la URL de Nightscout y una contraseña de administración. La configuración de Wi‑Fi del firmware original se vuelve a ingresar en el visor.

**Estado de la prueba:** se verificaron compilación, cabeceras, tamaños y sumas de ambas imágenes. La instalación OTA completa desde el firmware original sigue pendiente de prueba física. En una prueba anterior, la carga directa del visor desde el actualizador original informó éxito y el arranque falló con `e:3`. UART permitió recuperarlo. El [procedimiento OTA de referencia][reference] documenta el límite reducido del firmware original.

## Recuperar o instalar directamente por UART

Con el reloj apagado, conecta un adaptador USB–TTL de **3,3 V** con GND común, TX del adaptador a RX del ESP y RX a TX. Alimenta el reloj por USB-C y deja VCC del adaptador desconectado. Une GPIO0 a GND y enciende. Puedes respaldar la flash antes de la primera escritura, con `read_flash 0x0 0x400000 respaldo.bin`.

```sh
esptool.py --chip esp8266 --port COM3 --before no_reset --after no_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 nightscout-sd-pro-v0.9.1.bin
```

Cambia `COM3` por el puerto real. Después de grabar, apaga, retira el puente GPIO0–GND y enciende.

## Ajustes y compilación

En **Ajustes → Apariencia** puedes calibrar el panel y cambiar el tamaño de los números. **BGR con inversión activada** es el valor predeterminado. El control de brillo usa PWM en GPIO5. HTTPS cifra la conexión con Nightscout sin verificar la identidad del servidor. El token de lectura sigue siendo opcional.

```sh
pio run -e sd_pro_transition
pio run -e sd_pro
```

Los binarios quedan en `.pio/build/<perfil>/firmware.bin`. Antes de distribuirlos, compila sin `include/nightscout_config.h`.

[release]: https://github.com/Lechuga-visual/visor-nightscout-esp8266/releases/tag/v0.9.1
[panel]: http://192.168.4.1/
[reference]: https://giovi321.github.io/smalltv-mod/getting-started/flashing/#sdpro-smart-weather-clock
