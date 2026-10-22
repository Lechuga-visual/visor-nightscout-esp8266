# Obtener la clave pública de tu Nightscout

Estas herramientas piden **una URL**, verifican la identidad del sitio y guardan
un archivo `.txt` con su clave pública. No necesitan el token, no consultan
lecturas y no cambian el firmware ni los ajustes del Wemos.

## Windows 10 y 11

Descarga [Obtener-clave-Nightscout.cmd](Obtener-clave-Nightscout.cmd) usando
**Download raw file** de GitHub. Haz doble clic, pega la dirección completa
(por ejemplo `https://tu-nightscout.example/`) y pulsa **Obtener clave**.
No necesitas instalar Python ni OpenSSL: usa Windows PowerShell y los
certificados de confianza del sistema.

El archivo se guarda en el Escritorio y se abre una ventana con **Copiar clave**.
Si el Escritorio no existe, se utiliza tu carpeta de usuario. También puedes
abrir el `.txt` con el botón **Abrir .txt**. Cada extracción crea un archivo nuevo.

## Linux y macOS

Descarga [obtener-clave-nightscout.sh](obtener-clave-nightscout.sh). En una terminal,
desde la carpeta donde lo guardaste, ejecuta:

```bash
bash obtener-clave-nightscout.sh
```

Pega la URL en la ventana y confirma. El `.txt` se guarda en el Escritorio y
se abre para copiarlo. En Linux se utiliza el Escritorio configurado por XDG;
si no está disponible, se guarda en tu carpeta de usuario.

macOS usa sus ventanas y el llavero del sistema. Linux usa Zenity, KDialog o,
como alternativa, Python 3 con Tk. Basta con tener una de esas opciones para
mostrar la ventana; si no hay ninguna, pide la URL en la terminal. OpenSSL debe
estar instalado (macOS ya incluye una versión del sistema).

## Pegarla en el Wemos

1. Abre **Ajustes → Seguridad HTTPS y opción sin NTP**.
2. Pega **todo** el contenido del `.txt` en el campo PEM, incluyendo
   `-----BEGIN PUBLIC KEY-----` y `-----END PUBLIC KEY-----`.
3. Guarda el servidor. No marques la opción de borrar la clave.

El `.txt` contiene solamente la clave pública, sin instrucciones adicionales,
contraseñas ni claves privadas. Se guarda como `clave-nightscout-sitio-fecha.txt`;
los archivos anteriores se conservan.

## Si no puede obtener la clave

Comprueba Internet, la fecha/hora del equipo y que la dirección empiece por
`https://`. Usa la URL del sitio sin usuario, contraseña, token ni parámetros.
Si el certificado está vencido, no es confiable o no corresponde al sitio, la
herramienta se detiene y no exporta una clave. No desactiva esa verificación.
La clave fija seguirá sirviendo mientras el servidor conserve la misma clave,
aunque renueve su certificado; si cambia de clave, repite la extracción.

## Uso por terminal y pruebas

La versión Unix admite, opcionalmente:

```bash
bash obtener-clave-nightscout.sh --url https://tu-nightscout.example/ --output-dir . --headless --no-open
```

`--ca-file archivo.pem` permite indicar una CA privada de confianza para Unix,
como en la herramienta Python original. En macOS se valida la cadena capturada
con la política SSL del llavero y el nombre del sitio antes de exportar la clave.
No se instala esa CA en el sistema.

Las pruebas de TLS local comparan el PEM con la clave esperada para RSA y
P-256/P-384/P-521, rechazan certificados vencidos/no confiables/de otro nombre,
URLs con secretos y puertos inválidos, y comprueban que no se sobreescriban archivos.
Las pruebas de Windows utilizan raíces temporales exclusivamente en su entorno
de pruebas y las retiran al terminar; la herramienta descargable no modifica
el almacén de certificados.
