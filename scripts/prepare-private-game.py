#!/usr/bin/env python3
"""Relink a local decrypted dump and validate its real HLE dependency closure.

All outputs stay under ignored build/. The dump is never modified, uploaded,
or executed. A complete package still needs a Windows GPU reference and iPad
gameplay testing. Missing libraries or NIDs leave it explicitly not ready.
"""
import argparse
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/runtime-probes"))
from pe_image import PEImage, safe_library_name

spec = importlib.util.spec_from_file_location("package_runtime", ROOT / "scripts/package-runtime.py")
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)


def digest(path):
    with Path(path).open("rb") as source:
        value = hashlib.sha256()
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def dynamic_strings(path):
    """Read a bounds-checked x86-64 ELF dynamic table and its string table."""
    data = Path(path).read_bytes()

    def take(offset, size):
        if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
            raise ValueError(f"ELF field outside file: {path}")
        return data[offset:offset + size]

    def unpack(fmt, offset):
        return struct.unpack("<" + fmt, take(offset, struct.calcsize("<" + fmt)))

    if take(0, 6) != b"\x7fELF\x02\x01" or unpack("H", 18)[0] != 62:
        raise ValueError(f"Expected a decrypted little-endian x86-64 ELF: {path}")
    header_offset = unpack("Q", 32)[0]
    step, count = unpack("HH", 54)
    if step != 56 or not count:
        raise ValueError("Unsupported ELF program-header table")
    headers = [unpack("IIQQQQQQ", header_offset + i * step) for i in range(count)]
    dynamic = [h for h in headers if h[0] == 2]
    if len(dynamic) != 1 or dynamic[0][5] % 16:
        raise ValueError("Expected one aligned PT_DYNAMIC segment")
    h = dynamic[0]
    take(h[2], h[5])
    tags = []
    for offset in range(h[2], h[2] + h[5], 16):
        tag, value = unpack("QQ", offset)
        if tag == 0:
            break
        tags.append((tag, value))
    else:
        raise ValueError("Unterminated ELF dynamic table")

    def tag_value(tag):
        values = [v for t, v in tags if t == tag]
        if len(values) != 1:
            raise ValueError(f"Expected exactly one ELF tag {tag:#x}")
        return values[0]

    if any(t == 0x61000035 for t, _ in tags):
        if any(t == 5 for t, _ in tags):
            raise ValueError("Both OS and SysV string tables present")
        start, size = tag_value(0x61000035), tag_value(0x61000037)
    else:
        address, size = tag_value(5), tag_value(10)
        loads = [h for h in headers if h[0] == 1 and h[3] <= address
                 and address - h[3] <= h[5] and size <= h[5] - (address - h[3])]
        if len(loads) != 1:
            raise ValueError("ELF string table does not fit one file-backed LOAD")
        start = loads[0][2] + address - loads[0][3]
    strings = take(start, size)
    return tags, strings


def dynamic_name(strings, offset):
    if offset >= len(strings):
        raise ValueError("Dynamic string outside table")
    end = strings.find(b"\0", offset)
    if end < 0:
        raise ValueError("Unterminated dynamic string")
    return strings[offset:end].decode("ascii")


def needed_libraries(path):
    """Read DT_NEEDED, including ELF files with OS string-table tags."""
    tags, strings = dynamic_strings(path)
    result = []
    for tag, value in tags:
        if tag != 1:
            continue
        result.append(safe_library_name(dynamic_name(strings, value)))
    return result


def import_library_hints(path):
    """Associate qualified NID strings with declared import libraries.

    These are diagnostic hints, not proof that a symbol is imported or used.
    Only the relinker's actual import list decides the dependency gate.
    """
    tags, strings = dynamic_strings(path)
    libraries = {}
    for tag, value in tags:
        if tag not in (0x61000015, 0x61000049):
            continue
        library_id = value >> 48
        name = safe_library_name(dynamic_name(strings, value & 0xffffffff))
        if library_id in libraries and libraries[library_id] != name:
            raise ValueError("Conflicting ELF import-library IDs")
        libraries[library_id] = name
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"
    hints = {}
    for raw in strings.split(b"\0"):
        try:
            fields = raw.decode("ascii").split("#")
        except UnicodeDecodeError:
            continue
        if len(fields) != 3 or len(fields[0]) != 11:
            continue
        nid, encoded_library, module = fields
        if not encoded_library or not module or any(c not in alphabet for c in nid + encoded_library + module):
            continue
        library_id = 0
        for character in encoded_library:
            library_id = library_id * 64 + alphabet.index(character)
        if library_id in libraries:
            hints.setdefault(nid, set()).add(libraries[library_id])
    return hints


