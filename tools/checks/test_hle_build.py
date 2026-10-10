#!/usr/bin/env python3
"""Check generic target selection before any compiler/download is invoked."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
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


class HostContracts(unittest.TestCase):
    savedata_targets = ('savedata_memory_growth_tests', 'savedata_memory_metadata_tests',
                        'savedata_native_write_replacement_tests', 'savedata_replace_file_failure_tests')
    savedata_cases = ('savedata_memory_growth', 'savedata_memory_growth_read_failure',
                      'savedata_memory_metadata', 'savedata_native_write_replacement',
                      'savedata_replace_file_failure')

    def test_savedata_targets_exist_and_are_selected_by_both_builders(self):
        shell = (ROOT / 'scripts/m0-build-anyps5-winlibs.sh').read_text()
        shell_targets = re.search(r'\btests=\((.*?)\)', shell, re.DOTALL).group(1).split()
        cmake = (ROOT / 'upstreams/AnyPS5/core/libs/CMakeLists.txt').read_text()
        for target in self.savedata_targets:
            with self.subTest(target=target):
                self.assertEqual(build.TESTS.count(target), 1)
                self.assertEqual(shell_targets.count(target), 1)
                self.assertRegex(cmake, r'add_test_executable\(' + re.escape(target) + r'\s')

    def test_growth_ctest_selection_is_checked_against_exact_registered_inventory(self):
        command = build.host_test_command('savedata_memory_growth_tests', Path('/build'), str)
        self.assertEqual(command[:3], ['ctest', '--test-dir', '/build'])
        self.assertIn('--no-tests=error', command)
        self.assertEqual(command[command.index('--timeout') + 1], '45')
        pattern = command[command.index('-R') + 1]
        self.assertTrue(all(re.search(pattern, name) for name in self.savedata_cases[:2]))
        build.validate_ctest_inventory('savedata_memory_growth_tests',
                                      json.dumps({'tests': [{'name': name} for name in self.savedata_cases[:2]]}))
        for names in (self.savedata_cases[:1], self.savedata_cases[:2] + ('savedata_memory_growth_extra',),
                      ('savedata_memory_growth', 'savedata_memory_growth')):
            with self.subTest(names=names), self.assertRaises(ValueError):
                build.validate_ctest_inventory('savedata_memory_growth_tests',
                                              json.dumps({'tests': [{'name': name} for name in names]}))
        for value in command:
            build.quote(value)

    def test_shell_ctest_selection_covers_all_registered_savedata_cases_exactly(self):
        shell = (ROOT / 'scripts/m0-build-anyps5-winlibs.sh').read_text()
        pattern = re.search(r"-R '([^']*savedata_memory_growth[^']*)'", shell).group(1)
        candidates = self.savedata_cases + ('savedata_core_write_replacement',
                     'savedata_memory_growth_extra', 'prefix_savedata_memory_metadata')
        self.assertEqual([name for name in candidates if re.search(pattern, name)],
                         list(self.savedata_cases))
        cmake = (ROOT / 'upstreams/AnyPS5/core/libs/CMakeLists.txt').read_text()
        for name in self.savedata_cases:
            self.assertRegex(cmake, r'add_test\(NAME\s+' + re.escape(name) + r'\s')
        self.assertIn('set_tests_properties(savedata_memory_growth_read_failure PROPERTIES SKIP_RETURN_CODE 77)', cmake)

    def test_lifecycle_still_receives_exact_ctest_fixture_registration(self):
        command = build.host_test_command('guest_kernel_module_lifecycle_tests', Path('/build'), str)
        self.assertEqual(command[command.index('-R') + 1], 'guest_kernel_module_lifecycle')
        self.assertIn('--no-tests=error', command)
        build.validate_ctest_inventory('guest_kernel_module_lifecycle_tests',
                                      json.dumps({'tests': [{'name': 'guest_kernel_module_lifecycle'}]}))
        for value in command:
            build.quote(value)
        with self.assertRaises(ValueError):
            build.quote('^unsafe_for_cmd$')

    def test_standalone_tests_use_the_built_executable_and_unknown_targets_fail(self):
        for target in self.savedata_targets[1:]:
            with self.subTest(target=target):
                self.assertEqual(build.host_test_command(target, Path('/build'), str),
                                 ['/build/tests/' + target + '.exe'])
        with self.assertRaises(ValueError):
            build.host_test_command('../arbitrary', Path('/build'), str)

    def test_skipped_read_failure_is_separate_from_the_required_passed_growth_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / 'result.xml'
            report.write_text('<testsuite><testcase name="savedata_memory_growth" status="run"/>'
                              '<testcase name="savedata_memory_growth_read_failure" status="notrun">'
                              '<skipped message="SKIP_RETURN_CODE"/></testcase></testsuite>')
            results = build.ctest_case_results('savedata_memory_growth_tests', report)
            self.assertEqual(results['savedata_memory_growth']['status'], 'passed')
            self.assertEqual(results['savedata_memory_growth_read_failure']['status'], 'skipped')
            self.assertEqual(results['savedata_memory_growth_read_failure']['configured_skip_return_code'], 77)
            report.write_text('<testsuite><testcase name="savedata_memory_growth"><skipped/></testcase>'
                              '<testcase name="savedata_memory_growth_read_failure"/></testsuite>')
            with self.assertRaises(ValueError):
                build.ctest_case_results('savedata_memory_growth_tests', report)
            report.write_text('<testsuite><testcase name="savedata_memory_growth"/></testsuite>')
            with self.assertRaises(ValueError):
                build.ctest_case_results('savedata_memory_growth_tests', report)

    @unittest.skipUnless(shutil.which('ctest'), 'A native CTest executable is required')
    def test_actual_ctest_inventory_and_skip_report_are_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            python = sys.executable.replace('\\', '/')
            (directory / 'CTestTestfile.cmake').write_text(
                'add_test(savedata_memory_growth "' + python + '" "-c" "raise SystemExit(0)")\n'
                'add_test(savedata_memory_growth_read_failure "' + python + '" "-c" "raise SystemExit(77)")\n'
                'set_tests_properties(savedata_memory_growth_read_failure PROPERTIES SKIP_RETURN_CODE 77)\n')
            inventory = subprocess.run(['ctest', '--test-dir', str(directory), '-R',
                                        build.ctest_pattern('savedata_memory_growth_tests'), '--show-only=json-v1'],
                                       check=True, capture_output=True, text=True, timeout=10)
            build.validate_ctest_inventory('savedata_memory_growth_tests', inventory.stdout)
            command = build.host_test_command('savedata_memory_growth_tests', directory, str)
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=10)
            results = build.ctest_case_results('savedata_memory_growth_tests',
                                               directory / 'test-savedata_memory_growth_tests.xml')
            self.assertEqual(results['savedata_memory_growth']['status'], 'passed')
            self.assertEqual(results['savedata_memory_growth_read_failure']['status'], 'skipped')


if __name__ == '__main__':
    unittest.main()
