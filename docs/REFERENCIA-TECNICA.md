# Referencia técnica

Para montar y usar el dispositivo, comienza por la [guía de instalación](INSTALACION.md).
Este documento reúne los detalles de compilación, memoria, HTTPS, diagnóstico y
pruebas. El [historial](HISTORIAL.md) conserva las notas de las versiones anteriores.

Hardware actual: **Wemos D1 mini ESP8266, 4 MB de flash, GC9A01 SPI 240 × 240**.
Los pines de compilación están en [platformio.ini](../platformio.ini) y se ilustran
en el [nuevo diagrama de conexiones](images/conexiones-wemos-gc9a01.png).
La compilación distribuida se identifica en [build-info.json](../firmware/build-info.json).

## Compilar desde el código

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
- `DEFAULT_NIGHTSCOUT_URL`, con la URL base de tu Nightscout.
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
antigüedad y estado de conexión. Marca las lecturas
que tengan diez minutos o más con `ANTIGUA`. Si falla una consulta,
conserva el último valor con el error visible. Sin una hora válida indica que
la hora no está sincronizada; no asume que la lectura es reciente.

La interfaz usa un diseño oscuro de alto contraste y una mini gráfica
de los últimos 30 minutos. Con el tema predeterminado, la lectura es menta entre 70–180 mg/dL,
rojo por debajo y ámbar por encima. Ese intervalo visual está fijado en esta
versión y no constituye una alarma personalizada. Las lecturas antiguas y los
errores se distinguen por texto además del color. No hay alarmas sonoras.


## Visor web y estado local

En **Ajustes → Nombre de la persona** puedes escribir hasta 24 caracteres.
El nombre reemplaza NIGHTSCOUT arriba en la pantalla redonda y en el visor web.
Se guarda localmente y permanece después de reiniciar. Vacío vuelve a mostrar
NIGHTSCOUT. La web conserva las tildes; la fuente de la pantalla adapta
las letras españolas a sus equivalentes sin tilde y otros caracteres a `?`.
Los nombres largos se acortan con puntos en ambos visores; el nombre completo
sigue guardado y está disponible como etiqueta accesible en la web.
Los ajustes anteriores de Wi-Fi, servidor y HTTPS mantienen su formato.

Abre `http://IP_DEL_WEMOS/` e inicia sesión con tu usuario y contraseña de
administración. El panel muestra la misma glucosa, tendencia, antigüedad,
estado e historial de la pantalla redonda. Se adapta al teléfono y al computador.

El visor consulta el estado local del Wemos cada cinco segundos; el botón
**Actualizar visor** también lee ese estado y no fuerza consultas adicionales
a Nightscout. El Wemos conserva su intervalo de consulta de 30 segundos.
Dejar el panel abierto no pospone las consultas a Nightscout. Si el navegador
pierde contacto con el Wemos, conserva el último valor recibido, actualiza su
antigüedad cuando se conoce y muestra que el dispositivo no responde. No se
presenta como una lectura recién recibida.

La gráfica usa hasta 7 puntos, guardando la muestra más reciente de cada
bloque de cinco minutos. Se solicitan hasta 31 entradas y se decodifican una
por una para cubrir 30 minutos incluso con sensores que envían cada minuto,
sin guardar toda la respuesta JSON en RAM. Los huecos de más de 15 minutos no
se unen con una línea. Si falta la hora o no hay historial reciente se indica
en pantalla. El historial vive en RAM y se vuelve a obtener del servidor después
de reiniciar; no produce escrituras adicionales en flash.

El panel y `/api/state` requieren autenticación y no exponen el token Nightscout
ni contraseñas. Los recursos web están comprimidos en la flash del firmware;
funcionan sin servicios de fuentes, librerías externas ni cargas de FileSystem.
La hora de lectura en el navegador se muestra en la zona horaria del teléfono/PC.


### Rutas locales

Estas rutas pertenecen al Wemos, no al servidor Nightscout. Las páginas de uso
normal requieren el acceso de administración. En modo de configuración temporal,
`/` y `/configure` permiten configurar sin esa autenticación; el acceso a esa red
requiere la clave aleatoria mostrada físicamente en la pantalla. Esto también
se aplica al modo temporal que se abre tras un arranque sin poder conectar al Wi-Fi.

