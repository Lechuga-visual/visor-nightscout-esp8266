#!/usr/bin/env bash
# Verified public-key export. No Python/cryptography installation is required.
set -u
umask 077
URL=''; OUTPUT_DIR=''; CA_FILE=''; HEADLESS=0; NO_OPEN=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --url|--output-dir|--ca-file) [ "$#" -ge 2 ] || { echo 'Falta el valor de la opción.' >&2; exit 2; }
      if [ "$1" = '--url' ]; then URL=$2; elif [ "$1" = '--ca-file' ]; then CA_FILE=$2; else OUTPUT_DIR=$2; fi; shift 2 ;;
    --headless) HEADLESS=1; shift ;;
    --no-open) NO_OPEN=1; shift ;;
    --help) echo 'Uso: bash obtener-clave-nightscout.sh [--url https://sitio/] [--output-dir carpeta] [--headless] [--no-open]'; exit 0 ;;
    *) echo 'Opción no reconocida. Usa --help.' >&2; exit 2 ;;
  esac
done
KEY_OPENSSL=${NIGHTSCOUT_OPENSSL:-openssl}
OS=$(uname -s)
DIALOG=terminal
if [ "$OS" = Darwin ] && command -v osascript >/dev/null 2>&1; then DIALOG=mac
elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
  if command -v zenity >/dev/null 2>&1; then DIALOG=zenity
  elif command -v kdialog >/dev/null 2>&1; then DIALOG=kdialog
  elif command -v python3 >/dev/null 2>&1 && python3 -c 'import tkinter' >/dev/null 2>&1; then DIALOG=tk
  fi
fi
message() {
  [ "$HEADLESS" -eq 0 ] || return 0
  case "$DIALOG" in
    mac) osascript - "$1" <<'APPLESCRIPT' >/dev/null 2>&1
on run argv
  display alert "Clave pública de Nightscout" message (item 1 of argv) as warning buttons {"Aceptar"}
end run
APPLESCRIPT
      ;;
    zenity) zenity --error --title='Clave pública de Nightscout' --text="$1" >/dev/null 2>&1 ;;
    kdialog) kdialog --title 'Clave pública de Nightscout' --error "$1" >/dev/null 2>&1 ;;
    tk) python3 - "$1" <<'PY' >/dev/null 2>&1
import sys, tkinter as tk
from tkinter import messagebox
root=tk.Tk();root.withdraw()
messagebox.showerror('Clave pública de Nightscout',sys.argv[1],parent=root)
root.destroy()
PY
      ;;
  esac
}
fail() {
  if [ "${NIGHTSCOUT_KEY_DEBUG:-0}" = 1 ] && [ -n "${WORK:-}" ]; then
    [ ! -f "$WORK/tls.log" ] || cat "$WORK/tls.log" >&2
    [ ! -f "$WORK/verify.log" ] || cat "$WORK/verify.log" >&2
  fi
  printf '%s\n' "$1" >&2; message "$1" || :; exit 1; }
if [ -z "$URL" ]; then
  [ "$HEADLESS" -eq 0 ] || fail 'Falta --url con la dirección HTTPS del sitio.'
  case "$DIALOG" in
    mac) URL=$(osascript - <<'APPLESCRIPT'
set answer to display dialog "Pega la URL HTTPS completa de tu Nightscout, sin token ni contraseña. Guardaremos la clave verificada en un .txt en el Escritorio." default answer "https://" with title "Clave pública de Nightscout" buttons {"Cancelar", "Obtener clave"} default button "Obtener clave"
text returned of answer
APPLESCRIPT
      ) || exit 0 ;;
    zenity) URL=$(zenity --entry --width=540 --title='Clave pública de Nightscout' --text='Pega la URL HTTPS completa, sin token ni contraseña.\nGuardaremos un .txt en el Escritorio para copiar al Wemos.' --entry-text='https://') || exit 0 ;;
    kdialog) URL=$(kdialog --title 'Clave pública de Nightscout' --inputbox 'Pega la URL HTTPS completa, sin token ni contraseña. Se guardará un .txt en el Escritorio.' 'https://') || exit 0 ;;
    tk) URL=$(python3 - <<'PY'
import tkinter as tk
from tkinter import simpledialog
root=tk.Tk();root.withdraw()
value=simpledialog.askstring('Clave pública de Nightscout','Pega la URL HTTPS completa, sin token ni contraseña.\nGuardaremos un .txt en el Escritorio para copiar al Wemos.',initialvalue='https://',parent=root)
root.destroy()
if value is None: raise SystemExit(1)
print(value)
PY
      ) || exit 0 ;;
    *) printf 'URL HTTPS completa de tu Nightscout: ' >&2; IFS= read -r URL || exit 0 ;;
  esac
