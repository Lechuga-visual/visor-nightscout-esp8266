# Historial de implementación

**Español** · [English](HISTORY.en.md)

Notas conservadas al reorganizar la documentación. Describen lo que cambió y las
comprobaciones realizadas **al publicar cada versión**; no son una guía de instalación
ni una declaración de validación física de todas las versiones.

La descarga vigente es **0.8.0**. Consulta la [guía de instalación](INSTALACION.md),
la [referencia técnica](REFERENCIA-TECNICA.md) y la [hoja de ruta](../ROADMAP.md).

## Español e inglés en 0.8.0

Un solo firmware permite elegir Español / English en el inicio y en Ajustes,
para pantalla, bienvenida, configuración, visor, avisos y actualización web.
El idioma se guarda en una extensión de ocho bytes en el offset 3872, sin mover
los datos anteriores. Equipos existentes comienzan en español; no cambia unidades,
zona horaria ni credenciales. El selector conserva autenticación y CSRF, y funciona
sin JavaScript. Al guardar recarga la página: elige antes de llenar otros campos.

Textos y recursos traducidos en flash; historial de siete puntos y 30 minutos.
Documentación, capturas y herramientas de clave pública disponibles en inglés.
La validación física completa continúa pendiente.

## Brillo y horario nocturno en 0.7.1

En **Ajustes → Brillo y horario nocturno** puedes activar un horario, elegir
su inicio y fin, la zona horaria y la intensidad de día y de noche entre 10 y
100 %. Por defecto queda desactivado, con día al 100 %, noche al 25 % y horario
22:00–07:00 en Santiago. Pulsa **Guardar horario** para aplicarlo y conservarlo
al reiniciar. Con el horario desactivado se utiliza la intensidad de día, lo que
permite ajustarla manualmente.

**Se atenúa el contenido dibujado, no la luz de fondo.** El módulo de siete pines
no tiene entrada BL/BLK. Número, flecha, textos, gráfica y fondo se escalan en
RGB565, manteniendo sus colores. El visor web conserva su intensidad habitual.

**Probar noche durante 5 segundos** aplica temporalmente la intensidad nocturna
sin guardar ni escribir en flash. Se restaura sola aunque cierres el navegador.
El horario puede cruzar medianoche; el inicio se incluye y el fin se excluye.
La zona horaria se guarda y Santiago/Madrid ajustan el horario de verano.
Sin hora sincronizada se utiliza la intensidad de día, con un aviso en Ajustes.
En el modo de clave pública fija se sigue tomando la hora del HTTPS autenticado,
sin necesitar NTP. La configuración inicial permanece al 100 % para leer su clave.

Los ajustes ocupan una extensión etiquetada de 16 bytes desde el offset 3856 de
EEPROM. No cambia el formato de Wi-Fi, URL, token, clave HTTPS, nombre o apariencia.
Guardar y probar requieren autenticación de administración y el nonce CSRF vigente.
Las actualizaciones de intensidad no reservan un framebuffer ni cambian las
consultas a Nightscout.

[Captura móvil](../docs/images/horario-nocturno-movil.png) ·
[Captura web](../docs/images/horario-nocturno-web.png).
Son capturas del formulario real con valores de ejemplo; la atenuación y el
guardado en la placa requieren comprobación física.

## Bienvenida y configuración B en 0.7.0

Al encender, el Wemos muestra tu nombre y las etapas **Wi-Fi → servidor → primera
lectura**, con el tema guardado. Los indicadores avanzan según la conexión y la
respuesta real; un error conserva su mensaje y la IP para abrir Ajustes. Al recibir
la primera lectura válida aparece el visor habitual. Un fallo posterior mantiene
ese visor con sus avisos, sin volver a la bienvenida.

Si necesita configuración, la pantalla muestra tres instrucciones numeradas:
conectar a la red temporal, usar su clave y abrir `http://192.168.4.1/`.

El formulario inicial y `/configure` ahora tienen tres pasos:

1. **Tu Wi-Fi:** red de 2,4 GHz y su contraseña.
2. **Tu Nightscout:** nombre, URL base, token opcional y opciones HTTPS.
3. **Tu acceso:** usuario, contraseña de administración y resumen.

Volver atrás conserva los campos durante esa visita. Solo **Guardar y reiniciar**
en el último paso envía los ajustes; se bloquea la doble pulsación. Los errores
se muestran sin borrar lo escrito y el resumen oculta contraseñas y token. Sin
JavaScript se ofrecen los tres grupos juntos y un único guardado final.
La autenticación, el nonce CSRF y las validaciones del dispositivo se conservan.

[Captura móvil: Wi-Fi](../docs/images/configuracion-wifi-movil.png) ·
[Captura móvil: Nightscout](../docs/images/configuracion-nightscout-movil.png) ·
[Captura: revisión final](../docs/images/configuracion-resumen.png).
Las capturas muestran datos de ejemplo generados por el constructor HTML del
firmware en una prueba local; la conexión y el reinicio reales requieren la placa.

