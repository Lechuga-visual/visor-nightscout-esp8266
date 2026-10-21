import contextlib
import importlib.util
import pathlib
import socket
import ssl
import subprocess
import tempfile
import threading
import unittest
from datetime import datetime, timedelta, timezone

from test_public_ca_store import AVAILABLE, BEARSSL, ROOT


@unittest.skipUnless(AVAILABLE and importlib.util.find_spec("cryptography"),
                     "Pinned TLS tests need native compilers, ESP8266 BearSSL and cryptography")
class PinnedTlsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec, rsa
        from cryptography.x509.oid import NameOID

        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = pathlib.Path(cls.temporary.name)
        cls.binary = cls.directory / "pinned-tls"
        build = ROOT / ".pio/native-bearssl"
        subprocess.run(["make", "-s", "-C", str(BEARSSL), "-j2", "BUILD=" + str(build), "lib"],
                       check=True, capture_output=True, text=True)
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-I", str(BEARSSL / "inc"), "-I", str(ROOT / "include"), str(ROOT / "tests/test_pinned_tls.cpp"),
                        str(build / "libbearssl.a"), "-o", str(cls.binary)],
                       check=True, capture_output=True, text=True)
        cls.fixtures = {}
        now = datetime.now(timezone.utc)
        for label in ("ec", "ec384", "ec521", "rsa", "wrong", "valid"):
            key = (rsa.generate_private_key(public_exponent=65537, key_size=2048)
                   if label == "rsa" else ec.generate_private_key(
                       ec.SECP384R1() if label == "ec384" else ec.SECP521R1() if label == "ec521" else ec.SECP256R1()))
            name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
            certificate = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
                           .public_key(key.public_key()).serial_number(x509.random_serial_number())
                           .not_valid_before(now - timedelta(days=30))
                           .not_valid_after(now + timedelta(days=30) if label == "valid" else now - timedelta(days=1))
                           .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
                           .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost")]), critical=False)
                           .sign(key, hashes.SHA256()))
            cert_path = cls.directory / (label + ".crt")
            private_path = cls.directory / (label + "-private.pem")
            public_path = cls.directory / (label + "-public.der")
            cert_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
            private_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                                      serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
            public_path.write_bytes(key.public_key().public_bytes(serialization.Encoding.DER,
                                                                  serialization.PublicFormat.SubjectPublicKeyInfo))
            cls.fixtures[label] = cert_path, private_path, public_path

    @contextlib.contextmanager
    def server(self, label):
        cert, private, _ = self.fixtures[label]
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cert, private)
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(8)
        errors = []

        def serve():
            try:
                with listener:
                    connection, _ = listener.accept()
                    with connection:
                        connection.settimeout(5)
                        with context.wrap_socket(connection, server_side=True) as tls:
                            data = tls.recv(4096)
                            if data:
                                tls.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK")
            except (ssl.SSLError, ConnectionError):
                # Expected when the client rejects an untrusted certificate or wrong pin.
                pass
            except Exception as exc:
                errors.append(exc)

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            yield listener.getsockname()[1]
        finally:
            thread.join(timeout=10)
            self.assertFalse(thread.is_alive(), "TLS fixture server did not finish")
            self.assertEqual(errors, [])

    def test_real_tls_handshake_requires_pinned_ec_or_rsa_key_without_clock(self):
        for label in ("ec", "ec384", "ec521", "rsa"):
            for match in (True, False):
                with self.subTest(key_type=label, matching_key=match), self.server(label) as port:
                    key = self.fixtures[label if match else "wrong"][2]
                    result = subprocess.run([str(self.binary), str(port), str(key)],
                                            capture_output=True, text=True, timeout=8)
                    self.assertEqual(result.returncode == 0, match, result.stderr)

    def test_key_export_rejects_untrusted_expired_and_wrong_hostname(self):
        from cryptography.hazmat.primitives import serialization
        from tools.export_server_key import export_key

        cert = self.fixtures["valid"][0]
        with self.server("valid") as port:
            pem, fingerprint = export_key(f"https://localhost:{port}", str(cert))
            public = serialization.load_pem_public_key(pem)
            actual = public.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
            self.assertEqual(actual, self.fixtures["valid"][2].read_bytes())
            self.assertTrue(fingerprint.startswith("sha256/"))
        for label, hostname, trust in (("valid", "127.0.0.1", cert),
                                       ("valid", "localhost", None),
                                       ("ec", "localhost", self.fixtures["ec"][0])):
            with self.subTest(label=label, hostname=hostname, trust=bool(trust)), self.server(label) as port:
                with self.assertRaises(ssl.SSLCertVerificationError):
                    export_key(f"https://{hostname}:{port}", str(trust) if trust else None)
        for url in ("http://localhost", "https://user:pass@localhost", "https://localhost?token=example"):
            with self.assertRaises(ValueError):
                export_key(url)
