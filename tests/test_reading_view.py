import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARDUINOJSON = ROOT / '.pio/libdeps/d1_mini/ArduinoJson/src'


class ReadingViewTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++') and ARDUINOJSON.exists(), 'Needs g++ and installed ArduinoJson')
    def test_history_freshness_plot_and_streamed_nightscout_json(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / 'reading-view'
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            '-I', str(ROOT / 'include'), '-I', str(ARDUINOJSON),
                            str(ROOT / 'tests/test_reading_view.cpp'), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('6 reading view test groups passed', result.stdout)
