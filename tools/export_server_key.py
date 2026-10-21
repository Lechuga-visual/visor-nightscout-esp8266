#!/usr/bin/env python3
"""Export a server's public key only after normal TLS certificate verification.

Run on a trusted PC: python tools/export_server_key.py https://your-nightscout --output server-key.pem
Requires cryptography. Does not send a Nightscout token or query glucose data.
"""

import argparse
import base64
import hashlib
import socket
import ssl
import sys
from pathlib import Path
from urllib.parse import urlsplit


def export_key(url, ca_file=None):
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization

    parsed = urlsplit(url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.query or parsed.fragment):
        raise ValueError("Usa solo una URL HTTPS, sin usuario, contraseña, token ni parámetros.")
    context = ssl.create_default_context(cafile=ca_file)
    # Intentionally fail on untrusted/expired certificates or a hostname mismatch.
    # Never fetch a pin with verification disabled, and never trust a new key automatically.
    with socket.create_connection((parsed.hostname, parsed.port or 443), timeout=10) as tcp:
        with context.wrap_socket(tcp, server_hostname=parsed.hostname) as tls:
            certificate = x509.load_der_x509_certificate(tls.getpeercert(binary_form=True))
    key = certificate.public_key()
    from cryptography.hazmat.primitives.asymmetric import ec, rsa
    if not isinstance(key, (rsa.RSAPublicKey, ec.EllipticCurvePublicKey)):
        raise ValueError("El ESP8266 necesita una clave RSA o EC compatible con BearSSL.")
    if isinstance(key, ec.EllipticCurvePublicKey) and key.curve.name not in ("secp256r1", "secp384r1", "secp521r1"):
        raise ValueError("Curva EC no admitida por BearSSL.")
    pem = key.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
    der = key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    if len(pem) > 3072:
        raise ValueError("La clave excede el espacio del dispositivo.")
    fingerprint = "sha256/" + base64.b64encode(hashlib.sha256(der).digest()).decode("ascii")
    return pem, fingerprint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="URL base HTTPS de tu Nightscout")
    parser.add_argument("--output", type=Path, help="Archivo PEM de salida; omitir para imprimirlo")
    parser.add_argument("--ca-file", help="CA privada de confianza, solo si tu servidor la utiliza")
    args = parser.parse_args()
    try:
        pem, fingerprint = export_key(args.url, args.ca_file)
        if args.output:
            args.output.write_bytes(pem)
            print("Clave pública verificada guardada en " + str(args.output), file=sys.stderr)
        else:
            sys.stdout.write(pem.decode("ascii"))
        print("Identificador de la clave: " + fingerprint, file=sys.stderr)
        print("Pega el PEM en Seguridad HTTPS del Wemos. Si el servidor cambia de clave, repite el proceso.", file=sys.stderr)
    except (OSError, ValueError, ImportError) as exc:
        print("No se pudo obtener una clave verificada: " + str(exc), file=sys.stderr)
        print("No uses una conexión sin verificar para obtenerla. Revisa URL, reloj y CA del PC.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
