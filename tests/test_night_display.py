import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NightDisplayTests(unittest.TestCase):
    def test_storage_schedule_dst_scaling_and_preview_expiry(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = str(pathlib.Path(directory) / 'night-display')
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wno-class-memaccess',
                            '-I', str(ROOT / 'include'), str(ROOT / 'tests/test_night_display.cpp'),
                            '-o', executable], check=True)
            subprocess.run([executable], check=True)
