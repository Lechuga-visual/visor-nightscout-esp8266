import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class StartupViewTests(unittest.TestCase):
    def test_real_connection_response_and_reading_stages(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = str(pathlib.Path(directory) / 'startup')
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra',
                            '-I', str(ROOT / 'include'), str(ROOT / 'tests/test_startup_view.cpp'),
                            '-o', executable], check=True)
            subprocess.run([executable], check=True)
