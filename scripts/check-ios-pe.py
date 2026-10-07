#!/usr/bin/env python3
"""Record freshly built ARM64EC components, then verify the app embeds them."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
NAMES = ("ntdll.dll", "xtajit64.dll", "winevulkan.dll", "vulkan-1.dll")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def component(path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"Not a PE binary: {path}")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE header: {path}")
    machine = struct.unpack_from("<H", data, pe + 4)[0]
    # Linked ARM64EC PE images use the AMD64 machine value with CHPE metadata;
    # A641 identifies ARM64EC object files. Never accept a plain x64 DLL here.
    if machine not in (0x8664, 0xA641, 0xA64E):
        raise ValueError(f"Unexpected machine={machine:#x}: {path}")
    reader = shutil.which("llvm-readobj")
    if not reader:
        raise ValueError("llvm-readobj from the pinned llvm-mingw toolchain is required")
    config = subprocess.check_output([reader, "--coff-load-config", str(path)], text=True)
    code_map = re.search(r"CodeMap\s*\[(.*?)\]", config, re.S)
    if code_map is None or not re.search(r"\bARM64EC\b", code_map.group(1)):
        raise ValueError(f"PE image has no verified ARM64EC code range: {path}")
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "machine": hex(machine), "arm64ec_code_map": True}


def source_state(checkouts=None):
    checkouts = checkouts or ROOT / "upstreams"
    inputs = sorted(ROOT.glob("patches/*/*.patch")) + [ROOT / "scripts/m3-madeira-pe.sh"]
    return {
        "pins": {name: subprocess.check_output(
            ["git", "-C", str(checkouts / name), "rev-parse", "HEAD"], text=True).strip()
            for name in ("FEX", "wine", "Madeira")},
        "inputs": {str(path.relative_to(ROOT)): digest(path) for path in inputs},
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("record", "verify"))
    parser.add_argument("--app", type=pathlib.Path)
    parser.add_argument("--checkouts", type=pathlib.Path, default=ROOT / "upstreams",
                        help="Isolated upstream directory used for this build")
    parser.add_argument("--manifest", type=pathlib.Path,
                        default=ROOT / "build/ios-runtime/pe-components.json")
    args = parser.parse_args()
    if args.mode == "record":
        farm = args.checkouts / "Madeira/app/Madeira/arm64ec-windows"
        record = {"schema": 1, "sources": source_state(args.checkouts),
                  "components": {name: component(farm / name) for name in NAMES}}
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(record, indent=2) + "\n")
        print(f"Recorded actual PE build outputs: {args.manifest}")
    else:
        if args.app is None:
            parser.error("verify requires --app")
        record = json.loads(args.manifest.read_text())
        if record["sources"] != source_state(args.checkouts):
            raise ValueError("Source inputs changed after the recorded PE build; rebuild PE components")
        for name in NAMES:
            actual = component(args.app / "arm64ec-windows" / name)
            if actual != record["components"][name]:
                raise ValueError(f"App contains stale or incorrect PE component: {name}")
            print(f"Verified embedded {name}: {actual['sha256']}")


if __name__ == "__main__":
    main()