def nid_catalog(path):
    """Read local NID/name pairs; accept names only when their hashes match."""
    result = {}
    salt = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")
    for row in path.read_text().splitlines():
        fields = row.split()
        if len(fields) != 2:
            continue
        nid, name = fields
        computed = base64.b64encode(hashlib.sha1(name.encode() + salt).digest()[:8][::-1]).decode()[:11].replace("/", "-")
        if computed == nid:
            result[nid] = name
    return result


MODULE_DIRECTORIES = ("sce_module", "sce_modules", "prx", "Media/Modules", "Media/Plugins")


def decrypted_candidate(original, overlay=None):
    candidates = []
    paths = [original, original.with_name(original.name + ".esbak")]
    if overlay is not None:
        paths += [overlay, overlay.with_name(overlay.name + ".esbak")]
    for path in paths:
        if path.is_file():
            with path.open("rb") as source:
                if source.read(4) == b"\x7fELF":
                    candidates.append(path)
    if not candidates:
        raise ValueError(f"No decrypted ELF beside {original.name}")
    if len({digest(path) for path in candidates}) > 1:
        raise ValueError(f"Ambiguous decrypted copies of {original.name}; use a clean input directory")
    return candidates[0]


def select_elfs(dump):
    """Select real decrypted files without replacing any input SELF or backup."""
    overlay = dump / "decrypted"
    selected = {"eboot.elf": decrypted_candidate(dump / "eboot.bin", overlay / "eboot.bin")}
    if (any((base / "sce_module").exists() for base in (dump, overlay)) and
            any((base / "sce_modules").exists() for base in (dump, overlay))):
        raise ValueError("Both sce_module and sce_modules exist")
    module_names = set()
    for directory in MODULE_DIRECTORIES:
        names = set()
        for folder in (dump / directory, overlay / directory):
            if folder.is_dir():
                names.update(path.name.removesuffix(".esbak") for path in folder.iterdir()
                             if path.is_file() and path.name.endswith((".prx", ".prx.esbak")))
        for name in sorted(names):
            selected[directory + "/" + name] = decrypted_candidate(
                dump / directory / name, overlay / directory / name)
            module_names.add(name)
    return selected, module_names


def unpack_hle(archive_path, destination):
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate HLE archive members")
        manifest = json.loads(archive.read("hle-manifest.json"))
        if manifest.get("schema") != 1 or manifest.get("kind") != "anyps5_unpatched_hle_runtime":
            raise ValueError("Not a built unpatched HLE runtime archive")
        if set(names) != set(manifest["files"]) | {"hle-manifest.json"}:
            raise ValueError("Unexpected HLE archive contents")
        for name, expected in manifest["files"].items():
            parts = name.split("/")
            if len(parts) != 2 or parts[0] not in ("unpatched", "runtime"):
                raise ValueError("Unsafe HLE archive path")
            safe_library_name(parts[1])
            if Path(parts[1]).suffix not in (".prx", ".dll"):
                raise ValueError("Unexpected non-library in HLE archive")
            data = archive.read(name)
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f"HLE archive hash mismatch: {name}")
            path = destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            PEImage(path)
    return manifest