| Ruta | Uso |
| --- | --- |
| `/` | Panel y copia del visor. |
| `/api/state` | Estado del visor en JSON; no incluye contraseñas ni token Nightscout. Incluye el nonce de la sesión para cambios desde la web. |
| `/settings` | Servidor, hora, nombre, apariencia y horario nocturno. |
| `/configure` | Wi-Fi, Nightscout y credenciales de administración; guardar reinicia. |
| `/diagnostics` | Diagnóstico de red, memoria, HTTP, TLS y reinicios, sin lecturas ni secretos. |
| `/firmware` | Acceso al actualizador de firmware. |
| `/update` | Actualizador del núcleo ESP8266; seleccionar Firmware, no FileSystem. |

La interfaz local utiliza HTTP y autenticación Basic; su uso previsto es una red
local de confianza. La conexión saliente a Nightscout utiliza HTTPS según el modo
descrito más abajo. El estado JSON no es todavía un contrato de integración
versionado: los consumidores externos deben prever cambios.

### Memoria y persistencia en 0.7.1

- RAM estática de la compilación: **38.084 bytes**; no equivale a la memoria libre durante TLS.
- Historial en RAM: hasta **7 puntos** en bloques de cinco minutos, ventana de 30 minutos.
- Configuración en EEPROM emulada: Wi-Fi, servidor, autenticación, nombre y apariencia.
- Extensiones etiquetadas: nombre de 104 bytes; apariencia de 20 bytes en offset 3836;
  horario nocturno de 16 bytes en offset 3856. Se conserva el formato anterior.
- Recursos web gzip en flash; no se necesita una carga de FileSystem ni una imagen
  completa de pantalla en RAM. La atenuación se aplica a colores RGB565.

## Configuración y actualización por web

En un navegador de tu misma red, abre `http://IP_DEL_WEMOS/` e ingresa las
credenciales configuradas. También puedes probar `http://glucosa-display.local/`
si tu red admite mDNS; usa la IP si no resuelve.

En **Ajustes** (`/settings`) puedes guardar otra **URL base**, sin `/api/v1`, credenciales ni
parámetros. La URL se conserva en la memoria de configuración y se vuelve a consultar sin
reiniciar. Guardar otra URL descarta la lectura del servidor anterior y conserva
las claves de Wi-Fi y administración. También permite guardar un token de solo
lectura y, opcionalmente, una CA privada o una clave pública fija. Los campos de token nunca se rellenan
con el valor guardado.

Dejar el token vacío conserva el anterior **solo para el mismo origen HTTPS**
(protocolo, dominio y puerto). Al cambiar de origen se borran el token y la CA o clave
propia anteriores para no enviarlos a otro servidor. Puedes introducir el token
del nuevo servidor en ese mismo formulario. Para eliminar uno existente, marca
**Borrar token guardado**; no escribas a la vez un token nuevo.

En **Configurar Wi-Fi y contraseñas** puedes cambiar todos los datos sin
recompilar. Introduce de nuevo la contraseña de Wi-Fi y elige una contraseña
de administración al guardar; los campos de contraseña no se rellenan con los
valores anteriores. Este formulario requiere iniciar sesión y reinicia el Wemos.

Para actualizar:

1. Descarga el `.bin` publicado o compila con `.venv/bin/pio run -e d1_mini`.
2. En el Wemos pulsa **Actualizar** (`/firmware`) con las mismas credenciales.
3. En **Firmware**, selecciona el `.bin` descargado o `.pio/build/d1_mini/firmware.bin` y súbelo.
4. Espera el reinicio y comprueba la conexión, las lecturas y el acceso a la web.

El actualizador incluido en el núcleo ESP8266 también ofrece un formulario
**FileSystem**: no lo necesitas para este proyecto. No subas el firmware allí.
La ruta anterior `/update` sigue disponible para compatibilidad.
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

## HTTPS y autenticación

El mismo `.bin` sirve para configurar distintas instalaciones. En la web del
Wemos introduce:

1. **URL base:** por ejemplo `https://tu-nightscout.example`, sin API secret,
   token, parámetros ni `/api/v1`.
2. **Token de solo lectura:** generado en la administración de tu Nightscout,
   si el servidor exige autenticación. No uses el `API_SECRET` de administrador.
   El firmware rechaza guardar/enviar tokens con una URL HTTP.
