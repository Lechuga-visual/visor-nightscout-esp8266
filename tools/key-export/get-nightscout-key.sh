#!/usr/bin/env bash
# Verified public-key export. No Python/cryptography installation is required.
set -u
umask 077
URL=''; OUTPUT_DIR=''; CA_FILE=''; HEADLESS=0; NO_OPEN=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --url|--output-dir|--ca-file) [ "$#" -ge 2 ] || { echo 'Missing option value.' >&2; exit 2; }
      if [ "$1" = '--url' ]; then URL=$2; elif [ "$1" = '--ca-file' ]; then CA_FILE=$2; else OUTPUT_DIR=$2; fi; shift 2 ;;
    --headless) HEADLESS=1; shift ;;
    --no-open) NO_OPEN=1; shift ;;
    --help) echo 'Usage: bash get-nightscout-key.sh [--url https://site/] [--output-dir folder] [--headless] [--no-open]'; exit 0 ;;
    *) echo 'Unknown option. Use --help.' >&2; exit 2 ;;
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
  display alert "Nightscout public key" message (item 1 of argv) as warning buttons {"OK"}
end run
APPLESCRIPT
      ;;
    zenity) zenity --error --title='Nightscout public key' --text="$1" >/dev/null 2>&1 ;;
    kdialog) kdialog --title 'Nightscout public key' --error "$1" >/dev/null 2>&1 ;;
    tk) python3 - "$1" <<'PY' >/dev/null 2>&1
import sys, tkinter as tk
from tkinter import messagebox
root=tk.Tk();root.withdraw()
messagebox.showerror('Nightscout public key',sys.argv[1],parent=root)
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
  [ "$HEADLESS" -eq 0 ] || fail 'Missing --url with the HTTPS site address.'
  case "$DIALOG" in
    mac) URL=$(osascript - <<'APPLESCRIPT'
set answer to display dialog "Paste your full Nightscout HTTPS URL without a token or password. The verified public key will be saved as a .txt file on the Desktop." default answer "https://" with title "Nightscout public key" buttons {"Cancel", "Get key"} default button "Get key"
text returned of answer
APPLESCRIPT
      ) || exit 0 ;;
    zenity) URL=$(zenity --entry --width=540 --title='Nightscout public key' --text='Paste the full HTTPS URL without a token or password.\nA .txt file will be saved on the Desktop to copy to the Wemos.' --entry-text='https://') || exit 0 ;;
    kdialog) URL=$(kdialog --title 'Nightscout public key' --inputbox 'Paste the full HTTPS URL without a token or password. A .txt file will be saved on the Desktop.' 'https://') || exit 0 ;;
    tk) URL=$(python3 - <<'PY'
import tkinter as tk
from tkinter import simpledialog
root=tk.Tk();root.withdraw()
value=simpledialog.askstring('Nightscout public key','Paste the full HTTPS URL without a token or password.\nA .txt file will be saved on the Desktop to copy to the Wemos.',initialvalue='https://',parent=root)
root.destroy()
if value is None: raise SystemExit(1)
print(value)
PY
      ) || exit 0 ;;
    *) printf 'Full HTTPS URL of your Nightscout: ' >&2; IFS= read -r URL || exit 0 ;;
  esac