def audit_hle(stage, roots, patcher, archive_path):
    source = stage / ".hle-input"
    source.mkdir()
    manifest = unpack_hle(archive_path, source)
    libc = source / "unpatched/libc.prx"
    if "GuestArenaAllocate_nid_postfix" not in PEImage(libc).exports()[0]:
        raise ValueError("Refusing to NID-patch an already patched HLE build")
    candidates = {p.name.lower(): p for p in source.rglob("*") if p.is_file()}
    images, exports, pending, system = {}, {}, list(roots), set()
    missing_libraries = set()
    libs = stage / "libs"
    libs.mkdir()
    with (stage / "preparation/nid-patch.log").open("w") as log:
        while pending:
            name = pending.pop(0).lower()
            if packaging.external(name):
                system.add(name)
                continue
            if name in images or name in missing_libraries:
                continue
            original = candidates.get(name)
            if original is None:
                missing_libraries.add(name)
                continue
            path = libs / original.name
            shutil.copy2(original, path)
            if path.suffix == ".prx":
                command = [str(patcher), path.stem]
                if path.name.lower() != "libc.prx":
                    command += ["--preserve-exports", str(libc)]
                subprocess.run(command + [str(path)], stdout=log, stderr=subprocess.STDOUT, check=True)
            image = PEImage(path)
            images[name], exports[name] = image, image.exports()
            pending.extend(image.imports())
            for forward in exports[name][1].values():
                if forward:
                    library, _, _symbol = forward.rpartition(".")
                    pending.append(safe_library_name(library if library.lower().endswith(".dll") else library + ".dll"))
    unresolved_native = []
    for name, image in images.items():
        checks = [(library, symbol) for library, symbols in image.imports().items() for symbol in symbols]
        for forward in exports[name][1].values():
            if forward:
                library, _, symbol = forward.rpartition(".")
                checks.append((library if library.lower().endswith(".dll") else library + ".dll",
                               int(symbol[1:]) if symbol.startswith("#") else symbol))
        for library, symbol in checks:
            if packaging.external(library):
                continue
            available = exports.get(library.lower(), ({}, {}))
            if symbol not in available[1 if isinstance(symbol, int) else 0]:
                unresolved_native.append(f"{name}: {library}!{symbol}")
    # The relinker's startup searches only explicitly loaded handles, guest
    # modules first. A transitive DLL's exports cannot resolve a guest NID.
    providers = {key: set(exports[key][0]) for key in roots if key in exports}
    for path in (stage / "app0").rglob("*.guest.prx"):
        providers[path.relative_to(stage).as_posix()] = set(PEImage(path).exports()[0])
    diagnostics = stage / "preparation/windows-diagnostics-imports.txt"
    needed = set(diagnostics.read_text().splitlines())
    provided = set().union(*providers.values())
    # AnyPS5 synthesizes this TLS resolver in WindowsEntryStubBuilder when
    # bundled modules do not export it. All other NIDs must have real exports.
    tls_generated = bool(list((stage / "app0").rglob("*.guest.prx"))) and "vNe1w4diLCs" not in provided
    if tls_generated:
        provided.add("vNe1w4diLCs")
    audit = {"hle_source": manifest, "hle_archive_sha256": digest(archive_path),
             "loaded_host_libraries": roots, "packaged_libraries": sorted(images),
             "missing_libraries": sorted(missing_libraries), "unresolved_native_imports": sorted(set(unresolved_native)),
             "guest_nid_count": len(needed), "unresolved_guest_nids": sorted(needed - provided),
             "generated_platform_tls_resolver": tls_generated,
             "target_provided_libraries": sorted(system | {"vulkan-1.dll", "shell32.dll"})}
    audit["passed"] = not (audit["missing_libraries"] or audit["unresolved_native_imports"] or audit["unresolved_guest_nids"])
    shutil.rmtree(source)
    return audit


