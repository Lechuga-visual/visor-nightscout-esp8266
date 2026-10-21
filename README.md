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

Descarga [nightscout-d1-mini-v0.3.0.bin](firmware/nightscout-d1-mini-v0.3.0.bin)
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
   contraseña de administración de al menos 12 caracteres. Si tu Nightscout
   exige autenticación, introduce también su **token de solo lectura**. Para
   un Nightscout público con HTTPS no necesitas pegar un certificado.
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
las claves de Wi-Fi y administración. También permite guardar un token de solo
lectura y, opcionalmente, una CA privada. Los campos de token nunca se rellenan
con el valor guardado.

Dejar el token vacío conserva el anterior **solo para el mismo origen HTTPS**
(protocolo, dominio y puerto). Al cambiar de origen se borran el token y la CA
propia anteriores para no enviarlos a otro servidor. Puedes introducir el token
del nuevo servidor en ese mismo formulario. Para eliminar uno existente, marca
**Borrar token guardado**; no escribas a la vez un token nuevo.

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
Al actualizar desde 0.2.0 se conservan Wi-Fi, URL y claves de administración.
Introduce el token desde la web: las credenciales antes compiladas a mano no
se transfieren desde el firmware anterior.

## 5. Usar cualquier Nightscout con HTTPS

El mismo `.bin` sirve para configurar distintas instalaciones. En la web del
Wemos introduce:

1. **URL base:** por ejemplo `https://tu-nightscout.example`, sin API secret,
   token, parámetros ni `/api/v1`.
2. **Token de solo lectura:** generado en la administración de tu Nightscout,
   si el servidor exige autenticación. No uses el `API_SECRET` de administrador.
   El firmware rechaza guardar/enviar tokens con una URL HTTP.
3. **Certificados HTTPS:** normalmente deja esta sección sin modificar.

El firmware incluye **120 raíces públicas de confianza** de Mozilla, distribuidas
por certifi 2026.10.19. Están en flash y solo se carga en RAM la raíz necesaria.
No se necesita un certificado propio para el Wemos ni subir una imagen de
filesystem. Se verifican la cadena, el nombre del servidor y la hora; el reloj
se sincroniza por NTP. Hasta tener una hora válida se espera antes de consultar
HTTPS. Las renovaciones del certificado del servidor funcionan mientras la
cadena siga siendo válida bajo una raíz admitida. Nuevas raíces se distribuyen
con actualizaciones del firmware.

Si tu instalación utiliza una **CA privada**, abre la sección opcional y pega
un certificado público de esa autoridad en PEM (máximo 3072 bytes). Debe ser
un único certificado CA válido, sin claves privadas. Esa CA reemplaza el
almacén público para ese servidor; no desactiva la validación. Para volver al
almacén incluido, marca **Borrar CA propia y usar certificados públicos**.

El token y la CA quedan en la configuración local del dispositivo y se cambian
sin recompilar. El repositorio y el binario distribuido no incluyen tokens ni
datos de Wi-Fi de usuarios. El simulador sigue funcionando por HTTP, sin token.

La API consultada sigue siendo v1 y las entradas `sgv` se muestran en mg/dL;
aún no se puede seleccionar mmol/L en pantalla. Ver [certs/README.md](certs/README.md)
para la procedencia y reproducción del almacén de confianza.

## Verificación

```bash
python3 -m unittest discover -s tests -v
.venv/bin/pio run -e d1_mini
```

Las pruebas Python hacen solicitudes HTTP reales a un servidor efímero local
y comprueban el contrato de la API, fechas, historial y los escenarios de error.
Si está disponible `g++`, también ejecutan nueve grupos de pruebas nativas de
validación de ajustes, migración del formato anterior y conservación de claves
al cambiar la URL. Sin `g++`, esa prueba se informa como omitida.
Con `cryptography`, `make`, compiladores nativos y las fuentes de BearSSL del
núcleo ESP8266 instalado, otras dos pruebas verifican las 120 raíces con el
propio decodificador BearSSL y comprueban cadenas válidas, nombres incorrectos,
certificados vencidos, emisores no confiables y firmas manipuladas. Si faltan
esas herramientas, las pruebas TLS se informan como omitidas. En la nube se usa
el Python del venv preparado para ejecutar las once pruebas completas.
La compilación verifica las bibliotecas para ESP8266; la prueba física de SPI,
Wi-Fi, configuración desde el teléfono, persistencia en flash y OTA debe hacerse
con el Wemos conectado.