3. **Seguridad HTTPS y opción sin NTP:** puedes usar certificados CA o fijar la
   clave pública del servidor, como se explica debajo.

El firmware incluye **120 raíces públicas de confianza** de Mozilla, distribuidas
por certifi 2026.10.19. Están en flash y solo se carga en RAM la raíz necesaria.
No se necesita un certificado propio para el Wemos ni subir una imagen de
filesystem. En el modo predeterminado se verifican la cadena, el nombre del servidor
y la hora; el reloj se sincroniza por NTP o desde el navegador. En este modo,
hasta tener una hora válida se espera antes de consultar HTTPS. Las renovaciones
del certificado del servidor funcionan mientras la
cadena siga siendo válida bajo una raíz admitida. Nuevas raíces se distribuyen
con actualizaciones del firmware.

Si tu instalación utiliza una **CA privada**, abre la sección opcional y pega
un certificado público de esa autoridad en PEM (máximo 3072 bytes). Debe ser
un único certificado CA válido, sin claves privadas. Esa CA reemplaza el
almacén público para ese servidor; no desactiva la validación. Para volver al
almacén incluido, marca **Borrar CA o clave fija y usar certificados públicos**.

El token y la CA quedan en la configuración local del dispositivo y se cambian
sin recompilar. El repositorio y el binario distribuido no incluyen tokens ni
datos de Wi-Fi de usuarios.

La API consultada sigue siendo v1 y las entradas `sgv` se muestran en mg/dL;
aún no se puede seleccionar mmol/L en pantalla. Ver [certs/README.md](../certs/README.md)
para la procedencia y reproducción del almacén de confianza.

### HTTPS sin NTP: fijar la clave pública del servidor

Desde 0.4.0 puedes autenticar el servidor mediante su clave pública RSA o EC
fijada en el Wemos. No necesitas crear un certificado autofirmado ni cambiar
Nightscout. TLS sigue cifrando la conexión y exige demostrar la posesión de la
clave privada correspondiente; **no se comprueban cadena, nombre ni fechas del
certificado**, porque la identidad queda fijada por esa clave. El token puede
seguir usándose. Si el servidor cambia de clave, la conexión falla hasta que
actualices la clave guardada; no hay aceptación automática de claves nuevas.

Obtén la clave desde un PC de confianza con su reloj correctamente ajustado.
Para hacerlo con una ventana y sin instalar `cryptography`, utiliza las
[herramientas para Windows, Linux y macOS](../tools/key-export/README.md). Solo piden
la URL y generan un `.txt` con la clave, listo para copiar al Wemos. Hay paquetes
ZIP y capturas de las ventanas. No requieren actualizar el firmware.

Como alternativa por terminal, descarga
[export_server_key.py](../tools/export_server_key.py) y, en esa carpeta, ejecuta:

```bash
python -m pip install cryptography
python export_server_key.py https://tu-nightscout.example --output clave-servidor.pem
```

La herramienta verifica normalmente el certificado y el nombre del servidor
antes de exportar la clave. No envía tokens ni consulta glucosa. Si utilizas
una CA privada, puedes añadir `--ca-file tu-ca.pem`. No obtengas la clave desde
una conexión sin verificar ni desde un proxy que sustituya el certificado del
servidor: estarías fijando la identidad equivocada.

En el Wemos, abre **Ajustes → Seguridad HTTPS y opción sin NTP**, pega todo el contenido
de `clave-servidor.pem` (desde `BEGIN PUBLIC KEY` hasta `END PUBLIC KEY`) y pulsa
**Guardar servidor**, sin marcar la casilla de borrar. Desde 0.4.1 aparece la
confirmación **Servidor guardado**, con el modo de autenticación elegido. El modo se reconoce por
el PEM y se conserva después de reiniciar. La clave es pública; nunca pegues
la clave privada del servidor. Se conserva únicamente para el mismo origen HTTPS.

En este modo NTP no arranca automáticamente. Después de autenticar la conexión,
el Wemos ajusta su reloj con la cabecera **Date** de una respuesta HTTPS correcta
y calcula la antigüedad usando la fecha de cada lectura. Pide respuestas sin
caché y descarta la hora si la respuesta declara una antigüedad de caché mayor
que cero. **La fecha de una lectura nunca se usa como hora actual**: una lectura
antigua debe seguir apareciendo como antigua. Si falta una cabecera Date válida,
puede mostrar el valor con **Hora sin sincronizar**, sin afirmar su antigüedad;
puedes darle la hora desde el navegador.

