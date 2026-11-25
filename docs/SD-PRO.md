# Nightscout en el SD PRO

El perfil `sd_pro` corresponde al reloj con módulo ESP-12F, flash de 4 MB y pantalla cuadrada ST7789 SPI de 240 × 240. El perfil `d1_mini` corresponde al Wemos con GC9A01 redonda.

```sh
pio run -e sd_pro
```

El resultado queda en `.pio/build/sd_pro/firmware.bin`. Para distribuirlo, compila sin `include/nightscout_config.h`, que puede contener credenciales privadas.

## Primera instalación por UART

Usa un adaptador USB–TTL con niveles de 3,3 V. Conecta GND común, TX del adaptador a RX del ESP y RX del adaptador a TX del ESP. Alimenta el reloj por USB-C. Para entrar al cargador, une GPIO0 a GND antes de encender. Puedes respaldar la flash completa antes de grabar.

```sh
esptool.py --chip esp8266 --port COM3 --before no_reset --after no_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 .pio/build/sd_pro/firmware.bin
```

Cambia COM3 por el puerto real. Al terminar, apaga, retira el puente GPIO0–GND y enciende.

En el reloj probado, el OTA original informó éxito y el arranque posterior falló con `e:3`. La grabación directa por UART recuperó el arranque. El espacio OTA original documentado es de unos 550 KB. La ruta con cargador OTA intermedio sigue pendiente de prueba con este visor. El punto de acceso inicial apareció, pero el teléfono no logró unirse en esa prueba. Una compilación privada con Wi-Fi preconfigurado permitió confirmar acceso al panel por LAN.

## Conexión HTTPS

En configuración inicial o Ajustes, **HTTPS sin validar certificado** activa un modo opcional para URL HTTPS. Conserva el cifrado y omite verificar la identidad del servidor. Un intermediario podría leer el token o alterar las lecturas. Los modos con CA y clave fijada siguen disponibles al desmarcar la opción. La hora sigue siendo útil para calcular la antigüedad de las lecturas, aunque en este modo no es necesaria para abrir HTTPS. Diagnóstico muestra `tls_authentication: insecure` cuando está activo.