Actualizar normalmente conserva Wi-Fi, token, clave HTTPS, nombre y apariencia.
El horario nocturno se incorpora desde 0.7.1; los ajustes anteriores se conservan.

## Tres modos de visor y colores en 0.6.0

En el panel web, usa **Modo del visor** para elegir:

- **Con gráfica:** lectura y últimos 30 minutos.
- **Número · flecha lateral:** lectura grande con la flecha a la derecha.
- **Número · flecha debajo:** lectura centrada, con la flecha junto a la unidad.

El cambio se guarda en el Wemos y se aplica a la pantalla y al visor web.
La gráfica conserva siete puntos y las consultas siguen cada 30 segundos.
Cambiar de modo no borra el historial ni fuerza una consulta a Nightscout.
Desde 0.6.1, el nombre duplica su tamaño en los tres modos: fuente de 32 píxeles
en la pantalla y de 28 en el visor web. Los nombres largos se acortan con puntos
para mantener esa letra grande; el nombre completo guardado no cambia.
Se conserva la posición del nombre y del resto de los elementos. La lectura adapta su tamaño para cuatro
cifras. Las flechas web son trazos SVG, incluidas las tendencias dobles.

En **Ajustes → Apariencia** puedes elegir **menta, rojo, azul, rosado, celeste**
o **Personalizado**. Esta última opción permite cambiar acento/flecha, fondo
del visor y número con una vista previa. El dispositivo comprueba el contraste
antes de guardar. Los avisos de lectura baja, alta o antigua mantienen texto
y colores propios. Se conservan los colores personalizados al elegir otro tema.

La compilación 0.7.1 usa 38.084 bytes de RAM estática: 272 bytes más que 0.7.0.
Los dibujos se hacen directamente, sin una imagen completa de pantalla en RAM.
La apariencia se guarda en una extensión independiente de 20 bytes, después
del nombre, sin cambiar el formato del Wi-Fi, servidor, token o clave PEM.
Al actualizar desde 0.5.x comienza con gráfica y menta; el nombre y los ajustes
anteriores se conservan. El selector requiere la sesión vigente del Wemos.
El estado local devuelve el nonce CSRF de esa sesión para guardar cambios;
no devuelve el token de acceso a Nightscout ni contraseñas.

![Visor con número grande y flecha lateral; datos de ejemplo](../docs/images/visor-lateral.png)

[Ver el modo centrado en un teléfono](../docs/images/visor-centrado-movil.png) ·
[Ver el modo con gráfica](../docs/images/visor-grafica.png).

## Menos memoria y gráfica de 30 minutos en 0.5.2

La gráfica de pantalla y web cubre los últimos 30 minutos. La consulta baja de
145 a 31 entradas y el historial conserva hasta siete puntos de cinco minutos.
Los textos fijos de respuestas web, diagnóstico, mensajes y registro se guardan
en flash. La RAM estática de la compilación baja de 40.720 a 37.632 bytes:
**3.088 bytes menos**. Además, el documento temporal del visor baja de 3.072 a
1.792 bytes y el historial temporal necesita menos memoria durante HTTPS.
Esto ahorra RAM; la imagen de firmware no tiene por qué ocupar menos flash.

El diagnóstico de 0.5.1 mostró HTTP 200 con un cuerpo incompleto y solo 1.960
bytes libres al empezar el JSON. La autenticación por clave pública funcionaba.
La nueva consulta reduce la transferencia; si quedan menos de 3.072 bytes al
empezar el análisis, se informa memoria baja y se conserva el dato anterior.
Ese margen evita iniciar nuevas reservas con tan poca RAM, pero no garantiza
por sí solo resolver la excepción anterior: debe confirmarse en el dispositivo.
Se conservan el nombre, Wi-Fi, URL, token, CA o clave pública y datos de acceso.

## Corrección de lectura en 0.5.1

El cuerpo HTTP ahora se decodifica antes del JSON: admite `Content-Length`,
cierre de conexión y `Transfer-Encoding: chunked`, con un búfer fijo de 512
bytes. Pide contenido sin compresión y rechaza cuerpos incompletos, bloques
malformados, respuestas demasiado grandes y datos posteriores al JSON antes
de actualizar la lectura. La respuesta completa no se almacena en RAM.

Si falla el análisis del historial, las siguientes consultas utilizan la ruta
v1 de última lectura (`entries.json?count=1`), conservando los puntos ya
obtenidos y marcando el historial como parcial. Se reintenta el historial
completo después de cinco minutos. Una respuesta vacía sigue significando
sin lecturas; un error nunca se presenta como un dato nuevo.

`/diagnostics` añade estado HTTP, tamaño, codificación, bytes decodificados,
primer byte numérico, fase del análisis, error ArduinoJson, número de entradas
y memoria antes del JSON. No incluye valores de glucosa, tokens ni claves.
`reset_info` permite investigar el arranque anterior por excepción.
Se ha eliminado el servidor de pruebas, la previsualización y sus controles.
La respuesta real consultada es JSON válido y pasa el lector nativo; aún debe
comprobarse la corrección en el ESP8266 para confirmar la causa del fallo.
