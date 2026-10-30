# Firmware para Wemos D1 mini ESP8266

**Español** · [English](README.en.md)

**Versión 0.8.1** · **4 MB de flash** · pantalla **GC9A01 SPI 240 × 240**.
Este archivo sirve para ese hardware y el cableado del proyecto; no es un binario ESP32.

| Archivo | Uso |
| --- | --- |
| [nightscout-d1-mini-v0.8.1.bin](nightscout-d1-mini-v0.8.1.bin) | Programa listo para cargar. En GitHub pulsa **Download raw file**. |
| [SHA256SUMS](SHA256SUMS) | Sumas para comprobar la descarga. |
| [build-info.json](build-info.json) | Placa, herramientas de compilación, memoria y validaciones del binario. |

**Para instalarlo sigue la [guía paso a paso](../docs/INSTALACION.md).** Incluye el
[diagrama nuevo](../docs/INSTALACION.md#2-conectar-la-pantalla), primera carga USB,
configuración desde el teléfono y solución de problemas. El
[README del proyecto](../README.md) presenta las funciones y capturas.

## Primera carga y actualizaciones

- **Primera vez:** por USB, dirección **`0x00000`**, modo **DIO**, memoria **4 MB**.
  La guía ofrece una herramienta con ventana en Windows y una alternativa por
  terminal para Windows, macOS y Linux.
- **Si ya funciona:** abre la web del Wemos y pulsa **Actualizar**. Selecciona el
  `.bin` en **Firmware**, no en FileSystem. [Ver los pasos](../docs/INSTALACION.md#7-actualizar-por-wi-fi).
- El archivo no contiene Wi-Fi, tokens ni contraseñas de usuarios. Los introduces
  en el asistente del primer arranque, entrando a la red `glucosa-…` con la clave
  que muestra la pantalla.
- Una actualización normal conserva los ajustes. Un borrado completo de flash
  los elimina. El proyecto no ofrece reversión automática de firmware.

## Qué incluye 0.8.1

Corrige la antigüedad en la pantalla física en inglés: ahora muestra
**2 min ago**, en lugar de **Age 2 min**. Una lectura antigua muestra
**OLD 10 min ago**. El español conserva **Hace 2 min** y **ANTIGUA 10 min**;
el visor web mantiene su formato anterior. El cambio se aplica a los tres modos.

El selector Español / English y todos los ajustes guardados se conservan.

Conserva las funciones anteriores: tres modos, gráfica de 30 minutos, nombre grande,
cinco temas, colores personalizados, bienvenida y configuración guiada. El horario
nocturno sigue disponible con intensidad de día/noche y prueba de cinco segundos;
viene desactivado. Los equipos anteriores comienzan en español hasta cambiarlo.

**Se atenúa el contenido; la luz de fondo mantiene su intensidad.** El módulo de
siete pines no tiene entrada BL/BLK. El visor web mantiene su intensidad normal.

Para HTTPS puedes usar los certificados públicos incluidos o una clave pública
fija. Consulta [HTTPS, hora y clave pública](../docs/INSTALACION.md#8-https-hora-y-clave-pública)
y las [herramientas con ventana](../tools/key-export/README.md).

## Identificación y comprobaciones

- Tamaño del archivo: **744.400 bytes**.
- SHA-256: `c057c01fc77944049626ad330141554b6745061870d57c11f516a83533f26587`.
- RAM estática: **38.012 bytes**, igual que 0.8.0.
- Compilación y pruebas nativas/de navegador registradas en `build-info.json`;
  la validación completa de esta versión en hardware físico sigue pendiente.

El [historial](../docs/HISTORIAL.md) conserva las notas de implementación.
Para compilar desde el código o revisar diagnóstico y seguridad, utiliza la
[referencia técnica](../docs/REFERENCIA-TECNICA.md).
