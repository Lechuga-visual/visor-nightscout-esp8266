import gzip
import pathlib
import re
import unittest
from tools.embed_web_assets import generate, english_assets

ROOT = pathlib.Path(__file__).resolve().parents[1]


class WebAssetTests(unittest.TestCase):
    def test_checked_in_gzip_assets_match_the_browser_sources(self):
        generated = (ROOT / 'include/generated/web_assets.h').read_text()
        self.assertEqual(generated, generate())
        for name in ('index.html', 'ui.css', 'app.js', 'appearance.js', 'setup.js', 'night.js'):
            block = re.search(r'\b' + name.replace('.', '_') + r'\[\].*?\{(.*?)\};', generated, re.S)
            data = bytes(int(value,16) for value in re.findall(r'0x([0-9a-f]{2})', block[1]))
            self.assertEqual(gzip.decompress(data), (ROOT / 'web' / name).read_bytes())

        fragments = re.findall(r'const char setup_html_\d+\[\] PROGMEM = R"NSSETUP\((.*?)\)NSSETUP";', generated, re.S)
        template = (ROOT / 'web/setup.html').read_text()
        self.assertEqual(fragments, re.split(r'{{[a-z_]+}}', template))

        for name, text in english_assets().items():
            self.assertEqual((ROOT / 'web/en' / name).read_text(), text)
            if name == 'setup.html':
                fragments = re.findall(r'const char en_setup_html_\d+\[\] PROGMEM = R"NSSETUP\((.*?)\)NSSETUP";', generated, re.S)
                self.assertEqual(fragments, re.split(r'{{[a-z_]+}}', text))
            else:
                symbol = 'en_' + name.replace('.', '_')
                block = re.search(r'\b' + symbol + r'\[\].*?\{(.*?)\};', generated, re.S)
                data = bytes(int(value,16) for value in re.findall(r'0x([0-9a-f]{2})', block[1]))
                self.assertEqual(gzip.decompress(data), text.encode())
