import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PatientLabelTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'Needs g++')
    def test_legacy_offsets_name_storage_validation_and_display_characters(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / 'patient-label'
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            '-I', str(ROOT / 'include'), str(ROOT / 'tests/test_patient_label.cpp'),
                            '-o', str(binary)], check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)
