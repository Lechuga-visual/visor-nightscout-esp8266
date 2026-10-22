import hashlib
from pathlib import Path
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
TOOLS=ROOT/'tools/key-export'


class KeyPackageTests(unittest.TestCase):
    def test_downloads_match_sources_and_preserve_launch_permissions(self):
        windows=(TOOLS/'Obtener-clave-Nightscout.cmd').read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
        unix=(TOOLS/'obtener-clave-nightscout.sh').read_bytes()
        self.assertEqual((TOOLS/'Obtener-clave-Nightscout.command').read_bytes(),unix)
        with zipfile.ZipFile(TOOLS/'clave-nightscout-windows.zip') as package:
            self.assertIsNone(package.testzip())
            self.assertEqual(package.read('Obtener-clave-Nightscout.cmd'),windows)
            self.assertEqual(windows.count(b'\r\n'),windows.count(b'\n'))
        with zipfile.ZipFile(TOOLS/'clave-nightscout-linux-macos.zip') as package:
            self.assertIsNone(package.testzip())
            for name in ['obtener-clave-nightscout.sh','Obtener-clave-Nightscout.command']:
                self.assertEqual(package.read(name),unix)
                self.assertEqual((package.getinfo(name).external_attr>>16)&0o777,0o755)
        for line in (TOOLS/'SHA256SUMS').read_text().splitlines():
            digest,name=line.split('  ')
            data=(TOOLS/name).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(),digest)