Un Nightscout con lectura pública puede funcionar con el campo token vacío.
Si ya había uno guardado, marca **Borrar token guardado** para eliminarlo.
La lectura pública permite que otras personas con la URL consulten esos datos;
quitar el token no cambia el funcionamiento de HTTPS.

### Si se queda esperando la hora NTP

En el modo de certificados CA, NTP arranca después de conectar el Wi-Fi y reintenta cada 30 segundos
mientras falta la hora. Alterna entre `pool.ntp.org`, `time.google.com` y
`time.cloudflare.com`, por UDP al puerto 123. Algunas redes bloquean esas consultas.

Si aparece **Esperando hora NTP** o **Sin hora: abre la web**:

1. Desde un teléfono o computador conectado a la misma red, abre
   `http://IP_DEL_WEMOS/` e inicia sesión con tus datos de administración.
2. En **Ajustes → Hora del dispositivo**, comprueba el estado y pulsa **Reintentar NTP**.
3. Si sigue sin sincronizar, comprueba que el teléfono/PC tenga la fecha y hora
   correctas, preferentemente automáticas, y pulsa **Usar hora de este teléfono/PC**.

El navegador entrega su hora al Wemos y este vuelve a intentar consultar
Nightscout. La verificación del certificado HTTPS permanece activa. La hora
se muestra en UTC; una diferencia respecto a tu hora local es normal.
El Wemos no tiene un reloj con batería: si NTP sigue bloqueado, tendrás que
repetir este paso después de reiniciarlo. No es necesario abrir puertos entrantes
en el router. Esta opción requiere el firmware 0.3.1; puedes actualizar por
`/update` aunque todavía no se haya sincronizado la hora.

### Si aparece Sin Wi-Fi

Desde 0.4.0 la pantalla añade el motivo de la última desconexión, por ejemplo
`Sin Wi-Fi (200)`. El firmware desactiva el ahorro de energía del Wi-Fi y añade
reintentos de reconexión cada 15 segundos después de una conexión correcta.
Esto no demuestra la causa de una desconexión en una placa concreta.
Cuando reconecte, abre `/diagnostics`: muestra señal, contador y motivo de
desconexiones, memoria libre, estado del reloj y último error TLS. Requiere
la misma autenticación de administración y no muestra contraseñas ni tokens.
También se registran estos eventos por USB a 115200 baudios. Algunos motivos:
200 = se perdió la señal periódica del router; 201 = no se encuentra el punto
de acceso; 202 = fallo de autenticación; 204 = se agotó el tiempo de negociación.

Al ajustar la hora, la web confirma **Hora ajustada**. Desde 0.4.1 se espera a
que la conexión del navegador se cierre y pasen al menos tres segundos desde
la última solicitud antes de consultar HTTPS. El arranque deja al menos cinco
segundos antes de la primera consulta. Así la respuesta de guardar o ajustar
la hora puede terminar antes de abrir la conexión al servidor.

### Si el formulario dice que ha caducado

El código de sesión de los formularios cambia cada vez que arranca el Wemos.
Una página abierta antes de un reinicio o una actualización ya no sirve para
guardar datos. Ese rechazo sucede **antes de validar la clave o el certificado**.
Abre de nuevo `http://IP_DEL_WEMOS/settings`, recarga completamente la página y vuelve
a pegar la clave; no uses Atrás para recuperar el formulario anterior. No se
desactiva esta protección ni se reenvía automáticamente una solicitud rechazada.

Desde 0.4.1 el aviso incluye un enlace a la página actualizada. El encabezado
de la web muestra un identificador de sesión y el tiempo encendido. Si sigue
ocurriendo con una página recién cargada, consulta `/diagnostics`: `boot_id` y
`uptime_seconds` permiten identificar reinicios; `reset_reason` muestra el motivo
de arranque, `last_operation` la última fase registrada y `free_stack` el margen
mínimo de pila registrado durante ese arranque. Un motivo Wi-Fi `0` solo indica
que aún no se ha registrado una causa de desconexión.

