#!/usr/bin/env python3
"""Reject corrupt ELF tables, ambiguous backups, and unsafe HLE archives."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("private_game", ROOT / "scripts/prepare-private-game.py")
game = importlib.util.module_from_spec(spec)
spec.loader.exec_module(game)


def elf(strings=b"\0libkernel.prx\0", needed=1, os_tags=False):
    data = bytearray(1024)
    data[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", data, 18, 62)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<HH", data, 54, 56, 2)
    # Virtual address differs from file offset: exercise actual VA translation.
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 4, 0, 0x2000, 0, 1024, 1024, 4096)
    struct.pack_into("<IIQQQQQQ", data, 120, 2, 4, 256, 0x2100, 0, 64, 64, 8)
    tags = [(1, needed), (0x61000035 if os_tags else 5, 512 if os_tags else 0x2200),
            (0x61000037 if os_tags else 10, len(strings)), (0, 0)]
    for index, row in enumerate(tags):
        struct.pack_into("<QQ", data, 256 + index * 16, *row)
    data[512:512 + len(strings)] = strings
    return data


class PrivateGameTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def read(self, data):
        path = self.root / "input.elf"
        path.write_bytes(data)
        return game.needed_libraries(path)

    def test_runtime_index_is_preserved_but_dump_executables_are_not_assets(self):
        dump = self.root / "dump"
        contents = {"~INDEX": b"original runtime index", "data.js": b'{"project":[]}',
                    "eboot.bin": b"SELF", "eboot.bin.esbak": b"ELF backup",
                    "dump.complete": b"", "sce_module/libc.prx": b"SELF module"}
        inventory = {}
        for name, data in contents.items():
            path = dump / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            inventory[name] = {"sha256": hashlib.sha256(data).hexdigest()}
        app0 = self.root / "package/app0"
        game.stage_assets(dump, app0, inventory)
        self.assertEqual({p.relative_to(app0).as_posix() for p in app0.rglob("*") if p.is_file()},
                         {"~INDEX", "data.js"})
        self.assertEqual((app0 / "~INDEX").read_bytes(), contents["~INDEX"])
        self.assertEqual((dump / "eboot.bin").read_bytes(), contents["eboot.bin"])

    def test_sysv_and_os_string_offsets(self):
        for os_tags in (False, True):
            self.assertEqual(self.read(elf(os_tags=os_tags)), ["libkernel.prx"])

    def test_self_is_rejected(self):
        with self.assertRaises(ValueError):
            self.read(b"\x4f\x15\x3d\x1d" + bytes(1020))

    def test_truncated_program_headers(self):
        with self.assertRaises(ValueError):
            self.read(elf()[:100])

    def test_string_table_cannot_escape_load_segment(self):
        data = elf()
        struct.pack_into("<Q", data, 296, 4096)
        with self.assertRaises(ValueError):
            self.read(data)

    def test_needed_offset_is_bounded(self):
        with self.assertRaises(ValueError):
            self.read(elf(needed=0xffffffffffffffff))

    def test_needed_name_requires_terminator(self):
        with self.assertRaises(ValueError):
            self.read(elf(strings=b"\0libkernel.prx"))

    def test_needed_path_traversal_is_rejected(self):
        with self.assertRaises(ValueError):
            self.read(elf(strings=b"\0../evil.prx\0"))

    def test_select_elf_backup_without_mutating_self(self):
        original = self.root / "eboot.bin"
        original.write_bytes(b"SELF")
        backup = self.root / "eboot.bin.esbak"
        backup.write_bytes(elf())
        self.assertEqual(game.decrypted_candidate(original), backup)
        self.assertEqual(original.read_bytes(), b"SELF")

    def test_ambiguous_elf_backups_are_rejected(self):
        original = self.root / "eboot.bin"
        original.write_bytes(elf())
        (self.root / "eboot.bin.esbak").write_bytes(elf(strings=b"\0other.prx\0"))
        with self.assertRaises(ValueError):
            game.decrypted_candidate(original)

    def archive(self, name, content=b"data", expected=None, extra=False):
        path = self.root / "hle.zip"
        manifest = {"schema": 1, "kind": "anyps5_unpatched_hle_runtime", "files": {
            name: expected or hashlib.sha256(content).hexdigest()}}
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("hle-manifest.json", json.dumps(manifest))
            archive.writestr(name, content)
            if extra:
                archive.writestr("game.exe", b"private game")
        return path

    def test_hle_archive_rejects_path_traversal(self):
        with self.assertRaises(ValueError):
            game.unpack_hle(self.archive("unpatched/../escape.prx"), self.root / "out")
        self.assertFalse((self.root / "escape.prx").exists())

    def test_hle_archive_rejects_hash_mismatch_before_writing(self):
        with self.assertRaises(ValueError):
            game.unpack_hle(self.archive("unpatched/libc.prx", expected="0" * 64), self.root / "out")
        self.assertFalse((self.root / "out/unpatched/libc.prx").exists())

    def test_hle_archive_rejects_unmanifested_game_data(self):
        with self.assertRaises(ValueError):
            game.unpack_hle(self.archive("unpatched/libc.prx", extra=True), self.root / "out")


if __name__ == "__main__":
    unittest.main()
