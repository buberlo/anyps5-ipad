#!/usr/bin/env python3
"""Test PE/NID handling with tiny test-only DLLs; never treat them as HLE."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/runtime-probes"))
sys.path.insert(0, str(ROOT / "tools/demo"))
from pe_image import PEImage, safe_library_name
from build_demo import nid

spec = importlib.util.spec_from_file_location("package_runtime", ROOT / "scripts/package-runtime.py")
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)


class PackagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("WINDOWS_CXX", "x86_64-w64-mingw32-g++")
        cls.patcher = Path(os.environ.get("NID_PATCHER", ROOT / "build/host-tools/build/nid_patcher"))
        if not shutil.which(compiler) or not cls.patcher.is_file():
            raise RuntimeError("Tests require a real MinGW compiler and built host nid_patcher")
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="pe-packaging-test-only-", dir=ROOT / "build")
        cls.directory = Path(cls.temporary.name)
        producer = cls.directory / "producer.cpp"
        producer.write_text('''extern "C" {
__declspec(dllexport) int sceFixtureValue(unsigned value) { return value + 3; }
__declspec(dllexport) int sharedUtility(void) { return 7; }
__declspec(dllexport) int sharedUtility_nid_postfix(void) { return 7; }
}
''')
        consumer = cls.directory / "consumer.cpp"
        consumer.write_text('''extern "C" {
__declspec(dllimport) int sceFixtureValue(unsigned);
__declspec(dllexport) int fixtureCall_nid_postfix(unsigned value) { return sceFixtureValue(value); }
__declspec(dllexport) int sharedUtility(void) { return 7; }
}
''')
        cls.producer = cls.directory / "fixture-runtime.prx"
        cls.consumer = cls.directory / "fixture-consumer.prx"
        subprocess.run([compiler, "-shared", str(producer), "-o", str(cls.producer),
                        "-Wl,--out-implib," + str(cls.directory / "producer.a")], check=True)
        subprocess.run([compiler, "-shared", str(consumer), str(cls.directory / "producer.a"),
                        "-o", str(cls.consumer)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_actual_pe_import_and_export_tables(self):
        self.assertIn("sceFixtureValue", PEImage(self.producer).exports()[0])
        self.assertEqual(PEImage(self.consumer).imports()["fixture-runtime.prx"], ["sceFixtureValue"])

    def test_real_nid_patcher_preserves_shared_exports_and_patches_imports(self):
        producer = self.directory / "patched-producer.prx"
        consumer = self.directory / "patched-consumer.prx"
        shutil.copyfile(self.producer, producer)
        shutil.copyfile(self.consumer, consumer)
        subprocess.run([str(self.patcher), "fixture-runtime", str(producer)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([str(self.patcher), "fixture-consumer", "--preserve-exports", str(self.producer), str(consumer)],
                       check=True, stdout=subprocess.DEVNULL)
        exports = PEImage(producer).exports()[0]
        required = PEImage(consumer).imports()["fixture-runtime.prx"]
        self.assertEqual(required, [nid("sceFixtureValue")])
        self.assertTrue(set(required).issubset(exports))
        self.assertIn("sharedUtility", exports)
        self.assertIn("sharedUtility", PEImage(consumer).exports()[0])
        self.assertIn(nid("fixtureCall"), PEImage(consumer).exports()[0])

    def test_unmapped_import_name_is_rejected(self):
        image = PEImage(self.consumer)
        changed = bytearray(image.data)
        descriptor = image.offset(image.directory(1)[0], 20)
        struct.pack_into("<I", changed, descriptor + 12, 0xfffffff0)
        bad = self.directory / "bad-import.dll"
        bad.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "unmapped PE RVA"):
            PEImage(bad).imports()

    def test_truncated_section_is_rejected(self):
        bad = self.directory / "truncated.dll"
        bad.write_bytes(self.producer.read_bytes()[:512])
        with self.assertRaises(ValueError):
            PEImage(bad)

    def test_elf_link_only_stub_is_rejected(self):
        stub = self.directory / "link-only.prx"
        stub.write_bytes(b"\x7fELF" + bytes(128))
        with self.assertRaisesRegex(ValueError, "link-only ELF stubs"):
            PEImage(stub)

    def test_fixture_hash_mismatch_is_rejected(self):
        (self.directory / "demo.exe").write_bytes(self.producer.read_bytes())
        (self.directory / "demo-manifest.json").write_text(json.dumps({
            "schema": 1, "kind": "original_synthetic_guest", "pe_sha256": "0" * 64,
        }))
        with self.assertRaisesRegex(ValueError, "PE hash"):
            packaging.load_fixture(self.directory, "demo", "original_synthetic_guest")

    def test_path_traversal_dependency_is_rejected(self):
        for name in ("../bad.dll", "a/b.dll", "a\\b.dll", "C:bad.dll", ".", "..", "bad\0.dll"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                safe_library_name(name)


if __name__ == "__main__":
    unittest.main()
