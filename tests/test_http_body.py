import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARDUINOJSON = ROOT / '.pio/libdeps/d1_mini/ArduinoJson/src'


class HttpBodyTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++') and ARDUINOJSON.exists(), 'Needs g++ and installed ArduinoJson')
    def test_http_framing_fragmentation_truncation_and_json_diagnostics(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / 'http-body'
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            '-I', str(ROOT / 'include'), '-I', str(ARDUINOJSON),
                            str(ROOT / 'tests/test_http_body.cpp'), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('5 HTTP body test groups passed', result.stdout)
