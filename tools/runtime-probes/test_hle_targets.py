#!/usr/bin/env python3
"""Validate private HLE target selection without compiling or downloading."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hle_targets_build', ROOT / 'scripts/build-hle.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class HleTargets(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.libraries = self.root / 'core/libs/prx'
        for name in ('libc', 'libkernel', 'libSceHmd', 'ulobjmgr'):
            (self.libraries / name).mkdir(parents=True)

    def tearDown(self):
        self.temporary.cleanup()

    def manifest(self, files):
        archive = self.root / 'prior-hle.zip'
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('hle-manifest.json', json.dumps({'files': dict.fromkeys(files, 'prior-hash')}))
        return archive

    def test_exact_object_manager_is_discovered_and_deduplicated(self):
        self.assertEqual(build.choose_targets(self.root, [], None),
                         ['libSceHmd', 'libc', 'libkernel', 'ulobjmgr'])
        self.assertEqual(build.choose_targets(self.root, ['ulobjmgr', 'ulobjmgr'], None),
                         ['libc', 'libkernel', 'ulobjmgr'])

    def test_exception_still_requires_existing_library_directory(self):
        (self.libraries / 'ulobjmgr').rmdir()
        (self.libraries / 'ulobjmgr').write_text('not a library directory')
        with self.assertRaises(ValueError):
            build.choose_targets(self.root, ['ulobjmgr'], None)

    def test_other_non_lib_names_are_rejected_even_when_present(self):
        for name in ('othermgr', 'ulobjmgr2', 'ULOBJMGR'):
            with self.subTest(name=name):
                (self.libraries / name).mkdir(exist_ok=True)
                with self.assertRaises(ValueError):
                    build.choose_targets(self.root, [name], None)

    def test_unknown_paths_and_shell_syntax_are_rejected(self):
        for name in ('libMissing', '../ulobjmgr', 'ulobjmgr/../libc', 'ulobjmgr.prx',
                     'ulobjmgr;echo bad', 'ulobjmgr$(echo bad)', 'ulobjmgr & echo bad',
                     'ulobjmgr\n', 'ulobjmgr\\libc'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                build.choose_targets(self.root, [name], None)

    def test_prior_manifest_and_explicit_names_keep_exact_closure(self):
        archive = self.manifest(('unpatched/ulobjmgr.prx', 'unpatched/libSceHmd.prx',
                                 'runtime/libgcc_s_seh-1.dll'))
        self.assertEqual(build.choose_targets(self.root, ['ulobjmgr'], archive),
                         ['libSceHmd', 'libc', 'libkernel', 'ulobjmgr'])

    def test_prior_manifest_cannot_hide_a_path_in_a_valid_basename(self):
        for name in ('unpatched/../ulobjmgr.prx', 'unpatched/nested/ulobjmgr.prx',
                     'unpatched/ulobjmgr;echo bad.prx', 'unpatched/libMissing.prx'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                build.choose_targets(self.root, [], self.manifest((name,)))


class WinlibsTargets(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.targets_file = self.root / 'private-targets.txt'
        self.script = ROOT / 'scripts/m0-build-anyps5-winlibs.sh'

    def tearDown(self):
        self.temporary.cleanup()

    def selection(self, text, script=None):
        self.targets_file.write_text(text)
        return subprocess.run(['bash', str(script or self.script), '--print-hle-targets'],
                              env={**os.environ, 'APS5_HLE_TARGETS_FILE': str(self.targets_file)},
                              cwd=self.root, text=True, capture_output=True, timeout=10)

    def test_exact_object_manager_and_crlf_targets_are_deduplicated(self):
        result = self.selection('ulobjmgr\r\nlibSceHmd\r\nulobjmgr')
        self.assertEqual(result.returncode, 0, result.stderr)
        targets = result.stdout.splitlines()
        self.assertEqual(targets.count('ulobjmgr'), 1)
        self.assertEqual(targets.count('libSceHmd'), 1)

    def test_object_manager_exception_is_not_a_general_name_or_shell_exception(self):
        for name in ('othermgr', 'ulobjmgr2', 'libMissing', '../ulobjmgr',
                     'ulobjmgr;echo bad', 'ulobjmgr$(echo bad)', 'ulobjmgr & echo bad',
                     'ulobjmgr\\libc'):
            with self.subTest(name=name):
                result = self.selection(name + '\n')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('target', result.stderr.lower())

    def test_missing_object_manager_directory_is_rejected_before_any_build(self):
        default = self.selection('')
        self.assertEqual(default.returncode, 0, default.stderr)
        isolated = self.root / 'scripts/m0-build-anyps5-winlibs.sh'
        isolated.parent.mkdir()
        shutil.copy2(self.script, isolated)
        for name in default.stdout.splitlines():
            (self.root / 'upstreams/AnyPS5/core/libs/prx' / name).mkdir(parents=True)
        result = self.selection('ulobjmgr\n', isolated)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unknown HLE target or uninitialized AnyPS5 submodule: ulobjmgr', result.stderr)


if __name__ == '__main__':
    unittest.main()
