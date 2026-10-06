#!/usr/bin/env python3
"""Exercise package containment and reference evidence failure handling."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("runtime_reference", ROOT / "scripts/run-runtime-reference.py")
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.package = self.directory / "package"
        self.package.mkdir()
        self.evidence = self.directory / "evidence"
        self.evidence.mkdir()
        self.metadata = {"schema": 1, "kind": "anyps5_original_runtime_package", "demo_profile": "smoke",
                         "entrypoints": {"demo": "game.exe", "guest_cpu": "guest-cpu.exe"},
                         "environment": {}, "files": {}}
        for name in self.metadata["entrypoints"].values():
            data = ("test fixture bytes: " + name).encode()
            (self.package / name).write_bytes(data)
            self.metadata["files"][name] = hashlib.sha256(data).hexdigest()

    def test_valid_package_and_hash_mismatch(self):
        entries = reference.validate_package(self.package, self.metadata)
        self.assertEqual(entries["demo"], self.package / "game.exe")
        (self.package / "guest-cpu.exe").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "file mismatch"):
            reference.validate_package(self.package, self.metadata)

    def test_unlisted_executable_cannot_be_an_entrypoint(self):
        (self.package / "unverified.exe").write_bytes(b"not hash verified")
        self.metadata["entrypoints"]["demo"] = "unverified.exe"
        with self.assertRaisesRegex(ValueError, "hash-verified executable"):
            reference.validate_package(self.package, self.metadata)

    def test_entrypoint_traversal_and_windows_aliases_are_rejected(self):
        (self.directory / "outside.exe").write_bytes(b"outside")
        for name in ("../outside.exe", str(self.directory / "outside.exe"), "C:/outside.exe",
                     "C:outside.exe", "\\\\server\\share\\outside.exe", "..\\outside.exe",
                     "./game.exe", "game.exe:stream", "game.exe\0"):
            with self.subTest(name=name):
                self.metadata["entrypoints"]["demo"] = name
                with self.assertRaises(ValueError):
                    reference.validate_package(self.package, self.metadata)

    def test_manifest_file_traversal_is_rejected_before_reading(self):
        self.metadata["files"]["../outside.exe"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "invalid package path"):
            reference.validate_package(self.package, self.metadata)

    def guest_result(self, records, *, code=0, timeout=False, required=("complete",), summary=False):
        process = SimpleNamespace(pid=123, kill=lambda: None)
        waits = [subprocess.TimeoutExpired("fixture", 1), code] if timeout else [code]
        def wait(**unused):
            result = waits.pop(0)
            if isinstance(result, Exception):
                raise result
            return result
        process.wait = wait
        def launch(command, **kwargs):
            kwargs["stdout"].write(b"non-JSON runtime diagnostic\n")
            for record in records:
                kwargs["stdout"].write((json.dumps(record) + "\n").encode())
            return process
        with patch.object(reference.subprocess, "Popen", side_effect=launch), \
                patch.object(reference.subprocess, "run") as terminate:
            result = reference.run_guest(self.package / "game.exe", self.evidence, "fixture", {}, 1,
                                         required, probe="fixture", require_summary=summary)
            self.assertEqual(terminate.called, timeout)
        return result

    def test_exit_failure_timeout_missing_stage_and_wrong_probe_fail(self):
        record = {"schema": 1, "probe": "fixture", "stage": "complete", "status": "pass"}
        self.assertEqual(self.guest_result([record])["status"], "pass")
        self.assertEqual(self.guest_result([record], code=1)["status"], "fail")
        timed_out = self.guest_result([record], timeout=True)
        self.assertEqual(timed_out["status"], "fail")
        self.assertTrue(timed_out["timed_out"])
        self.assertEqual(self.guest_result([])["status"], "fail")
        self.assertEqual(self.guest_result([dict(record, probe="wrong")])["status"], "fail")

    def test_success_summary_does_not_mask_a_failed_record(self):
        summary = {"schema": 1, "probe": "fixture", "summary": True, "failed": 0}
        failed = {"schema": 1, "probe": "fixture", "case": "actual_readback", "status": "fail"}
        self.assertEqual(self.guest_result([summary], required=(), summary=True)["status"], "pass")
        self.assertEqual(self.guest_result([failed, summary], required=(), summary=True)["status"], "fail")
        self.assertEqual(self.guest_result([], required=(), summary=True)["status"], "fail")

    def test_missing_gpu_is_explicit_not_run_but_gpu_error_fails(self):
        (self.package / "package-manifest.json").write_text(json.dumps(self.metadata))
        arguments = ["run-runtime-reference.py", str(self.package), "--evidence", str(self.evidence)]
        for gpu_status, expected in (("not_run", 0), ("fail", 1), ("available", 1)):
            with self.subTest(gpu_status=gpu_status):
                def run_guest(executable, evidence, name, *args, **kwargs):
                    return {"status": "fail" if name == "demo" else "pass"}
                inventory = {"status": gpu_status, "reason": "test", "devices": []}
                native_os = SimpleNamespace(name="nt", environ={}, pathsep=os.pathsep)
                with patch.object(reference, "os", native_os), patch.object(reference.sys, "argv", arguments), \
                        patch.object(reference, "run_guest", side_effect=run_guest), \
                        patch.object(reference, "vulkan_inventory", return_value=inventory), patch("builtins.print"):
                    self.assertEqual(reference.main(), expected)
                result = json.loads((self.evidence / "reference-summary.json").read_text())
                self.assertEqual(result["graphics"]["status"], "not_run" if gpu_status == "not_run" else "fail")
                self.assertEqual(result["device_acceptance"], "not_run")


if __name__ == "__main__":
    unittest.main()
