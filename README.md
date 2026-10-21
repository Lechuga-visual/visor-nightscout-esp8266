# Pantalla de glucosa con Wemos D1 mini y GC9A01

Primera versión para **ESP8266**, aunque el repositorio se llame `nightscout-esp32`.
Incluye un simulador de una parte de la API de Nightscout, una pantalla circular
de 240 × 240 y un servidor web en el Wemos para cambiar la URL y actualizar el
firmware por Wi-Fi (OTA). Todos los datos del simulador son ficticios.

## 1. Ejecutar el simulador en tu computador

Necesitas Python 3.10 o posterior, sin paquetes adicionales. Desde este repositorio:

```bash
python3 simulator/server.py --host 0.0.0.0 --port 8000
```

Conecta el computador y el Wemos a la misma red local. El Wemos requiere Wi-Fi de 2,4 GHz. Abre en el navegador
la IP local del computador, puerto 8000, para cambiar el escenario. Por ejemplo,
si tu computador tiene IP `192.168.1.100`, la URL base es
`http://192.168.1.100:8000`. Permite conexiones entrantes al puerto 8000 en el
firewall de tu computador. Una red de invitados con aislamiento entre clientes
puede impedir que el Wemos llegue al servidor.

El servidor que se ejecute dentro del entorno en la nube sirve para las pruebas
automatizadas. Para que lo consulte tu Wemos, ejecuta el simulador en tu
computador dentro de tu red local; no necesitas publicar un servidor en Internet.

API implementada:

| Ruta | Respuesta |
| --- | --- |
| `GET /api/v1/entries.json?count=1` | Última lectura; lista de objetos con `sgv`, `direction` y `date` en milisegundos |
| `GET /api/v1/entries/sgv.json?count=12` | Historial, de más reciente a más antiguo, hasta 288 puntos |
| `GET /api/v1/status.json` | Estado básico y unidades `mg/dl` |
| `GET /` | Selector de escenarios |

Los escenarios son `cycle`, `normal`, `rising`, `falling`, `high`, `low`, `stale`,
`empty` y `error`. La interfaz controla el escenario de todas las consultas del
Wemos; también se puede forzar una respuesta con `?scenario=low&count=1`.
`stale` devuelve una lectura de al menos 30 minutos de antigüedad; `empty`
devuelve una lista vacía y `error` devuelve HTTP 503. En `cycle` el escenario
cambia cada minuto para acelerar las pruebas. Las marcas de tiempo de las
lecturas están espaciadas cinco minutos, como en una fuente típica de glucosa.

El simulador implementa solo las rutas anteriores, no un servidor Nightscout
completo, ni carga de datos, autenticación o API v3. La interfaz de control está
pensada para una red local de confianza.

## 2. Cableado de señales

| GC9A01 SPI | Wemos D1 mini | GPIO |
| --- | --- | --- |
| SCL / CLK | D5 | 14 |
| SDA / DIN / MOSI | D7 | 13 |
| CS | D2 | 4 |
| DC | D1 | 5 |
| RST / RES | D0 | 16 |
| GND | GND | — |

En el módulo de siete pines de la foto, conecta también **VCC a 3V3** del Wemos.
Esta guía señala los pares con el mismo color:

![Conexiones entre la pantalla GC9A01 y el Wemos D1 mini](docs/cableado-gc9a01.png)

En este módulo, SDA/SCL son señales **SPI**, no I²C. No se necesita MISO.
El firmware utiliza SPI a 27 MHz y no reserva una imagen completa en RAM.

Realiza el cableado sin alimentación. Las señales del ESP8266 son de 3,3 V.
Si utilizas otra variante con un pin BL/BLK, consulta su ficha antes de
conectarlo; la retroiluminación no está controlada por un GPIO en esta versión.

## 3. Preparar y cargar el firmware

### Binario listo para cargar

Descarga [nightscout-d1-mini-v0.2.0.bin](firmware/nightscout-d1-mini-v0.2.0.bin)
desde la carpeta `firmware` (en GitHub utiliza **Download raw file**). Está
compilado para **Wemos D1 mini ESP8266 con 4 MB de flash**, con los pines de la
tabla anterior. No contiene datos privados ni requiere configurar el Wi-Fi
antes de compilar.

La primera carga se hace por USB, escribiendo el archivo a partir de la
dirección **0x00000**. Las instrucciones y el SHA-256 están en
[firmware/README.md](firmware/README.md).

### Configuración inicial desde el teléfono

1. Enciende el Wemos con la pantalla conectada. Creará una red **`glucosa-XXXXXX`**,
   donde el sufijo identifica tu placa.
2. Conéctate a esa red usando la **clave de 12 caracteres que muestra la pantalla**.
   La clave es aleatoria y cambia cada vez que arranca en modo configuración.
3. Mantén la conexión aunque el teléfono indique que no hay Internet. Abre
   **`http://192.168.4.1/`** en el navegador; no es necesario esperar un portal automático.
4. Introduce el nombre y la contraseña de tu Wi-Fi de **2,4 GHz**, la URL base
   del simulador —por ejemplo `http://192.168.1.100:8000`— y un usuario y
   contraseña de administración de al menos 12 caracteres.
5. Pulsa **Guardar y reiniciar**. El Wemos guardará los datos en su flash y
   apagará la red de configuración al reiniciarse. Reconecta el teléfono a tu
   Wi-Fi habitual y abre la IP que ahora muestra la pantalla del Wemos.

La URL debe usar la IP del computador donde ejecutas el simulador. No uses
`localhost`, que para el Wemos sería el propio dispositivo.

