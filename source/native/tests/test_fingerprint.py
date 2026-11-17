"""The committed framework fingerprint must describe the packaged plugins."""
import json
import re
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FINGERPRINT = json.loads((ROOT / 'source/native/framework-fingerprint.json').read_text())
DIGEST = re.compile(r'[0-9a-f]{64}')


class FingerprintTest(unittest.TestCase):
    def test_covers_every_packaged_plugin(self):
        packaged = sorted(Path(name).name for package in (ROOT / 'packages').glob('*.zip')
                          for name in zipfile.ZipFile(package).namelist()
                          if re.fullmatch(r'[^/]+/lib[^/]+\.so', name))
        self.assertEqual(FINGERPRINT['plugins'], packaged)

    def test_holds_digests_and_a_plugin_app_vtable(self):
        self.assertEqual(FINGERPRINT['format'], 1)
        self.assertEqual(sorted(FINGERPRINT['frameworks']), ['libAppFramework.A.so', 'libCoreFramework.A.so'])
        functions = [f for lib in FINGERPRINT['frameworks'].values() for f in lib['functions'].values()]
        self.assertGreaterEqual(len(functions), 35)
        for function in functions:
            self.assertEqual(sorted(function), ['bytes', 'prefix_sha256'])
            self.assertTrue(DIGEST.fullmatch(function['prefix_sha256']))
        self.assertTrue(any('_ZTVN3bmw9PluginAppE' in lib['vtables'] for lib in FINGERPRINT['frameworks'].values()))


if __name__ == '__main__':
    unittest.main()
