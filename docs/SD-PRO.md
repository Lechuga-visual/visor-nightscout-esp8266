# Instalar Nightscout en el SD PRO

Para el SD PRO con **ESP-12F (ESP8266), flash de 4 MB y pantalla cuadrada ST7789 de 240 × 240**. Descarga ambos archivos de la [release v0.9.2][release].

| Archivo | Dónde se carga |
| --- | --- |
| `SDP_Transicion_Nightscout-v0.9.2-WiFi.bin` | En la pantalla **Firmware Update (OTA)** del firmware original del SD PRO. Conserva el prefijo `SDP`. |
| `nightscout-sd-pro-v0.9.2-acceso.bin` | Desde el panel del firmware de transición. Si ya tienes Nightscout, cárgalo en **Actualizar**, como firmware. |

## Desde el firmware original

1. Con el reloj en tu red de casa, abre su IP y ve a **Firmware Update (OTA)**. Sube **`SDP_Transicion_Nightscout-v0.9.2-WiFi.bin`**. Mantén el reloj alimentado durante la escritura y el reinicio.
2. Conéctate a la red **`glucosa-…`** que muestre el reloj con la clave fija **`nglucosa`**. Si el sistema avisa que no tiene Internet, mantén la conexión.
3. Abre **[192.168.4.1][panel]**. El panel de transición abre sin cuenta de administración y muestra clientes conectados, flash y RAM libre. La pantalla muestra cinco barras de prueba de color.
4. Para subir el firmware desde un PC en tu red de casa, usa **Conectar el reloj a tu red** desde la red temporal e ingresa tu Wi-Fi de 2,4 GHz. Abre desde el PC la IP nueva que aparezca en la pantalla. También puedes subirlo desde la red temporal. El firmware de transición conserva la clave de tu red solo hasta reiniciar.
5. En **Instalar Nightscout**, sube **`nightscout-sd-pro-v0.9.2-acceso.bin`**. La segunda etapa admite hasta 1.044.464 bytes.
6. Si es una instalación nueva, el visor mostrará otra red **`glucosa-…`** con la misma clave **`nglucosa`**. Conéctate y vuelve a abrir **[192.168.4.1][panel]**. Ingresa tu Wi-Fi, la URL de Nightscout y crea una clave de administración. El asistente propone `admin` como usuario y te permite cambiarlo.

**Estado de la prueba:** se verificaron compilación, cabeceras, tamaños y sumas de ambas imágenes. La instalación OTA completa desde el firmware original sigue pendiente de prueba física. En una prueba anterior, la carga directa del visor desde el actualizador original informó éxito y el arranque falló con `e:3`. UART permitió recuperarlo. El [procedimiento OTA de referencia][reference] documenta el límite reducido del firmware original.

## Recuperar o instalar directamente por UART

Con el reloj apagado, conecta un adaptador USB–TTL de **3,3 V** con GND común, TX del adaptador a RX del ESP y RX a TX. Alimenta el reloj por USB-C y deja VCC del adaptador desconectado. Une GPIO0 a GND y enciende. Puedes respaldar la flash antes de la primera escritura, con `read_flash 0x0 0x400000 respaldo.bin`.

```sh
esptool.py --chip esp8266 --port COM3 --before no_reset --after no_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 nightscout-sd-pro-v0.9.2-acceso.bin
```

Cambia `COM3` por el puerto real. Después de grabar, apaga, retira el puente GPIO0–GND y enciende.

## Ajustes y compilación

En **Ajustes → Mantenimiento** puedes cambiar la clave de administración o borrar los ajustes y volver al asistente con **Restablecer de fábrica**.

En **Ajustes → Apariencia** puedes calibrar el panel y cambiar el tamaño de los números. **BGR con inversión activada** es el valor predeterminado. El control de brillo usa PWM en GPIO5. HTTPS cifra la conexión con Nightscout sin verificar la identidad del servidor. El token de lectura sigue siendo opcional.

```sh
pio run -e sd_pro_transition
pio run -e sd_pro
```

Los binarios quedan en `.pio/build/<perfil>/firmware.bin`. Antes de distribuirlos, compila sin `include/nightscout_config.h`.

[release]: https://github.com/Lechuga-visual/visor-nightscout-esp8266/releases/tag/v0.9.2
[panel]: http://192.168.4.1/
[reference]: https://giovi321.github.io/smalltv-mod/getting-started/flashing/#sdpro-smart-weather-clock
