import pathlib
import shutil
import subprocess
import tempfile
import unittest


class DeviceSettingsTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "Native settings tests require g++")
    def test_firmware_configuration_storage_and_validation(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / "test_device_settings"
            subprocess.run([
                "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I", str(root / "include"), str(root / "tests/test_device_settings.cpp"),
                "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("6 settings test groups passed", result.stdout)