fi
# Parse only an HTTPS authority. Paths are allowed and ignored; secrets never are.
case "$URL" in https://*) ;; *) fail 'Use a full URL starting with https://.' ;; esac
case "$URL" in *'?'*|*'#'*|*'@'*|*'\'*|*[[:space:]]*) fail 'Use a URL without a username, password, token, parameters or spaces.' ;; esac
AUTHORITY=${URL#https://}; AUTHORITY=${AUTHORITY%%/*}; HOST=''; PORT=443; VERIFY_FLAG=-verify_hostname
if [[ "$AUTHORITY" =~ ^\[([0-9a-fA-F:]+)\](:([0-9]+))?$ ]]; then
  HOST=${BASH_REMATCH[1]}; [ -z "${BASH_REMATCH[3]:-}" ] || PORT=${BASH_REMATCH[3]:-}; ADDRESS="[$HOST]"; VERIFY_FLAG=-verify_ip
elif [[ "$AUTHORITY" =~ ^([a-zA-Z0-9][a-zA-Z0-9.-]*)(:([0-9]+))?$ ]]; then
  HOST=${BASH_REMATCH[1]}; [ -z "${BASH_REMATCH[3]:-}" ] || PORT=${BASH_REMATCH[3]:-}; HOST=${HOST%.}; ADDRESS=$HOST
  [[ "$HOST" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] && VERIFY_FLAG=-verify_ip
else fail 'Invalid site address. Example: https://your-nightscout.example/'; fi
[ "${#PORT}" -le 5 ] || fail 'Invalid port.'
PORT=$((10#$PORT)); [ "$PORT" -ge 1 ] && [ "$PORT" -le 65535 ] || fail 'Invalid port.'
[ -z "$CA_FILE" ] || [ -r "$CA_FILE" ] || fail 'Could not read the specified private CA.'
command -v "$KEY_OPENSSL" >/dev/null 2>&1 || fail 'OpenSSL was not found. Install the openssl package for your system and reopen this tool.'
WORK=$(mktemp -d "${TMPDIR:-/tmp}/nightscout-key.XXXXXXXX") || fail 'Could not create a temporary folder.'
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
    /usr/bin/security find-certificate -a -p /System/Library/Keychains/SystemRootCertificates.keychain >"$WORK/mac-roots.pem" 2>/dev/null || fail 'Could not read macOS trusted certificate authorities.'
    ROOT_ARGS=( -CAfile "$WORK/mac-roots.pem" )
  fi
  printf '' | "$KEY_OPENSSL" s_client -tls1_2 "${ROOT_ARGS[@]}" -connect "$ADDRESS:$PORT" -servername "$HOST" -showcerts >"$WORK/chain.log" 2>"$WORK/tls.log" &
else
  ROOT_ARGS=( -tls1_2 ); [ -z "$CA_FILE" ] || ROOT_ARGS+=( -CAfile "$CA_FILE" )
  "$KEY_OPENSSL" s_client "${ROOT_ARGS[@]}" -connect "$ADDRESS:$PORT" -servername "$HOST" -showcerts -verify_return_error "$VERIFY_FLAG" "$HOST" </dev/null >"$WORK/chain.log" 2>"$WORK/tls.log" &
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
[ "$CONNECTED" -eq 1 ] || fail 'Could not verify the site. Check the URL, Internet and computer clock. No key was saved.'
awk -v dir="$WORK" '/-----BEGIN CERTIFICATE-----/{n++;file=dir "/cert-" n ".pem"} file!=""{print >file} /-----END CERTIFICATE-----/{close(file);file=""}' "$WORK/chain.log"
[ -s "$WORK/cert-1.pem" ] || fail 'The site did not provide a valid certificate.'
"$KEY_OPENSSL" x509 -in "$WORK/cert-1.pem" -checkend 0 -noout >/dev/null 2>&1 || fail 'The site certificate has expired. No key was saved.'
if [ "$OS" = Darwin ]; then
  NOT_BEFORE=$("$KEY_OPENSSL" x509 -in "$WORK/cert-1.pem" -startdate -noout | sed -n 's/^notBefore=//p')
  START_EPOCH=$(LC_ALL=C date -j -u -f '%b %e %T %Y %Z' "$NOT_BEFORE" '+%s' 2>/dev/null) || fail 'Could not validate the certificate date.'
  [ "$START_EPOCH" -le "$(date -u '+%s')" ] || fail 'The site certificate is not yet valid. Check the computer clock.'
  # Leaf first, followed by intermediates; the final trust decision uses macOS roots.
  CERTS=(); for cert in "$WORK"/cert-*.pem; do CERTS+=( -c "$cert" ); done
  VERIFY_ARGS=( -p ssl -s "$HOST" ); [ -z "$CA_FILE" ] || VERIFY_ARGS+=( -r "$CA_FILE" )
  /usr/bin/security verify-cert "${CERTS[@]}" "${VERIFY_ARGS[@]}" >"$WORK/verify.log" 2>&1 || fail 'macOS could not verify the site certificate. Check the URL and clock. No key was saved.'
fi
"$KEY_OPENSSL" x509 -in "$WORK/cert-1.pem" -pubkey -noout >"$WORK/public.pem" 2>"$WORK/key.log" || fail 'Could not read the verified public key.'
"$KEY_OPENSSL" asn1parse -in "$WORK/public.pem" >"$WORK/algorithm.txt" 2>>"$WORK/key.log" || fail 'The public key is invalid.'
if grep -Eq 'OBJECT[[:space:]]*:rsaEncryption[[:space:]]*$' "$WORK/algorithm.txt"; then :
elif grep -Eq 'OBJECT[[:space:]]*:id-ecPublicKey[[:space:]]*$' "$WORK/algorithm.txt" && grep -Eq 'OBJECT[[:space:]]*:(prime256v1|secp384r1|secp521r1)[[:space:]]*$' "$WORK/algorithm.txt"; then :
else fail 'The site key is not an ESP8266-compatible RSA or EC key.'; fi
[ "$(wc -c <"$WORK/public.pem")" -le 3072 ] || fail 'The key exceeds the available ESP8266 space.'
if [ -z "$OUTPUT_DIR" ]; then
  if [ "$OS" = Linux ] && command -v xdg-user-dir >/dev/null 2>&1; then OUTPUT_DIR=$(xdg-user-dir DESKTOP); fi
  if [ -z "$OUTPUT_DIR" ] || [ ! -d "$OUTPUT_DIR" ]; then
    if [ -d "$HOME/Desktop" ]; then OUTPUT_DIR=$HOME/Desktop
    elif [ -d "$HOME/Escritorio" ]; then OUTPUT_DIR=$HOME/Escritorio
    else OUTPUT_DIR=$HOME; fi
  fi
fi
[ -d "$OUTPUT_DIR" ] && [ -w "$OUTPUT_DIR" ] || fail 'The output folder is not writable.'
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
    [ "$RESULT" -eq 17 ] && [ -e "$CANDIDATE" ] || fail 'Could not save the key file.'
  fi
done
[ -n "$DEST" ] || fail 'Could not create a new file. No previous key was overwritten.'
printf 'Verified public key saved to: %s\n' "$DEST"
if [ "$NO_OPEN" -eq 0 ]; then
  if [ "$OS" = Darwin ]; then open -e "$DEST" >/dev/null 2>&1 || message "Key saved to: $DEST"
  elif [ "$DIALOG" = tk ]; then python3 - "$DEST" <<'PY'
import sys, tkinter as tk
from pathlib import Path
path=Path(sys.argv[1]);content=path.read_text(encoding='ascii')
root=tk.Tk();root.title('Public key ready');root.geometry('720x420')
frame=tk.Frame(root,padx=18,pady=15);frame.pack(fill='both',expand=True)
tk.Label(frame,text='Copy the key and paste it into the PEM field under HTTPS security on the Wemos.',anchor='w').pack(fill='x')
tk.Label(frame,text='File saved: '+str(path),wraplength=675,justify='left',anchor='w').pack(fill='x',pady=(10,12))
text=tk.Text(frame,height=12,width=66,font=('Courier',10),wrap='none');text.insert('1.0',content);text.configure(state='disabled');text.pack(fill='both',expand=True)
notice=tk.StringVar();tk.Label(frame,textvariable=notice,anchor='w').pack(fill='x',pady=6)
buttons=tk.Frame(frame);buttons.pack(fill='x')
def copy():
    root.clipboard_clear();root.clipboard_append(content);root.update()
    notice.set('Key copied. You can now paste it into the Wemos.')
tk.Button(buttons,text='Close',command=root.destroy,padx=16).pack(side='right')
tk.Button(buttons,text='Copy key',command=copy,padx=16).pack(side='right',padx=10)
root.mainloop()
PY
  elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$DEST" >/dev/null 2>&1 &
  else message "Key saved to: $DEST. Open the .txt file and copy all its contents to the Wemos."; fi
fi
