"""Exercise the actual standalone scripts against local, verified TLS fixtures."""
import contextlib
import importlib.util
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import unittest
from datetime import datetime, timedelta, timezone

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / 'tools/key-export/obtener-clave-nightscout.sh'
WINDOWS = ROOT / 'tools/key-export/Obtener-clave-Nightscout.cmd'
PWSH = os.environ.get('KEY_EXPORT_PWSH') or shutil.which('powershell') or shutil.which('pwsh')


@unittest.skipUnless(importlib.util.find_spec('cryptography'), 'Needs cryptography for TLS fixtures')
class KeyLauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec, rsa
        from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.fixtures = {}
        for kind in ('rsa', 'ec', 'ec384', 'ec521', 'expired', 'future', 'untrusted'):
            key = rsa.generate_private_key(public_exponent=65537, key_size=2048) if kind == 'rsa' else ec.generate_private_key(
                ec.SECP384R1() if kind == 'ec384' else ec.SECP521R1() if kind == 'ec521' else ec.SECP256R1())
            subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'localhost')])
            now = datetime.now(timezone.utc)
            cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key())
                    .serial_number(x509.random_serial_number()).not_valid_before(now+timedelta(days=1) if kind == 'future' else now-timedelta(days=10))
                    .not_valid_after(now+timedelta(days=10) if kind != 'expired' else now-timedelta(days=1))
                    .add_extension(x509.BasicConstraints(ca=True,path_length=None),critical=True)
                    .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost')]),critical=False)
                    .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),critical=False)
                    .sign(key,hashes.SHA256()))
            crt=cls.directory/(kind+'.crt');private=cls.directory/(kind+'.private.pem')
            crt.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
            private.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
            cls.fixtures[kind]=(crt,private,key.public_key().public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo))

    @contextlib.contextmanager
    def server(self, kind):
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(*self.fixtures[kind][:2])
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(12)
        errors=[]
        def serve():
            try:
                with listener:
                    conn,_=listener.accept()
                    with conn:
                        conn.settimeout(5)
                        with context.wrap_socket(conn,server_side=True) as tls:
                            data=tls.recv(1024)
                            if data: errors.append('Tool sent application data instead of only obtaining the TLS key')
            except (ssl.SSLError,ConnectionError): pass
            except Exception as exc: errors.append(repr(exc))
        thread=threading.Thread(target=serve,daemon=True);thread.start()
        try: yield listener.getsockname()[1]
        finally:
            thread.join(14);self.assertFalse(thread.is_alive());self.assertEqual(errors,[])

    def run_tool(self, platform, url, output, trust=None):
        env=os.environ.copy()
        shell=ROOT/'tools/key-export/get-nightscout-key.sh' if getattr(self,'english_tool',False) else SHELL
        windows=ROOT/'tools/key-export/Get-Nightscout-Key.cmd' if getattr(self,'english_tool',False) else WINDOWS
        if platform == 'shell':
            if trust: env['SSL_CERT_FILE']=str(trust)
            else: env.pop('SSL_CERT_FILE',None)
            command=['bash',str(shell),'--url',url,'--output-dir',str(output),'--headless','--no-open']
            if trust:command+=['--ca-file',str(trust)]
        else:
            command=[PWSH,'-NoLogo','-NoProfile','-File',str(ROOT/'tests/key_export_windows.ps1'),
                     '-Tool',str(windows),'-Url',url,'-Directory',str(output)]
            if trust:
                if os.name == 'nt':command+=['-TrustCertificate',str(trust)]
                else:env['SSL_CERT_FILE']=str(trust)
            else:env.pop('SSL_CERT_FILE',None)
        return subprocess.run(command,capture_output=True,text=True,env=env,timeout=28)

    def exercise(self, platform):
        from cryptography.hazmat.primitives import serialization
        # Windows Schannel's default TLS 1.2 curve policy excludes P-521.
        # Its SPKI conversion is tested separately, without changing OS TLS policy.
        kinds=('rsa','ec','ec384') if platform == 'windows' and os.name == 'nt' else ('rsa','ec','ec384','ec521')
        for kind in kinds:
            with self.subTest(platform=platform,key=kind),tempfile.TemporaryDirectory(prefix='key output ') as target:
                out=Path(target)
                # Repeated exports make distinct TXT files instead of overwriting any existing one.
                for _ in range(2):
                    with self.server(kind) as port:
                        result=self.run_tool(platform,f'https://localhost:{port}/some/path',out,self.fixtures[kind][0])
                    self.assertEqual(result.returncode,0,result.stderr)
                files=list(out.glob('*.txt'));self.assertEqual(len(files),2)
                for file in files:
                    pem=file.read_bytes();self.assertTrue(pem.startswith(b'-----BEGIN PUBLIC KEY-----'))
                    self.assertTrue(pem.rstrip().endswith(b'-----END PUBLIC KEY-----'))
                    der=serialization.load_pem_public_key(pem).public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo)
                    self.assertEqual(der,self.fixtures[kind][2]);self.assertLessEqual(len(pem),3072)
        for kind,host,trust in [('ec','127.0.0.1','ec'),('expired','localhost','expired'),('future','localhost','future'),('untrusted','localhost',None)]:
            with self.subTest(platform=platform,rejected=(kind,host)),tempfile.TemporaryDirectory() as target:
                out=Path(target);existing=out/'previous.txt';existing.write_text('keep existing file')
                with self.server(kind) as port:
                    result=self.run_tool(platform,f'https://{host}:{port}',out,self.fixtures[trust][0] if trust else None)
                self.assertNotEqual(result.returncode,0)
                self.assertEqual(list(out.iterdir()),[existing]);self.assertEqual(existing.read_text(),'keep existing file')
        for url in ('http://localhost','https://user:password@localhost','https://localhost/?token=secret','https://localhost/#secret','https://localhost:0','https://localhost:65536','https://localhost/\\unsafe','https://localhost/no-port'):
            with self.subTest(platform=platform,url=url),tempfile.TemporaryDirectory() as target:
                result=self.run_tool(platform,url,Path(target));self.assertNotEqual(result.returncode,0)
                self.assertFalse(list(Path(target).iterdir()))
                self.assertNotIn('unbound variable',result.stderr)

    @unittest.skipUnless(PWSH,'PowerShell required')
    def test_windows_spki_encoding_including_p521(self):
        from cryptography.hazmat.primitives import serialization
        for kind in ('rsa','ec','ec384','ec521'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as directory:
                result=subprocess.run([PWSH,'-NoLogo','-NoProfile','-File',str(ROOT/'tests/key_export_windows.ps1'),
                    '-Tool',str(WINDOWS),'-Directory',directory,'-PublicKeyFixture',str(self.fixtures[kind][0])],
                    capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stderr)
                pem=next(Path(directory).glob('*.txt')).read_bytes()
                der=serialization.load_pem_public_key(pem).public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo)
                self.assertEqual(der,self.fixtures[kind][2])

    @unittest.skipIf(os.name=='nt' or not shutil.which('openssl'),'Unix shell and OpenSSL required')
    def test_shell_export_and_rejection(self):self.exercise('shell')

    @unittest.skipUnless(PWSH and __import__('sys').platform != 'darwin','PowerShell on Windows/Linux required')
    def test_windows_export_and_rejection(self):
        if os.name == 'nt' and os.environ.get('GITHUB_ACTIONS') != 'true':
            self.skipTest('Windows OS-trust fixtures run only in disposable CI runners')
        self.exercise('windows')

    @unittest.skipIf(os.name=='nt' or not shutil.which('openssl'),'Unix shell and OpenSSL required')
    def test_english_shell_export_and_rejection(self):
        self.english_tool=True
        self.exercise('shell')

    @unittest.skipUnless(PWSH and __import__('sys').platform != 'darwin','PowerShell on Windows/Linux required')
    def test_english_windows_export_and_rejection(self):
        if os.name == 'nt' and os.environ.get('GITHUB_ACTIONS') != 'true':
            self.skipTest('Windows OS-trust fixtures run only in disposable CI runners')
        self.english_tool=True
        self.exercise('windows')
