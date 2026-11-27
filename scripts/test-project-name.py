#!/usr/bin/env python3
"""The unfinished plugin has one canonical identity, with no old-brand aliases."""
import json
from pathlib import Path
import re
import subprocess
import unittest
ROOT = Path(__file__).resolve().parents[1]
class ProjectName(unittest.TestCase):
    def test_metadata_and_registrations(self):
        info = json.loads((ROOT / 'buildspec.json').read_text())
        self.assertEqual(info['name'], 'hypershader')
        self.assertEqual(info['displayName'], 'hypershader')
        self.assertEqual(info['platformConfig']['macos']['bundleId'], 'dev.hypershader.plugin')
        self.assertIn('OBS_MODULE_USE_DEFAULT_LOCALE("hypershader", "en-US")', (ROOT / 'src/plugin-main.c').read_text())
        source = (ROOT / 'src/filter-hypershader.c').read_text()
        self.assertEqual(set(re.findall(r'\.id\s*=\s*"(hypershader_\w+)"', source)), {'hypershader_filter', 'hypershader_source', 'hypershader_transition'})
    def test_no_retired_brand(self):
        pattern = re.compile(r'hyper\s*shader\s*fx|\bhsfx\b', re.I)
        paths = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z']).decode().split('\0')
        failures = []
        for name in filter(None, paths):
            if name == 'scripts/test-project-name.py':
                continue
            if pattern.search(name): failures.append(name)
            try: content = (ROOT / name).read_text()
            except (UnicodeDecodeError, IsADirectoryError): continue
            if pattern.search(content) or re.search(r"[H]yper[S]hader|[H]ypershader|hyper[S]hader|\bHYPERSHADER\b", content): failures.append(name)
        self.assertEqual(failures, [])
if __name__ == '__main__': unittest.main()
