import pathlib
import shutil
import subprocess
import tempfile
import unittest


class DeviceClockTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "Native clock tests require g++")
    def test_wifi_retry_reconnect_and_browser_time_validation(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / "test_device_clock"
            subprocess.run([
                "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I", str(root / "include"), str(root / "tests/test_device_clock.cpp"),
                "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("6 clock test groups passed", result.stdout)