fi
# Parse only an HTTPS authority. Paths are allowed and ignored; secrets never are.
case "$URL" in https://*) ;; *) fail 'Usa una URL completa que comience por https://.' ;; esac
case "$URL" in *'?'*|*'#'*|*'@'*|*'\'*|*[[:space:]]*) fail 'Usa una URL sin usuario, contraseña, token, parámetros ni espacios.' ;; esac
AUTHORITY=${URL#https://}; AUTHORITY=${AUTHORITY%%/*}; HOST=''; PORT=443; VERIFY_FLAG=-verify_hostname
if [[ "$AUTHORITY" =~ ^\[([0-9a-fA-F:]+)\](:([0-9]+))?$ ]]; then
  HOST=${BASH_REMATCH[1]}; [ -z "${BASH_REMATCH[3]}" ] || PORT=${BASH_REMATCH[3]}; ADDRESS="[$HOST]"; VERIFY_FLAG=-verify_ip
elif [[ "$AUTHORITY" =~ ^([a-zA-Z0-9][a-zA-Z0-9.-]*)(:([0-9]+))?$ ]]; then
  HOST=${BASH_REMATCH[1]}; [ -z "${BASH_REMATCH[3]}" ] || PORT=${BASH_REMATCH[3]}; HOST=${HOST%.}; ADDRESS=$HOST
  [[ "$HOST" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] && VERIFY_FLAG=-verify_ip
else fail 'La dirección del sitio no es válida. Ejemplo: https://tu-nightscout.example/'; fi
[ "${#PORT}" -le 5 ] || fail 'Puerto no válido.'
PORT=$((10#$PORT)); [ "$PORT" -ge 1 ] && [ "$PORT" -le 65535 ] || fail 'Puerto no válido.'
[ -z "$CA_FILE" ] || [ -r "$CA_FILE" ] || fail 'No se pudo leer la CA privada indicada.'
command -v "$KEY_OPENSSL" >/dev/null 2>&1 || fail 'No se encontró OpenSSL. Instala el paquete openssl de tu sistema y vuelve a abrir esta herramienta.'
WORK=$(mktemp -d "${TMPDIR:-/tmp}/nightscout-key.XXXXXXXX") || fail 'No se pudo crear una carpeta temporal.'
PROCESS=''; WATCHDOG=''
cleanup() {
  [ -z "$WATCHDOG" ] || { kill "$WATCHDOG" 2>/dev/null || :; wait "$WATCHDOG" 2>/dev/null || :; }
  [ -z "$PROCESS" ] || { kill "$PROCESS" 2>/dev/null || :; wait "$PROCESS" 2>/dev/null || :; }
  rm -rf "$WORK"
}
trap cleanup EXIT
trap 'exit 130' INT TERM
if [ "$OS" = Darwin ]; then
  # Apple's system OpenSSL/LibreSSL may lack hostname verification. Authenticate
  # the SAME captured chain with the macOS SSL policy and system keychain below.
  if [ -n "$CA_FILE" ]; then ROOT_ARGS=( -CAfile "$CA_FILE" )
  else
    /usr/bin/security find-certificate -a -p /System/Library/Keychains/SystemRootCertificates.keychain >"$WORK/mac-roots.pem" 2>/dev/null || fail 'No se pudieron leer las autoridades de confianza de macOS.'
    ROOT_ARGS=( -CAfile "$WORK/mac-roots.pem" )
  fi
  printf '' | "$KEY_OPENSSL" s_client -tls1_2 "${ROOT_ARGS[@]}" -connect "$ADDRESS:$PORT" -servername "$HOST" -showcerts >"$WORK/chain.log" 2>"$WORK/tls.log" &
else
  ROOT_ARGS=(); [ -z "$CA_FILE" ] || ROOT_ARGS=( -CAfile "$CA_FILE" )
  "$KEY_OPENSSL" s_client -tls1_2 "${ROOT_ARGS[@]}" -connect "$ADDRESS:$PORT" -servername "$HOST" -showcerts -verify_return_error "$VERIFY_FLAG" "$HOST" </dev/null >"$WORK/chain.log" 2>"$WORK/tls.log" &
fi
PROCESS=$!
(
  sleep 20 & SLEEPER=$!
  trap 'kill "$SLEEPER" 2>/dev/null || :; wait "$SLEEPER" 2>/dev/null || :; exit 0' TERM INT
  wait "$SLEEPER"
  kill "$PROCESS" 2>/dev/null || :
) >/dev/null 2>&1 & WATCHDOG=$!
if wait "$PROCESS"; then CONNECTED=1; else CONNECTED=0; fi
PROCESS=''; kill "$WATCHDOG" 2>/dev/null || :; wait "$WATCHDOG" 2>/dev/null || :; WATCHDOG=''
[ "$CONNECTED" -eq 1 ] || fail 'No se pudo verificar el sitio. Revisa la URL, Internet y la fecha y hora del equipo. No se guardó una clave.'
awk -v dir="$WORK" '/-----BEGIN CERTIFICATE-----/{n++;file=dir "/cert-" n ".pem"} file!=""{print >file} /-----END CERTIFICATE-----/{close(file);file=""}' "$WORK/chain.log"
[ -s "$WORK/cert-1.pem" ] || fail 'El sitio no entregó un certificado válido.'
"$KEY_OPENSSL" x509 -in "$WORK/cert-1.pem" -checkend 0 -noout >/dev/null 2>&1 || fail 'El certificado del sitio está vencido. No se guardó una clave.'
if [ "$OS" = Darwin ]; then
  NOT_BEFORE=$("$KEY_OPENSSL" x509 -in "$WORK/cert-1.pem" -startdate -noout | sed -n 's/^notBefore=//p')
  START_EPOCH=$(LC_ALL=C date -j -u -f '%b %e %T %Y %Z' "$NOT_BEFORE" '+%s' 2>/dev/null) || fail 'No se pudo validar la fecha del certificado.'
  [ "$START_EPOCH" -le "$(date -u '+%s')" ] || fail 'El certificado del sitio todavía no está vigente. Revisa el reloj del equipo.'
  # Leaf first, followed by intermediates; the final trust decision uses macOS roots.
  CERTS=(); for cert in "$WORK"/cert-*.pem; do CERTS+=( -c "$cert" ); done
  ROOT_ARGS=(); [ -z "$CA_FILE" ] || ROOT_ARGS=( -r "$CA_FILE" )
  /usr/bin/security verify-cert "${CERTS[@]}" "${ROOT_ARGS[@]}" -p ssl -s "$HOST" >"$WORK/verify.log" 2>&1 || fail 'macOS no pudo verificar el certificado del sitio. Revisa la URL y la fecha y hora. No se guardó una clave.'
fi
"$KEY_OPENSSL" x509 -in "$WORK/cert-1.pem" -pubkey -noout >"$WORK/public.pem" 2>"$WORK/key.log" || fail 'No se pudo leer la clave pública verificada.'
"$KEY_OPENSSL" asn1parse -in "$WORK/public.pem" >"$WORK/algorithm.txt" 2>>"$WORK/key.log" || fail 'La clave pública no es válida.'
if grep -Eq 'OBJECT[[:space:]]*:rsaEncryption[[:space:]]*$' "$WORK/algorithm.txt"; then :
elif grep -Eq 'OBJECT[[:space:]]*:id-ecPublicKey[[:space:]]*$' "$WORK/algorithm.txt" && grep -Eq 'OBJECT[[:space:]]*:(prime256v1|secp384r1|secp521r1)[[:space:]]*$' "$WORK/algorithm.txt"; then :
else fail 'La clave del sitio no es RSA o EC compatible con el ESP8266.'; fi
[ "$(wc -c <"$WORK/public.pem")" -le 3072 ] || fail 'La clave excede el espacio del ESP8266.'
if [ -z "$OUTPUT_DIR" ]; then
  if [ "$OS" = Linux ] && command -v xdg-user-dir >/dev/null 2>&1; then OUTPUT_DIR=$(xdg-user-dir DESKTOP); fi
  if [ -z "$OUTPUT_DIR" ] || [ ! -d "$OUTPUT_DIR" ]; then
    if [ -d "$HOME/Desktop" ]; then OUTPUT_DIR=$HOME/Desktop
    elif [ -d "$HOME/Escritorio" ]; then OUTPUT_DIR=$HOME/Escritorio
    else OUTPUT_DIR=$HOME; fi
  fi
fi
[ -d "$OUTPUT_DIR" ] && [ -w "$OUTPUT_DIR" ] || fail 'No se puede escribir en la carpeta de destino.'
SAFE=$(printf '%s' "$HOST" | tr -c 'a-zA-Z0-9._-' '_' | cut -c 1-60)
BASE="clave-nightscout-$SAFE-$(date +%Y%m%d-%H%M%S)"
DEST=''
for ((i=0;i<100;i++)); do
  SUFFIX=''; [ "$i" -eq 0 ] || SUFFIX="-$i"
  CANDIDATE="$OUTPUT_DIR/$BASE$SUFFIX.txt"
  if (set -o noclobber; exec 3>"$CANDIDATE" || exit 17
      cat "$WORK/public.pem" >&3 || { rm -f "$CANDIDATE"; exit 18; }) 2>/dev/null; then
    DEST=$CANDIDATE; break
  else
    RESULT=$?
    [ "$RESULT" -eq 17 ] && [ -e "$CANDIDATE" ] || fail 'No se pudo guardar el archivo de la clave.'
  fi
done
[ -n "$DEST" ] || fail 'No se pudo crear un archivo nuevo. No se reemplazó ninguna clave anterior.'
printf 'Clave pública verificada guardada en: %s\n' "$DEST"
if [ "$NO_OPEN" -eq 0 ]; then
  if [ "$OS" = Darwin ]; then open -e "$DEST" >/dev/null 2>&1 || message "La clave se guardó en: $DEST"
  elif [ "$DIALOG" = tk ]; then python3 - "$DEST" <<'PY'
import sys, tkinter as tk
from pathlib import Path
path=Path(sys.argv[1]);content=path.read_text(encoding='ascii')
root=tk.Tk();root.title('Clave pública lista');root.geometry('720x420')
frame=tk.Frame(root,padx=18,pady=15);frame.pack(fill='both',expand=True)
tk.Label(frame,text='Copia la clave y pégala en el campo PEM de Seguridad HTTPS del Wemos.',anchor='w').pack(fill='x')
tk.Label(frame,text='Archivo guardado: '+str(path),wraplength=675,justify='left',anchor='w').pack(fill='x',pady=(10,12))
text=tk.Text(frame,height=12,width=66,font=('Courier',10),wrap='none');text.insert('1.0',content);text.configure(state='disabled');text.pack(fill='both',expand=True)
notice=tk.StringVar();tk.Label(frame,textvariable=notice,anchor='w').pack(fill='x',pady=6)
buttons=tk.Frame(frame);buttons.pack(fill='x')
def copy():
    root.clipboard_clear();root.clipboard_append(content);root.update()
    notice.set('Clave copiada. Ya puedes pegarla en el Wemos.')
tk.Button(buttons,text='Cerrar',command=root.destroy,padx=16).pack(side='right')
tk.Button(buttons,text='Copiar clave',command=copy,padx=16).pack(side='right',padx=10)
root.mainloop()
PY
  elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$DEST" >/dev/null 2>&1 &
  else message "La clave se guardó en: $DEST. Abre el .txt y copia todo su contenido al Wemos."; fi
fi
