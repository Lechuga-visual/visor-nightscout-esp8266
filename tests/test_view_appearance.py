import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ViewAppearanceTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'Needs g++')
    def test_storage_migration_partial_updates_and_readable_colors(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / 'view-appearance'
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            '-I', str(ROOT / 'include'), str(ROOT / 'tests/test_view_appearance.cpp'),
                            '-o', str(binary)], check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('3 appearance test groups passed', result.stdout)
