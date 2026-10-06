#!/usr/bin/env python3
"""Package original guest fixtures with a freshly NID-patched real PRX closure.

Only unpatched HLE build outputs are accepted. The script never mutates them,
never substitutes linker stubs and never downloads DLLs. An existing package
is atomically replaced only with --replace and a valid owned-package manifest.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/runtime-probes"))
from pe_image import PEImage, safe_library_name

SYSTEM_DLLS = set("""advapi32.dll avrt.dll bcrypt.dll cfgmgr32.dll comctl32.dll comdlg32.dll
crypt32.dll d3d11.dll d3d12.dll d3dcompiler_47.dll dbghelp.dll dwmapi.dll dxgi.dll gdi32.dll
hid.dll imm32.dll iphlpapi.dll kernel32.dll kernelbase.dll mpr.dll msvcrt.dll ntdll.dll ole32.dll
oleaut32.dll powrprof.dll propsys.dll psapi.dll rpcrt4.dll secur32.dll setupapi.dll shell32.dll
shlwapi.dll ucrtbase.dll user32.dll userenv.dll usp10.dll uxtheme.dll version.dll winhttp.dll
wininet.dll winmm.dll winspool.drv wintrust.dll wldap32.dll ws2_32.dll wtsapi32.dll
xinput1_3.dll xinput1_4.dll xinput9_1_0.dll vulkan-1.dll""".split())
ENVIRONMENT = {
    "APS5_GUEST_ARENA_BASE": "0x200000000", "APS5_GUEST_ARENA_SIZE": "0x100000000",
    "APS5_GUEST_ARENA_CHUNK": "0x10000000", "APS5_GUEST_ARENA_LAZY": "1",
    "MADEIRA_FEX_AVX": "1", "MADEIRA_CONTROLS_XBOX_DEFAULT": "1", "MADEIRA_FRAMEGEN": "0",
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def external(name):
    low = name.lower()
    return low in SYSTEM_DLLS or low.startswith(("api-ms-win-", "ext-ms-win-"))


def load_fixture(directory, stem, kind):
    manifest_path = directory / (stem + "-manifest.json")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or manifest.get("kind") != kind:
        raise ValueError(f"unexpected fixture manifest: {manifest_path}")
    executable = directory / (stem + ".exe")
    if digest(executable) != manifest.get("pe_sha256"):
        raise ValueError(f"fixture PE hash does not match {manifest_path}")
    for name, wanted in manifest["source_sha256"].items():
        source = (ROOT / name).resolve()
        if ROOT not in source.parents or digest(source) != wanted:
            raise ValueError(f"fixture source changed since build: {name}")
    PEImage(executable)
    return executable, manifest_path, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/anyps5-mingw")
    parser.add_argument("--unpatched", type=Path, help="defaults to BUILD/core/libs/libs/unpatched")
    parser.add_argument("--nid-patcher", type=Path, default=ROOT / "build/host-tools/build/nid_patcher")
    parser.add_argument("--demo", type=Path, default=ROOT / "build/demo-acceptance")
    parser.add_argument("--guest-cpu", type=Path, default=ROOT / "build/guest-cpu")
    parser.add_argument("--guest-exceptions", type=Path, default=ROOT / "build/guest-exceptions")
    parser.add_argument("--runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, default=ROOT / "build/runtime-package")
    parser.add_argument("--replace", action="store_true")
    parser.add_argument("--zip", type=Path, help="optional deterministic archive of the completed package")
    args = parser.parse_args()
    unpatched = (args.unpatched or args.build / "core/libs/libs/unpatched").resolve()
    output = args.output.resolve()
    if output == ROOT or ROOT not in output.parents:
        raise ValueError("package output must be a child of this repository")
    if args.zip and (args.zip.resolve() == output or output in args.zip.resolve().parents):
        raise ValueError("archive must be outside the package directory")
    if output.exists():
        marker = output / "package-manifest.json"
        if not args.replace or not marker.is_file() or json.loads(marker.read_text()).get("kind") != "anyps5_original_runtime_package":
            raise ValueError("output exists; --replace only accepts a previously generated runtime package")
    libc = unpatched / "libc.prx"
    libc_exports, _ = PEImage(libc).exports()
    if "GuestArenaAllocate_nid_postfix" not in libc_exports:
        raise ValueError("libc input is not an unpatched AnyPS5 build; refusing to NID-patch twice")
    fixtures = [load_fixture(args.demo, "demo", "original_synthetic_guest"),
                load_fixture(args.guest_cpu, "guest-cpu", "original_synthetic_guest_cpu")]
    if (args.guest_exceptions / "guest-exceptions-manifest.json").is_file():
        fixtures.append(load_fixture(args.guest_exceptions, "guest-exceptions", "original_synthetic_guest_exceptions"))
    candidates = {}
    for folder in [unpatched, *args.runtime_dir]:
        if not folder.is_dir():
            raise ValueError(f"dependency directory missing: {folder}")
        for path in sorted(folder.iterdir()):
            if path.is_file() and path.suffix.lower() in (".prx", ".dll"):
                key = safe_library_name(path.name).lower()
                if key in candidates and digest(candidates[key]) != digest(path):
                    raise ValueError(f"ambiguous runtime dependency {path.name}: two different binaries")
                candidates[key] = path
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".runtime-package-", dir=output.parent))
    try:
        libs = stage / "libs"
        libs.mkdir()
        (stage / "app0").mkdir()
        (stage / "app0/.keep").write_text("Original demo has no external game assets.\n")
        images, sources, exported, dependencies, system = {}, {}, {}, {}, set()
        roots = {item["library"].lower() for _, _, fixture in fixtures for item in fixture["imports"]}
        pending = sorted(roots)
        while pending:
            key = pending.pop(0)
            if key in images or external(key):
                if external(key):
                    system.add(key)
                continue
            source = candidates.get(key)
            if source is None:
                raise ValueError(f"missing dependency {key}; add its build output directory with --runtime-dir")
            PEImage(source)
            destination = libs / source.name
            shutil.copyfile(source, destination)
            if source.suffix.lower() == ".prx":
                if source.parent != unpatched:
                    raise ValueError(f"PRX must come from the explicit unpatched directory: {source}")
                command = [str(args.nid_patcher.resolve()), source.stem]
                if source.name.lower() != "libc.prx":
                    command += ["--preserve-exports", str(libc)]
                subprocess.run(command + [str(destination)], check=True)
            image = PEImage(destination)
            images[key], sources[key] = image, source
            exported[key] = image.exports()
            imports = image.imports()
            dependencies[key] = imports
            for name in imports:
                safe_library_name(name)
                pending.append(name.lower())
            for forward in exported[key][1].values():
                if forward:
                    library, dot, symbol = forward.rpartition(".")
                    if not dot or not symbol:
                        raise ValueError(f"invalid forwarded export: {forward}")
                    name = library if library.lower().endswith(".dll") else library + ".dll"
                    pending.append(safe_library_name(name).lower())

        def require_symbol(library, symbol):
            key = library.lower()
            if external(key):
                return
            named, ordinal = exported[key]
            if (isinstance(symbol, int) and symbol not in ordinal) or (isinstance(symbol, str) and symbol not in named):
                raise ValueError(f"unresolved runtime import {library}!{symbol}")

        # Validate both the PE dependency graph and the custom relinker loader's
        # NID imports (those are not in game.exe's normal PE import directory).
        for key, imports in dependencies.items():
            for library, symbols in imports.items():
                for symbol in symbols:
                    require_symbol(library, symbol)
            for forward in exported[key][1].values():
                if forward:
                    library, _, symbol = forward.rpartition(".")
                    require_symbol(library if library.lower().endswith(".dll") else library + ".dll",
                                   int(symbol[1:]) if symbol.startswith("#") else symbol)
        for executable, manifest_path, fixture in fixtures:
            name = "game.exe" if fixture["kind"] == "original_synthetic_guest" else executable.name
            shutil.copyfile(executable, stage / name)
            shutil.copyfile(manifest_path, stage / manifest_path.name)
            for item in fixture["imports"]:
                require_symbol(item["library"], item["nid"])
            for library in PEImage(executable).imports():
                if not external(library):
                    raise ValueError(f"unexpected native guest dependency: {library}")
                system.add(library.lower())
        # Vulkan is loaded at runtime; it is deliberately provided by the
        # platform bridge/driver, not copied from an unrelated Windows SDK.
        system.update(("vulkan-1.dll", "shell32.dll"))
        config = ["# Import these lines via Madeira: Game details > This game's config.",
                  "# A file beside game.exe is NOT auto-applied by Madeira.",
                  "# 8–12 GiB is a candidate: require native Mach + Windows VA probes first.",
                  "# Fixed mappings must fail on collision; do not silently relocate."]
        config += [f"env.{name} = {value}" for name, value in sorted(ENVIRONMENT.items())]
        config += ["d3d12 = 0"]
        (stage / "game-profile.cfg").write_text("\n".join(config) + "\n")
        metadata = {"schema": 1, "kind": "anyps5_original_runtime_package", "runtime_verified": False,
                    "entrypoints": {"demo": "game.exe", "guest_cpu": "guest-cpu.exe"},
                    "demo_profile": fixtures[0][2]["profile"], "environment": ENVIRONMENT,
                    "loader_layout": "Executables load NID-patched PRX dependencies from adjacent libs/; app0/ is assets.",
                    "configuration": "Import game-profile.cfg into Madeira game config; not auto-applied.",
                    "device_va_status": "candidate_unverified", "target_provided_libraries": sorted(system),
                    "dynamic_dependencies": {"libSceAgcDriver.prx": ["vulkan-1.dll"], "guest_startup": ["shell32.dll"]},
                    "nid_patcher_sha256": digest(args.nid_patcher),
                    "packager_sha256": digest(Path(__file__)),
                    "pe_reader_sha256": digest(ROOT / "tools/runtime-probes/pe_image.py"),
                    "unpatched_inputs": {sources[key].name: digest(sources[key]) for key in sorted(sources)},
                    "dependency_imports": {images[key].path.name: dependencies[key] for key in sorted(dependencies)},
                    "files": {str(path.relative_to(stage)).replace(os.sep, "/"): digest(path)
                              for path in sorted(stage.rglob("*")) if path.is_file()}}
        if len(fixtures) == 3:
            metadata["entrypoints"]["guest_exceptions"] = "guest-exceptions.exe"
        (stage / "package-manifest.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
        backup = output.with_name(output.name + ".previous")
        if output.exists():
            if backup.exists():
                raise ValueError(f"backup already exists: {backup}")
            output.rename(backup)
            try:
                stage.rename(output)
            except BaseException:
                backup.rename(output)
                raise
            shutil.rmtree(backup)
        else:
            stage.rename(output)
        if args.zip:
            args.zip.parent.mkdir(parents=True, exist_ok=True)
            handle, temporary_zip = tempfile.mkstemp(prefix=".runtime-package-", suffix=".zip", dir=args.zip.parent)
            os.close(handle)
            try:
                with zipfile.ZipFile(temporary_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                    for path in sorted(output.rglob("*")):
                        if path.is_file():
                            info = zipfile.ZipInfo(path.relative_to(output).as_posix(), (1980, 1, 1, 0, 0, 0))
                            info.create_system = 3
                            info.external_attr = 0o644 << 16
                            archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
                os.replace(temporary_zip, args.zip)
            finally:
                if os.path.exists(temporary_zip):
                    os.unlink(temporary_zip)
        print(f"Packaged {len(images)} runtime libraries in {output}; execution and device VA are unverified.")
    finally:
        if stage.exists():
            shutil.rmtree(stage)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Runtime package failed: {error}")
