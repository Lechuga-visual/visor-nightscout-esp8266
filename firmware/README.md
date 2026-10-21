# Firmware para Wemos D1 mini (ESP8266)

Versión **0.4.0**, GC9A01 SPI 240 × 240, D1 mini con **4 MB de flash**.

- [Descargar el .bin](nightscout-d1-mini-v0.4.0.bin) (en GitHub: **Download raw file**).
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
python -m esptool --chip esp8266 --port COM3 --baud 115200 write_flash --flash_mode dio --flash_size 4MB 0x00000 nightscout-d1-mini-v0.4.0.bin
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

## Nightscout y HTTPS

Puedes configurar tu propio Nightscout desde la misma web: URL base y token de
solo lectura, si es necesario. El token no se muestra al volver a abrir la
página y no se envía por HTTP. Dejar vacío lo conserva para el mismo origen
HTTPS; al cambiar de servidor se borra el anterior. El `API_SECRET` de
administrador no es necesario.

Hay 120 certificados raíz públicos incluidos, sin pasos extra de instalación.
Normalmente deja vacía la sección de certificados. Si tu servidor usa una CA
privada, puedes introducir su PEM público desde esa sección. La cadena, el
nombre y la hora se verifican en el modo de CA; el dispositivo no necesita un certificado
de cliente. El token y cualquier CA propia permanecen solo en el Wemos.

Al actualizar desde 0.2.0 se conservan la red Wi-Fi, la URL y los datos de
administración. Introduce el token en la web después de actualizar. La imagen
0.2.0 anterior permanece disponible para referencia, pero no tiene estos campos.

## Consultar sin NTP

Desde 0.4.0 puedes pegar una **clave pública fija del servidor** en
**Seguridad HTTPS y opción sin NTP**. El archivo PEM debe obtenerse desde un
PC verificando primero el certificado; se incluye la herramienta
[export_server_key.py](../tools/export_server_key.py). Consulta los pasos en el
[README principal](../README.md#https-sin-ntp-fijar-la-clave-pública-del-servidor).

TLS autentica por esa clave y cifra los datos, sin comprobar cadena, nombre
ni fechas del certificado. No se necesita crear otro certificado. Si el proveedor
cambia la clave, debes volver a obtenerla y guardarla. El token sigue siendo
opcional según los permisos de Nightscout.

Después de autenticar HTTPS, el Wemos toma la hora de la cabecera **Date** del
servidor y calcula cuánto tiempo tiene la lectura. No toma la fecha de la lectura
como hora actual. Sin Date válida muestra **Hora sin sincronizar**. NTP no se
inicia automáticamente en este modo; la clave permanece guardada al reiniciar.

## Si queda esperando NTP en el modo de CA

La versión 0.3.1 inicia NTP al conectar el Wi-Fi y reintenta cada 30 segundos
mientras no tiene hora, alternando tres servidores. Si tu red no permite NTP,
abre `http://IP_DEL_WEMOS/`, inicia sesión y, en **Hora del dispositivo**, pulsa
**Usar hora de este teléfono/PC**. Comprueba primero que la fecha y hora de ese
teléfono o computador sean correctas. El Wemos volverá a consultar Nightscout
manteniendo la validación HTTPS. Si reinicias la placa y NTP sigue sin responder,
necesitarás volver a darle la hora desde el navegador.

Puedes subir esta actualización por `/update` incluso sin hora sincronizada;
al pasar desde 0.3.x se conservan Wi-Fi, URL, token y datos de administración.

Si se desconecta del Wi-Fi, la pantalla muestra ahora el motivo numérico.
Cuando reconecte, `/diagnostics` permite revisar señal, memoria y errores sin
mostrar credenciales. No se ha determinado aún la causa en tu placa.

## Actualizaciones siguientes

Con el Wemos conectado a tu Wi-Fi, abre `http://IP_DEL_WEMOS/update`, inicia
sesión con tus datos de administración y sube el `.bin` en **Firmware**. No lo
subas en **FileSystem**. La configuración guardada se conserva al actualizar
normalmente; un borrado completo de flash la elimina.

El archivo se ha compilado y se han probado el reloj, la configuración, los
certificados y conexiones TLS reales con claves fijas RSA y EC, usando la
biblioteca BearSSL del núcleo ESP8266 en el entorno de
desarrollo. La conexión Wi-Fi, HTTPS en el dispositivo, el panel SPI, la grabación
de ajustes y la actualización OTA necesitan validación física en la placa.
