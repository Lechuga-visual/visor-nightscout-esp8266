# Firmware para Wemos D1 mini (ESP8266)

Versión **0.2.0**, GC9A01 SPI 240 × 240, D1 mini con **4 MB de flash**.

- [Descargar el .bin](nightscout-d1-mini-v0.2.0.bin) (en GitHub: **Download raw file**).
- [SHA-256](SHA256SUMS).
- [Datos de compilación](build-info.json).

El binario no contiene credenciales de Wi-Fi ni de administración. En el primer
arranque pide estos datos por web. Usa el cableado del [README principal](../README.md).

## Primera carga por USB

Conecta el Wemos por USB con un cable de datos. En tu herramienta de flasheo
ESP8266 selecciona el puerto de la placa y escribe el `.bin` en **0x00000**.
Usa flash de **4 MB** y modo **DIO**. No se necesita una imagen de filesystem.

Si utilizas Python y esptool 4.x, por ejemplo:

```bash
python -m pip install esptool==4.8.1
python -m esptool --chip esp8266 --port COM3 --baud 115200 write_flash --flash_mode dio --flash_size 4MB 0x00000 nightscout-d1-mini-v0.2.0.bin
```

Cambia `COM3` por el puerto real. En Linux suele ser `/dev/ttyUSB0` y en macOS
`/dev/cu.usbserial-...`. Ejecuta el comando desde la carpeta donde descargaste
el archivo. Reinicia la placa al terminar la carga.

## Datos de la red de configuración

| Dato | Valor |
| --- | --- |
| Red del Wemos | `glucosa-XXXXXX` (sufijo propio de tu placa) |
| Clave de esa red | 12 caracteres mostrados en la pantalla; cambia al reiniciar el modo de configuración |
| Página de configuración | `http://192.168.4.1/` |
| Usuario de administración | El que elijas en el formulario; sugiere `admin` |
| Clave de administración/OTA | La que elijas, de al menos 12 caracteres |

La clave de la red inicial permite conectar el teléfono al Wemos. La contraseña
de tu Wi-Fi habitual se introduce después en el formulario; son claves distintas.
Mantén la conexión aunque el teléfono avise que esta red no tiene Internet.

Guarda tu Wi-Fi de 2,4 GHz, su contraseña, la URL del simulador y los datos de
administración. El Wemos reinicia y muestra su nueva IP en la red de tu casa.
Si no puede conectar en un minuto tras arrancar, vuelve a crear la red de
configuración. Sin la pantalla no podrás leer la clave inicial.

## Actualizaciones siguientes

Con el Wemos conectado a tu Wi-Fi, abre `http://IP_DEL_WEMOS/update`, inicia
sesión con tus datos de administración y sube el `.bin` en **Firmware**. No lo
subas en **FileSystem**. La configuración guardada se conserva al actualizar
normalmente; un borrado completo de flash la elimina.

El archivo se ha compilado y se han probado las validaciones de configuración
en el entorno de desarrollo. La conexión Wi-Fi, el panel SPI, la grabación de
ajustes y la actualización OTA todavía necesitan validación física en la placa.
