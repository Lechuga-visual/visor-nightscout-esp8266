#!/usr/bin/env python3
"""Build the downloadable, self-contained key launchers deterministically."""
import hashlib
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[1]
DIRECTORY=ROOT/'tools/key-export'
WINDOWS_HELP='''CLAVE PUBLICA DE NIGHTSCOUT - WINDOWS

1. Extrae este ZIP y abre Obtener-clave-Nightscout.cmd con doble clic.
2. Pega la URL completa, por ejemplo https://tu-nightscout.example/
3. Pulsa Obtener clave. Se guarda un TXT en el Escritorio.
4. Pulsa Copiar clave y pega todo en el campo PEM de Seguridad HTTPS del Wemos.

No necesita instalar Python ni OpenSSL. No pide token ni contrasena.
Si el certificado no se puede verificar, no exporta una clave.
No reemplaza archivos existentes y no cambia certificados de confianza.
Windows 10/11. Si no existe el Escritorio, usa la carpeta de usuario.

Capturas e instrucciones:
https://github.com/Lechuga-visual/nightscout-esp32/tree/main/tools/key-export
'''
UNIX_HELP='''CLAVE PUBLICA DE NIGHTSCOUT - LINUX Y MACOS

macOS: extrae el ZIP y abre Obtener-clave-Nightscout.command con doble clic.
Linux: desde la carpeta descargada, ejecuta:
    bash obtener-clave-nightscout.sh

Pega la URL HTTPS completa, sin token ni contrasena. Se guarda un TXT en el
Escritorio y se abre para copiar su contenido al campo PEM del Wemos.

macOS utiliza las ventanas y certificados del sistema. No necesita Python.
Linux utiliza Zenity, KDialog o Python 3 con Tk para mostrar una ventana.
Si no hay un entorno grafico compatible, pide la URL en la terminal.
Necesita OpenSSL (incluido en macOS y habitualmente en Linux).

Si Finder no abre el .command, utiliza la alternativa con bash de arriba.
No se omite la verificacion del certificado y no se reemplazan archivos.

Capturas e instrucciones:
https://github.com/Lechuga-visual/nightscout-esp32/tree/main/tools/key-export
'''


def archive(name, entries):
    with zipfile.ZipFile(DIRECTORY/name,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as output:
        for filename,data,mode in entries:
            info=zipfile.ZipInfo(filename,date_time=(2026,1,1,0,0,0))
            info.create_system=3
            info.external_attr=(0o100000|mode)<<16
            info.compress_type=zipfile.ZIP_DEFLATED
            output.writestr(info,data)


def build():
    windows=(DIRECTORY/'Obtener-clave-Nightscout.cmd').read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
    unix=(DIRECTORY/'obtener-clave-nightscout.sh').read_bytes()
    command=DIRECTORY/'Obtener-clave-Nightscout.command'
    command.write_bytes(unix);command.chmod(0o755)
    archive('clave-nightscout-windows.zip',[('Obtener-clave-Nightscout.cmd',windows,0o644),('LEEME.txt',WINDOWS_HELP.replace('\n','\r\n').encode(),0o644)])
    archive('clave-nightscout-linux-macos.zip',[('obtener-clave-nightscout.sh',unix,0o755),('Obtener-clave-Nightscout.command',unix,0o755),('LEEME.txt',UNIX_HELP.encode(),0o644)])
    names=['obtener-clave-nightscout.sh','Obtener-clave-Nightscout.command','clave-nightscout-windows.zip','clave-nightscout-linux-macos.zip']
    (DIRECTORY/'SHA256SUMS').write_text(''.join(hashlib.sha256((DIRECTORY/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in names))


if __name__=='__main__':
    build();print('Packaged Windows and Linux/macOS key tools')