def stage_assets(dump, app0, inventory):
    for relative, info in inventory.items():
        path = Path(relative)
        # ~INDEX is a runtime VFS index, not a dumper sidecar. Keep it and
        # other asset names, including names starting with a tilde.
        if (path.parts[0] in ("sce_module", "sce_modules", "prx", "decrypted", "fakelib") or
                (path.parent.as_posix() in MODULE_DIRECTORIES and path.name.endswith(".prx")) or
                relative == "eboot.bin" or path.name.endswith((".esbak", ".complete"))):
            continue
        destination = app0 / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dump / path, destination)
        if digest(destination) != info["sha256"]:
            raise ValueError("Dump changed while staging assets")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--hle", type=Path, help="Built hle-runtime.zip, never contains game data")
    parser.add_argument("--relinker", type=Path, default=ROOT / "build/host-tools/build/relinker/relinker")
    parser.add_argument("--nid-patcher", type=Path, default=ROOT / "build/host-tools/build/nid_patcher")
    parser.add_argument("--nid-catalog", type=Path, help="Optional local whitespace-separated NID/name catalog; names are hash-verified")
    args = parser.parse_args()
    dump, output = args.dump.resolve(), args.output.resolve()
    if ROOT / "build" not in output.parents or output.exists():
        raise ValueError("Output must be a NEW directory under ignored build/")
    if output == dump or dump in output.parents or output in dump.parents:
        raise ValueError("Output must be separate from the immutable dump")
    files = sorted(dump.rglob("*"))
    if any(p.is_symlink() for p in files):
        raise ValueError("Symlinks are not accepted inside private dumps")
    inventory = {p.relative_to(dump).as_posix(): {"bytes": p.stat().st_size, "sha256": digest(p)}
                 for p in files if p.is_file()}
    param = json.loads((dump / "sce_sys/param.json").read_text())
    selected, module_names = select_elfs(dump)
    needed = {name: needed_libraries(path) for name, path in selected.items()}
    hints = {name: import_library_hints(path) for name, path in selected.items()}
    names = nid_catalog(args.nid_catalog) if args.nid_catalog else {}
    roots = sorted({library.lower() for names in needed.values() for library in names
                    if library not in module_names})
    if "libscelibcinternal.prx" in roots and "libc.prx" not in roots:
        roots.append("libc.prx")
        roots.sort()
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".private-game-", dir=output.parent))
    try:
        input_dir, preparation = stage / ".input", stage / "preparation"
        input_dir.mkdir()
        preparation.mkdir()
        for name, source in selected.items():
            path = input_dir / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, path)
            if digest(path) != inventory[source.relative_to(dump).as_posix()]["sha256"]:
                raise ValueError("Dump changed while staging an ELF")
        # Relinker diagnostics are relative to cwd; keep them in the private
        # staging directory instead of accidentally writing into the repo root.
        command = [str(args.relinker.resolve()), "--windows", "--windows-diagnostics", "--to-intel", "--registry",
                   str(input_dir / "eboot.elf"), str(stage / "game.exe")]
        with (preparation / "relink.log").open("w") as log:
            subprocess.run(command, cwd=preparation, stdout=log, stderr=subprocess.STDOUT, check=True)
        PEImage(stage / "game.exe")
        shutil.move(stage / "game.registry.json", preparation / "game.registry.json")
        shutil.rmtree(input_dir)
        app0 = stage / "app0"
        app0.mkdir(exist_ok=True)
        stage_assets(dump, app0, inventory)
        audit = audit_hle(stage, roots, args.nid_patcher.resolve(), args.hle.resolve()) if args.hle else {
            "passed": False, "missing_libraries": roots, "reason": "hle_archive_not_supplied"}
        audit["unresolved_guest_imports"] = [
            {"nid": nid, "name": names.get(nid), "references": [
                {"module": module, "library": library}
                for module, imports in sorted(hints.items())
                for library in sorted(imports.get(nid, ()))
            ]}
            for nid in audit.get("unresolved_guest_nids", ())
        ]
        if args.nid_catalog:
            audit["nid_catalog_sha256"] = digest(args.nid_catalog)
        (stage / "REFERENCE.md").write_text(
            "# Private game package\n\n"
            "Keep this package local; it contains your dumped game assets and converted binaries.\n"
            "Run from this directory on native Windows with a Vulkan GPU only after the\n"
            "dependency audit passes. The existing native CPU/memory/GPU probes should\n"
            "qualify that PC first. No Windows or iPad gameplay result is implied by packaging.\n\n"
            "Example PowerShell environment after a successful mapping probe at 8 GiB:\n\n"
            "```powershell\n"
            "$env:APS5_GUEST_ARENA_LAZY = '1'\n"
            "$env:APS5_GUEST_ARENA_BASE = '0x200000000'\n"
            "$env:APS5_GUEST_ARENA_SIZE = '0x100000000'\n"
            "$env:APS5_GUEST_ARENA_CHUNK = '0x10000000'\n"
            ".\\game.exe *> windows-game.log\n"
            "```\n\n"
            "Record GPU/driver, actual menu and gameplay, controls, sound, saves, crashes\n"
            "and exact package hashes. Retain the log even if startup fails.\n"
            "The qualified iPad uses a different arena: 0x7400000000 (464 GiB).\n"
            "Madeira configuration must be applied through its own game/global config;\n"
            "a config file beside game.exe is not automatically applied.\n")
        metadata = {"schema": 1, "kind": "anyps5_private_game_package",
                    "title_id": param["titleId"], "content_version": param["contentVersion"],
                    "title": param["localizedParameters"][param["localizedParameters"]["defaultLanguage"]]["titleName"],
                    "selected_inputs": {name: {"dump_path": p.relative_to(dump).as_posix(), "sha256": inventory[p.relative_to(dump).as_posix()]["sha256"]} for name, p in selected.items()},
                    "dump_inventory": inventory, "elf_dependencies": needed, "dependency_audit": audit,
                    "relinker_sha256": digest(args.relinker), "nid_patcher_sha256": digest(args.nid_patcher),
                    "preparer_sha256": digest(Path(__file__)),
                    "status": "prepared_unexecuted" if audit["passed"] else "not_ready_missing_dependencies",
                    "windows_gpu_reference": "not_run", "ipad_gameplay": "not_run",
                    "files": {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}}
        (stage / "private-game-manifest.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
        stage.rename(output)
        print(f"{metadata['title']} {metadata['content_version']}: {metadata['status']}; {output}")
        print(f"Dependencies: {len(roots)} explicit HLE libraries; audit passed={audit['passed']}")
        return 0 if audit["passed"] else 2
    finally:
        if stage.exists():
            shutil.rmtree(stage)


if __name__ == "__main__":
    sys.exit(main())
