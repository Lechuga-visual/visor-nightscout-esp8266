# Hoja de ruta

La meta es que una persona pueda armar o recibir el dispositivo, configurarlo
sin conocimientos de programación y consultar su Nightscout de forma clara y
confiable. Queremos evolucionar desde un visor local sencillo hacia un dispositivo
con más capacidad e integración, manteniendo el control de los datos en sus usuarios.

Esta es una dirección de trabajo, **no una lista de funciones ya disponibles ni
un calendario comprometido**. Las prioridades se ajustarán con pruebas físicas,
necesidades reales y las posibilidades de cada placa.

## Punto de partida: lo que ya existe

El firmware **0.7.1** utiliza Wemos D1 mini **ESP8266** y pantalla GC9A01.
Incluye visor físico y web, tres modos, gráfica de 30 minutos, nombre, temas y
colores personalizados, configuración guiada, HTTPS, actualización por Wi-Fi y
atenuación del contenido por horario. Las funciones y sus límites se describen
en el [README](README.md#funcionalidades-actuales).

La capacidad del ESP8266 condiciona el historial y las tareas simultáneas. El
siguiente paso es consolidar esta base y preparar una plataforma con más margen,
sin presentar un cambio de placa como solución automática a todos los problemas.

## 1. Consolidar la experiencia actual

**Objetivo:** que instalar, recuperar y mantener el equipo sea predecible.

- Completar la comprobación física de 0.7.1: primer arranque, guardado, pantalla,
  horario nocturno y actualizaciones que conservan la configuración.
- Hacer pruebas prolongadas con Wi-Fi débil, cortes de Internet, servidor sin
  lecturas, errores de autenticación y respuestas grandes o incompletas.
- Afinar los mensajes y la recuperación de acceso para distinguir un problema
  de Wi-Fi, de hora, de permisos o del servidor.
- Mantener la guía, el cableado y las capturas al día; recoger dudas de personas
  que no programan y usarlas para mejorar el asistente.

**Cómo sabremos que está listo:** una persona puede seguir la guía con el equipo
real, ver una lectura y recuperar la conexión sin recompilar; las interrupciones
no hacen pasar un dato antiguo por uno reciente.

## 2. Llevar el visor a ESP32

**Objetivo:** disponer de más memoria y capacidad para ampliar funciones sin
sobrecargar la lectura ni la interfaz.

| Trabajo previsto | Resultado esperado |
| --- | --- |
| Elegir una placa de referencia | Documentar modelo, memoria, alimentación, disponibilidad y compatibilidad real con la pantalla; valorar una variante con PSRAM si aporta una ventaja comprobable. |
| Crear una compilación ESP32 independiente | Mantener el destino ESP8266 identificado y separar los binarios para evitar cargas en una placa equivocada. |
| Adaptar pantalla, almacenamiento y Wi-Fi | Definir un cableado propio para ESP32; los nombres D0/D1 del Wemos no se trasladan directamente. |
| Medir memoria y tiempos | Comprobar que HTTPS, el servidor local y el dibujo pueden funcionar juntos de forma estable. |
| Diseñar una migración de ajustes | Facilitar el cambio de dispositivo sin publicar ni incluir contraseñas o tokens en archivos de ejemplo. |

Todavía no hay placa ESP32 elegida ni un firmware ESP32 listo para descargar.
Se priorizará conservar la experiencia del visor y después ampliar funciones.
La continuidad y el alcance de mantenimiento del ESP8266 se definirán según las
pruebas; las mejoras que excedan su memoria podrán ser exclusivas de ESP32.

**Cómo sabremos que está listo:** hay binario identificado, diagrama propio, guía
probada y validación en la placa elegida, incluyendo recuperación por USB.

## 3. Aprovechar la nueva capacidad

Estas mejoras se estudiarán después de medir la versión ESP32:

- Historial más amplio y detallado, con límites de memoria y una indicación clara
  de huecos o datos incompletos. Si se guarda de forma permanente, definir cuánto
  se conserva, cómo se borra y el efecto de las escrituras sobre la memoria.
- Más opciones de lectura: unidades mmol/L, intervalos visuales configurables,
  accesibilidad, idiomas y tamaños de letra.
- **Brillo real de la luz de fondo**, mediante una pantalla con entrada BL/BLK
  y un circuito apropiado. Requiere otro cableado y comprobar sus requisitos
  eléctricos; el módulo actual de siete pines solo atenúa el contenido.
- Sincronización de hora y mantenimiento de HTTPS más sencillos. Investigar cómo
  reducir pasos con seguridad, manteniendo la validación del servidor. La gestión
  automática de claves queda para una etapa posterior.
- Actualizaciones más fáciles de identificar y recuperar: estudiar imágenes
  firmadas y reversión a una versión funcional cuando el hardware lo permita.

No se fijan todavía duración del historial, pantallas adicionales ni prestaciones
que no hayan sido medidas en el nuevo hardware.

## 4. Integración a mayor nivel

**Objetivo:** permitir que el dispositivo participe en un sistema más amplio,
con conexiones documentadas y permisos claros.

| Línea de exploración | Qué habría que definir antes de implementarla |
| --- | --- |
| API local estable | Versiones, autenticación, campos y frecuencia de consulta para que otras aplicaciones lean el estado del visor. El `/api/state` actual es una ruta interna, sin ese compromiso de estabilidad. |
| Domótica y otros paneles | Evaluar MQTT o Home Assistant con acceso explícito, configuración guiada y control de quién recibe los datos. |
| Varios dispositivos o personas | Separar identidades, servidores y permisos; decidir si se resuelve en cada visor o mediante un componente adicional. |
| Notificaciones opcionales | Definir eventos, destino, retrasos, desconexiones y consentimiento. Una notificación informativa no se presentará como una alarma clínica garantizada. |
| Acceso remoto seguro | Evaluar una conexión protegida o un servicio complementario, sin exponer directamente la administración HTTP del Wemos a Internet. |
| Más compatibilidad con Nightscout | Documentar versiones y permisos admitidos, y estudiar nuevas rutas de API sin perder las comprobaciones de antigüedad. |

Estas integraciones **no están implementadas**. Su diseño debe evitar compartir
datos por defecto y utilizar permisos de solo lectura cuando sean suficientes.
El proyecto sigue orientado a visualización y acompañamiento; el control de
insulina y las decisiones automáticas de tratamiento quedan fuera de este plan.

## Cómo proponer un cambio

Puedes [abrir una propuesta en GitHub](https://github.com/Lechuga-visual/visor-nightscout-esp8266/issues)
con lo que necesitas, en qué situación lo usarías y qué placa o pantalla tienes.
No publiques contraseñas, tokens ni datos personales de salud. Antes de ampliar
funciones, buscaremos una propuesta visual o técnica que se pueda revisar y una
forma de comprobarla en hardware.

[Volver al proyecto](README.md) · [Instalar la versión actual](docs/INSTALACION.md) ·
[Consultar el historial](docs/HISTORIAL.md)
