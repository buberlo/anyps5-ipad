#!/usr/bin/env python3
"""Check generic target selection before any compiler/download is invoked."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hlebuild', ROOT / 'scripts/build-hle.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class Targets(unittest.TestCase):
    def test_full_selection_is_discovered_and_explicit_selection_is_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('libc', 'libkernel', 'libDifferentTitleService', 'libSecond.native'):
                (root / 'core/libs/prx' / name).mkdir(parents=True)
            self.assertEqual(build.choose_targets(root, [], None),
                             ['libDifferentTitleService', 'libSecond.native', 'libc', 'libkernel'])
            self.assertEqual(build.choose_targets(root, ['libSecond.native'], None),
                             ['libSecond.native', 'libc', 'libkernel'])
            for value in ('../libc', 'libMissing', 'libc & echo bad', 'libc\n'):
                with self.assertRaises(ValueError):
                    build.choose_targets(root, [value], None)

    def test_prior_manifest_selects_names_without_reusing_binaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('libc', 'libkernel', 'libOther'):
                (root / 'core/libs/prx' / name).mkdir(parents=True)
            archive = root / 'prior.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('hle-manifest.json', json.dumps({'files': {'unpatched/libOther.prx': 'previous-hash', 'runtime/libgcc_s_seh-1.dll': 'runtime-hash'}}))
            self.assertEqual(build.choose_targets(root, [], archive), ['libOther', 'libc', 'libkernel'])


if __name__ == '__main__':
    unittest.main()
