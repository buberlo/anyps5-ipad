#!/usr/bin/env python3
"""Reject corrupt ELF tables, ambiguous backups, and unsafe HLE archives."""
import hashlib
import base64
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

    def test_deferred_lifecycle_requires_exact_discovered_plugin(self):
        selected = {"eboot.elf": None, "Media/Plugins/SaveData.prx": None}
        self.assertEqual(game.deferred_lifecycle_modules(["SaveData.prx"], selected), ["SaveData.prx"])
        self.assertEqual(game.deferred_lifecycle_modules([], selected), [])
        for name in ("Unknown.prx", "savedata.prx", "eboot.elf"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                game.deferred_lifecycle_modules([name], selected)

    def test_deferred_lifecycle_rejects_paths_globs_and_option_tokens(self):
        for name in ("../SaveData.prx", "Media/Plugins/SaveData.prx", r"Media\SaveData.prx",
                     "*.prx", "", "--option.prx", "bad\x00.prx", "bad\n.prx"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                game.deferred_lifecycle_modules([name], {name: None})

    def test_deferred_lifecycle_rejects_duplicate_or_ambiguous_modules(self):
        with self.assertRaises(ValueError):
            game.deferred_lifecycle_modules(["SaveData.prx", "SaveData.prx"], {"SaveData.prx": None})
        with self.assertRaises(ValueError):
            game.deferred_lifecycle_modules(["SaveData.prx"],
                {"Media/Plugins/SaveData.prx": None, "sce_module/SaveData.prx": None})

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

    def library_elf(self, tag, value, strings=b"\0libkernel.prx\0AAAAAAAAAAA#B#B\0"):
        data = elf(strings=strings)
        struct.pack_into("<QQ", data, 256 + 3 * 16, tag, value)
        struct.pack_into("<QQ", data, 256 + 4 * 16, 0, 0)
        struct.pack_into("<QQ", data, 120 + 32, 80, 80)
        path = self.root / "library.elf"
        path.write_bytes(data)
        return path

    def test_import_library_ids_are_decoded_without_changing_needed_libraries(self):
        for tag in (0x61000015, 0x61000049):
            path = self.library_elf(tag, (1 << 48) | 1)
            self.assertEqual(game.import_library_hints(path), {"AAAAAAAAAAA": {"libkernel.prx"}})
            self.assertEqual(game.needed_libraries(path), ["libkernel.prx"])

    def test_undeclared_library_id_and_unrelated_unicode_are_not_import_evidence(self):
        path = self.library_elf(0x61000049, (2 << 48) | 1,
                                b"\0libkernel.prx\0AAAAAAAAAAA#B#B\0\xc3\xa4\0")
        self.assertEqual(game.import_library_hints(path), {})

    def test_import_library_name_is_bounded(self):
        path = self.library_elf(0x61000049, (1 << 48) | 0xffff)
        with self.assertRaises(ValueError):
            game.import_library_hints(path)

    def test_conflicting_library_ids_are_rejected(self):
        path = self.library_elf(0x61000049, (1 << 48) | 1,
                                b"\0libkernel.prx\0other.prx\0AAAAAAAAAAA#B#B\0")
        data = bytearray(path.read_bytes())
        struct.pack_into("<QQ", data, 256 + 4 * 16, 0x61000049, (1 << 48) | 15)
        struct.pack_into("<QQ", data, 256 + 5 * 16, 0, 0)
        struct.pack_into("<QQ", data, 120 + 32, 96, 96)
        path.write_bytes(data)
        with self.assertRaises(ValueError):
            game.import_library_hints(path)

    def test_catalog_names_must_match_the_nid_hash(self):
        name = "syntheticApi"
        salt = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")
        nid = base64.b64encode(hashlib.sha1(name.encode() + salt).digest()[:8][::-1]).decode()[:11].replace("/", "-")
        catalog = self.root / "catalog.txt"
        catalog.write_text(f"{nid} {name}\nAAAAAAAAAAA inventedName\nmalformed\n")
        self.assertEqual(game.nid_catalog(catalog), {nid: name})

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

    def test_decrypted_overlay_preserves_original_self(self):
        original = self.root / "eboot.bin"
        original.write_bytes(b"SELF")
        overlay = self.root / "decrypted/eboot.bin"
        overlay.parent.mkdir()
        overlay.write_bytes(elf())
        self.assertEqual(game.decrypted_candidate(original, overlay), overlay)
        self.assertEqual(original.read_bytes(), b"SELF")

    def test_conflicting_overlay_is_rejected(self):
        original = self.root / "eboot.bin"
        original.write_bytes(elf())
        overlay = self.root / "decrypted/eboot.bin"
        overlay.parent.mkdir()
        overlay.write_bytes(elf(strings=b"\0other.prx\0"))
        with self.assertRaises(ValueError):
            game.decrypted_candidate(original, overlay)

    def test_overlay_is_not_assumed_to_be_decrypted(self):
        original = self.root / "eboot.bin"
        original.write_bytes(b"SELF")
        overlay = self.root / "decrypted/eboot.bin"
        overlay.parent.mkdir()
        overlay.write_bytes(b"SELF")
        with self.assertRaises(ValueError):
            game.decrypted_candidate(original, overlay)

    def test_overlay_only_unity_modules_keep_their_paths(self):
        overlay = self.root / "decrypted"
        overlay.mkdir()
        (overlay / "eboot.bin").write_bytes(elf())
        expected = {"eboot.elf": overlay / "eboot.bin"}
        modules = {"sce_module/provider.prx", "Media/Modules/engine.prx",
                   "Media/Plugins/plugin.prx"}
        for name in modules:
            path = overlay / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(elf())
            expected[name] = path
        selected, names = game.select_elfs(self.root)
        self.assertEqual(selected, expected)
        self.assertEqual(names, {Path(name).name for name in modules})

    def test_singular_plural_conflict_across_overlay_is_rejected(self):
        (self.root / "eboot.bin").write_bytes(elf())
        (self.root / "sce_module").mkdir()
        (self.root / "decrypted/sce_modules").mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "Both sce_module and sce_modules"):
            game.select_elfs(self.root)

    def test_unity_binaries_and_overlay_are_not_copied_as_assets(self):
        dump = self.root / "dump"
        contents = {"Media/Modules/engine.prx": b"SELF",
                    "Media/Plugins/plugin.prx": b"SELF",
                    "Media/Modules/module-data.bin": b"real asset",
                    "Media/level0": b"scene",
                    "decrypted/Media/Modules/engine.prx": bytes(elf()),
                    "fakelib/libSceFixture.sprx": b"dump helper",
                    "ampr_emu.index": b"preserve unknown index"}
        inventory = {}
        for name, data in contents.items():
            path = dump / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            inventory[name] = {"sha256": hashlib.sha256(data).hexdigest()}
        app0 = self.root / "package/app0"
        game.stage_assets(dump, app0, inventory)
        self.assertEqual({p.relative_to(app0).as_posix() for p in app0.rglob("*") if p.is_file()},
                         {"Media/Modules/module-data.bin", "Media/level0", "ampr_emu.index"})

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
