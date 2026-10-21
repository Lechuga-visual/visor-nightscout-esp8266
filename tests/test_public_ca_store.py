import importlib.util
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from datetime import datetime, timedelta, timezone


ROOT = pathlib.Path(__file__).resolve().parents[1]
CORE = pathlib.Path(os.environ.get("PLATFORMIO_CORE_DIR", "/workspace/.platformio"))
BEARSSL = CORE / "packages/framework-arduinoespressif8266/tools/sdk/ssl/bearssl"
AVAILABLE = all(shutil.which(tool) for tool in ("g++", "make", "cc")) and BEARSSL.exists()


@unittest.skipUnless(AVAILABLE and importlib.util.find_spec("cryptography"),
                     "TLS validation tests need native compilers, ESP8266 BearSSL sources and cryptography")
class PublicCaStoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = ROOT / ".pio/native-bearssl"
        subprocess.run(["make", "-s", "-C", str(BEARSSL), "-j2", "BUILD=" + str(cls.build), "lib"],
                       check=True, capture_output=True, text=True)

    def compile(self, sources, output, extra=()):
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-I", str(ROOT / "include"), "-I", str(BEARSSL / "inc"), *extra,
                        *map(str, sources), str(self.build / "libbearssl.a"), "-o", str(output)],
                       check=True, capture_output=True, text=True)

    def test_all_public_roots_are_decodable_and_indexed_correctly(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / "check-roots"
            self.compile([ROOT / "tests/test_public_ca_bundle.cpp"], binary)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("120 public roots decoded", result.stdout)

    def test_real_bearssl_certificate_validation(self):
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec, rsa
        from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
        from tools.generate_trust_store import encode_store

        now = datetime.now(timezone.utc)

        def authority(name, rsa_key=False):
            key = rsa.generate_private_key(public_exponent=65537, key_size=2048) if rsa_key else ec.generate_private_key(ec.SECP256R1())
            subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
            certificate = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
                           .public_key(key.public_key()).serial_number(x509.random_serial_number())
                           .not_valid_before(now - timedelta(days=60)).not_valid_after(now + timedelta(days=365))
                           .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
                           .sign(key, hashes.SHA256()))
            return key, certificate

        def leaf(key, issuer, expired=False):
            public = ec.generate_private_key(ec.SECP256R1()).public_key()
            return (x509.CertificateBuilder()
                    .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "unit.nightscout.invalid")]))
                    .issuer_name(issuer.subject).public_key(public).serial_number(x509.random_serial_number())
                    .not_valid_before(now - timedelta(days=30))
                    .not_valid_after(now - timedelta(days=1) if expired else now + timedelta(days=30))
                    .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                    .add_extension(x509.SubjectAlternativeName([x509.DNSName("unit.nightscout.invalid")]), critical=False)
                    .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
                    .sign(key, hashes.SHA256()))

        key, ca = authority("Native TLS test CA")
        other_key, other_ca = authority("Untrusted native TLS test CA")
        rsa_key, rsa_ca = authority("Native RSA TLS test CA", rsa_key=True)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            header = directory / "fixture-ca.h"
            header.write_text(encode_store([ca, rsa_ca], "ephemeral unit-test roots only")[0])
            inputs = []
            for name, certificate in (("valid", leaf(key, ca)), ("expired", leaf(key, ca, True)),
                                      ("untrusted", leaf(other_key, other_ca)), ("valid-rsa", leaf(rsa_key, rsa_ca))):
                path = directory / (name + ".der")
                path.write_bytes(certificate.public_bytes(serialization.Encoding.DER))
                inputs.append(str(path))
            binary = directory / "check-tls"
            self.compile([ROOT / "src/public_ca_store.cpp", ROOT / "tests/test_public_ca_store.cpp"], binary,
                         ['-DPUBLIC_CA_DATA_HEADER="' + str(header) + '"'])
            result = subprocess.run([str(binary), *inputs], check=True, capture_output=True, text=True)
            self.assertIn("wrong host, expired, untrusted and tampered certificates rejected", result.stdout)