Si al arrancar no consigue conectarse a la red guardada en un minuto, vuelve
al modo de configuración con una nueva clave en pantalla. Para recuperar el
acceso, puedes apagar temporalmente tu punto de acceso Wi-Fi y reiniciar el
Wemos. Un corte de Wi-Fi durante el funcionamiento normal mantiene la
reconexión automática; no abre el modo de configuración.

### Compilar desde el código (opcional)

Instala PlatformIO Core 6.1.18, o utiliza PlatformIO en VS Code. Por ejemplo:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.18
```

En Windows, el ejecutable correspondiente está en `.venv\Scripts\pio.exe`.

Puedes compilar sin crear ningún archivo de configuración privado. Si prefieres
introducir valores predeterminados en la compilación, copia
`include/nightscout_config.example.h` a `include/nightscout_config.h` y completa localmente:

- `WIFI_SSID` y `WIFI_PASSWORD`.
- `DEFAULT_NIGHTSCOUT_URL`, con la IP local del computador y el puerto 8000.
- `WEB_USERNAME` y una `WEB_PASSWORD` propia de al menos 12 caracteres.

`include/nightscout_config.h` está excluido de Git. Si falta, se activa la
configuración inicial por Wi-Fi. Los datos que ya se guardaron mediante la web
tienen prioridad sobre los valores predeterminados de la compilación. No hay
una contraseña de administración predeterminada.

Desde la raíz del repositorio:

```bash
.venv/bin/pio run -e d1_mini
.venv/bin/pio run -e d1_mini -t upload
.venv/bin/pio device monitor -b 115200
```

La primera carga requiere USB. La pantalla muestra la IP del Wemos al conectarse.
Consulta la API cada 30 segundos y presenta glucosa en mg/dL, flecha de tendencia,
antigüedad y estado de conexión. Marca las lecturas simuladas con `DEMO` y las
que tengan diez minutos o más con `LECTURA ANTIGUA`. Si falla una consulta,
conserva el último valor con el error visible. Sin hora NTP válida indica que
la hora no está sincronizada; no asume que la lectura es reciente.

Los colores naranja fuera de 70–180 mg/dL son valores de demostración fijados
en esta versión. La pantalla todavía no incluye gráfica ni alarmas sonoras.

## 4. Cambiar el servidor y actualizar por web

En un navegador de tu misma red, abre `http://IP_DEL_WEMOS/` e ingresa las
credenciales configuradas. También puedes probar `http://glucosa-display.local/`
si tu red admite mDNS; usa la IP si no resuelve.

La página permite guardar otra **URL base**, sin `/api/v1`, credenciales ni
parámetros. La URL se conserva en EEPROM emulada y se vuelve a consultar sin
reiniciar. Guardar otra URL descarta la lectura del servidor anterior y conserva
las claves de Wi-Fi y administración.

En **Configurar Wi-Fi y contraseñas** puedes cambiar todos los datos sin
recompilar. Introduce de nuevo la contraseña de Wi-Fi y elige una contraseña
de administración al guardar; los campos de contraseña no se rellenan con los
valores anteriores. Este formulario requiere iniciar sesión y reinicia el Wemos.

Para actualizar:

1. Descarga el `.bin` publicado o compila con `.venv/bin/pio run -e d1_mini`.
2. En el Wemos abre `/update` con las mismas credenciales.
3. En **Firmware**, selecciona el `.bin` descargado o `.pio/build/d1_mini/firmware.bin` y súbelo.
4. Espera el reinicio y comprueba la conexión, las lecturas y el acceso a la web.

El actualizador incluido en el núcleo ESP8266 también ofrece un formulario
**FileSystem**: no lo necesitas para este proyecto. No subas el firmware allí.
El modelo configurado es D1 mini con 4 MB de flash y espacio para OTA; confirma
la capacidad si usas un clon. Un firmware demasiado grande se rechazará.

Esta actualización usa HTTP con contraseña en la red local; no cifra la carga
ni verifica una firma del fabricante. Úsala en una red de confianza y mantén
el puerto del Wemos sin exposición a Internet. El ESP8266 no ofrece reversión
automática en este proyecto: un firmware que no arranque puede requerir USB.
Las actualizaciones normales de firmware conservan la configuración guardada.
Un borrado completo de flash elimina también esa configuración.

## 5. Conectar después con Nightscout real

La misma pantalla consulta el endpoint v1 de un Nightscout real. Antes de
cambiar la URL a HTTPS, configura en `include/nightscout_config.h` la raíz CA de confianza
que corresponda al certificado del servidor, dentro de `NIGHTSCOUT_ROOT_CA`.
La conexión valida el certificado y el nombre del servidor, y requiere hora NTP.
Esta versión no incorpora un almacén general de certificados: una CA vacía
rechaza HTTPS; no se desactiva la validación TLS.

Si el servidor exige autenticación, configura un token Nightscout de **solo
lectura** en `NIGHTSCOUT_TOKEN`. No se enviará por HTTP. La CA y el token se
actualizan compilando y cargando el firmware. La contraseña administrativa de
Nightscout no es necesaria para consultar lecturas. Esta primera versión asume
entradas `sgv` en mg/dL; aún no permite seleccionar mmol/L en pantalla.

## Verificación

```bash
python3 -m unittest discover -s tests -v
.venv/bin/pio run -e d1_mini
```

Las pruebas Python hacen solicitudes HTTP reales a un servidor efímero local
y comprueban el contrato de la API, fechas, historial y los escenarios de error.
Si está disponible `g++`, también ejecutan seis grupos de pruebas nativas de
validación de ajustes, migración del formato anterior y conservación de claves
al cambiar la URL. Sin `g++`, esa prueba se informa como omitida.
La compilación verifica las bibliotecas para ESP8266; la prueba física de SPI,
Wi-Fi, configuración desde el teléfono, persistencia en flash y OTA debe hacerse
con el Wemos conectado.