La versión 0.4.1 también separa el análisis JSON de la conexión TLS y pasa el
documento JSON a memoria dinámica acotada. Esto reduce la reserva de pila durante
la conexión; no demuestra por sí solo cuál fue la causa en una placa concreta.

## Verificación

```bash
python3 -m unittest discover -s tests -v
.venv/bin/pio run -e d1_mini
```

Si está disponible `g++`, también ejecutan diez grupos de pruebas nativas de
validación de ajustes, migración del formato anterior y conservación de claves
al cambiar la URL. Otros seis grupos comprueban el inicio de NTP después del
Wi-Fi, los reintentos, la reconexión, el desbordamiento de `millis()` y la
validación de la hora del navegador y de la fecha HTTP. Sin `g++`, esas pruebas se informan como omitidas.
Con `cryptography`, `make`, compiladores nativos y las fuentes de BearSSL del
núcleo ESP8266 instalado, otras dos pruebas verifican las 120 raíces con el
propio decodificador BearSSL y comprueban cadenas válidas, nombres incorrectos,
certificados vencidos, emisores no confiables y firmas manipuladas. Si faltan
esas herramientas, las pruebas TLS se informan como omitidas. Dos pruebas más
hacen conexiones TLS locales entre BearSSL y OpenSSL: claves RSA/EC correctas
aceptadas sin reloj, claves distintas rechazadas y exportación que rechaza
certificados no confiables, vencidos o de otro nombre. Cubren P-256, P-384 y P-521,
incluida la reparación del marcador de punto EC que omite el decodificador del
núcleo 3.1.2. Se añaden seis grupos nativos para historial acotado, muestras de
un minuto, antigüedad, huecos y lectura incremental de JSON, y una prueba que
comprueba que los recursos gzip del firmware coincidan con sus fuentes. Otras dos
pruebas cubren el cuerpo HTTP (fragmentación, bloques, truncamiento y límites) y
el almacenamiento del nombre, la validación UTF-8 y los offsets anteriores. En la
nube se usa el Python del venv para las pruebas completas. El estado de
bienvenida cubre conexión, errores HTTP, JSON rechazado, respuesta vacía y el
paso definitivo al visor después de la primera lectura válida.
La compilación verifica las bibliotecas para ESP8266; la prueba física de SPI,
Wi-Fi, configuración desde el teléfono, persistencia en flash y OTA debe hacerse
con el Wemos conectado.

Después de modificar `web/`, ejecuta `python3 tools/embed_web_assets.py` y guarda
`include/generated/web_assets.h` antes de compilar.

Las pruebas de apariencia verifican los offsets anteriores de EEPROM, cambios
parciales, rechazo atómico de valores inválidos y contraste después de convertir
los colores al formato de la pantalla. Las pruebas de navegador interceptan
solicitudes con recursos locales, sin iniciar un servidor de previsualización:

```bash
CHROMIUM_PATH=/usr/bin/chromium node tests/test_web_views.js
CHROMIUM_PATH=/usr/bin/chromium node tests/test_setup_views.js
CHROMIUM_PATH=/usr/bin/chromium node tests/test_night_views.js
```

Cubren los tres modos y cinco temas en escritorio/móvil, tendencias simples y
dobles, avisos, colores personalizados, sesión caducada, nombres como texto y
conservación de la lectura al perder contacto. Las capturas usan datos de ejemplo;
la grabación real en EEPROM, el servidor HTTP del Wemos y el panel SPI requieren
comprobación en hardware.

El asistente se prueba con HTML producido por las funciones C++ reales y los
recursos web locales: validaciones, regreso entre pasos, Enter, resumen sin
secretos, CSRF, respuestas de error, guardado único, confirmación y uso sin
JavaScript. No utiliza un servidor de simulación.

Las pruebas nocturnas cubren la extensión EEPROM sin modificar los bytes previos,
validación atómica, límites de horario y cruce de medianoche, ausencia de reloj,
transiciones de verano/invierno de Santiago y Madrid, intensidad del 100 % sin
cambiar ningún color RGB565 y restauración de la prueba al desbordarse millis().
El navegador verifica el formulario C++ real, controles, validaciones, payload
CSRF, errores y desconexión, bloqueo de doble prueba y guardado nativo.
