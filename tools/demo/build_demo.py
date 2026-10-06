#!/usr/bin/env python3
"""Compile an original SysV x86-64 ELF with NID imports, then relink to PE.

The tiny import DSOs are linker inputs only and must NEVER be shipped as HLE
libraries. The runtime requires the real NID-patched AnyPS5 PRX closure.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


IMPORTS = {
    "libc.prx": [("exit", "void", "int")],
    "libkernel.prx": [
        ("sceKernelWrite", "i64", "int, const void*, usize"),
        ("sceKernelUsleep", "int", "unsigned"),
        ("sceKernelGetProcessTime", "u64", "void"),
        ("sceKernelAllocateDirectMemory", "int", "i64, i64, usize, usize, int, i64*"),
        ("sceKernelMapDirectMemory", "int", "void**, usize, int, int, i64, usize"),
    ],
    "libSceAgc.prx": [("sceAgcCreateShader", "int", "void**, void*, const volatile void*")],
    "libSceAgcDriver.prx": [("sceAgcDriverSubmitDcb", "int", "const void*")],
    "libSceVideoOut.prx": [
        ("sceVideoOutOpen", "int", "int, int, int, const void*"),
        ("sceVideoOutSetBufferAttribute2", "void", "void*, u64, u32, u32, u32, u64, u32, u64"),
        ("sceVideoOutRegisterBuffers2", "int", "int, int, int, const void*, int, const void*, int, void*"),
        ("sceVideoOutSubmitFlip", "int", "int, int, int, i64"),
        ("sceVideoOutGetFlipStatus", "int", "int, void*"),
        ("sceVideoOutClose", "int", "int"),
    ],
    "libScePad.prx": [
        ("scePadInit", "int", "void"),
        ("scePadOpen", "int", "int, int, int, const void*"),
        ("scePadReadState", "int", "int, void*"),
    ],
}


def nid(name):
    # Same public NID encoding as pinned AnyPS5's nid/src/NidCompute.cpp.
    suffix = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")
    digest = hashlib.sha1(name.encode() + suffix).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip("=").replace("/", "-")


def run(args):
    subprocess.run([str(x) for x in args], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--relinker", type=Path)
    parser.add_argument("--elf-only", action="store_true")
    parser.add_argument("--profile", choices=("smoke", "acceptance"), default="acceptance")
    parser.add_argument("--frames", type=int)
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--seconds", type=int)
    args = parser.parse_args()
    width, height, frames, seconds = (320, 192, 120, 30) if args.profile == "smoke" else (1280, 720, 36000, 600)
    args.width = args.width if args.width is not None else width
    args.height = args.height if args.height is not None else height
    args.frames = args.frames if args.frames is not None else frames
    args.seconds = args.seconds if args.seconds is not None else seconds
    if not 1 <= args.frames <= 36000:
        parser.error("--frames must be in 1..36000")
    if not 1 <= args.seconds <= 600:
        parser.error("--seconds must be in 1..600")
    if not 320 <= args.width <= 1920 or not 192 <= args.height <= 1080 or args.width * args.height % 32:
        parser.error("dimensions must be 320..1920 x 192..1080 with a pixel count divisible by 32")
    root = Path(__file__).resolve().parents[2]
    out = (args.output or root / "build/demo").resolve()
    linker_inputs = out / "link-only-imports"
    linker_inputs.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("ELF_CC", "clang")
    ld = os.environ.get("ELF_LD") or shutil.which("ld.lld")
    if not ld:
        candidates = list((root / "build/toolchains/rustup/toolchains").glob("*/lib/rustlib/*/bin/rust-lld"))
        if candidates:
            ld = str(sorted(candidates)[0])
    if not ld:
        raise SystemExit("ELF linker missing: set ELF_LD to ld.lld or rust-lld")
    linker = [ld] + (["-flavor", "gnu"] if Path(ld).stem in ("rust-lld", "lld") else [])
    compiler = [cc, "--target=x86_64-unknown-linux-gnu", "-ffreestanding", "-fPIC", "-fno-stack-protector",
                "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-fno-builtin", "-mno-red-zone"]
    declarations = []
    manifest = {"schema": 1, "kind": "original_synthetic_guest", "profile": args.profile, "frame_limit": args.frames,
                "time_limit_seconds": args.seconds, "dimensions": [args.width, args.height],
                "target_fps": 60, "acceptance_minimum_seconds": 600,
                "acceptance_minimum_average_fps": 30,
                "timing_scope": "Guest monotonic frame timings include draw, GPU readback, FlipStatus acknowledgment and pacing; not display timestamps.",
                "complete_status_scope": "execution only; device foreground, visible presentation and user input require independent acceptance",
                "controls": {"move": "left stick or dpad", "serve": "cross", "exit": "circle"},
                "imports": [], "runtime_verified": False,
                "source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                                  for name in ("tools/demo/demo.c", "tools/demo/build_demo.py")}}
    stub_paths = []
    for library, functions in IMPORTS.items():
        assembly = [".text"]
        for name, result, parameters in functions:
            encoded = nid(name)
            declarations.append(f'extern {result} {name}({parameters}) __asm__("{encoded}");')
            assembly += [f'.globl "{encoded}"', f'.type "{encoded}",@function', f'"{encoded}":', "  ud2"]
            manifest["imports"].append({"symbol": name, "nid": encoded, "library": library})
        source = linker_inputs / (library + ".s")
        obj = linker_inputs / (library + ".o")
        stub = linker_inputs / library
        source.write_text("\n".join(assembly) + "\n")
        run([cc, "--target=x86_64-unknown-linux-gnu", "-c", source, "-o", obj])
        run(linker + ["-m", "elf_x86_64", "-shared", "--hash-style=sysv", "-soname", library, obj, "-o", stub])
        stub_paths.append(stub)
    (out / "imports.h").write_text("\n".join(declarations) + "\n")
    obj = out / "demo.o"
    elf = out / "demo.elf"
    run(compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fvisibility=hidden",
                    f"-DDEMO_FRAME_LIMIT={args.frames}", f"-DDEMO_WIDTH={args.width}",
                    f"-DDEMO_HEIGHT={args.height}", f"-DDEMO_SECONDS={args.seconds}", "-I", out,
                    f"-DDEMO_SMOKE_PROFILE={int(args.profile == 'smoke')}",
                    "-c", root / "tools/demo/demo.c", "-o", obj])
    run(linker + ["-m", "elf_x86_64", "-shared", "-Bsymbolic", "-z", "now", "--hash-style=sysv",
                  "--no-undefined", "-e", "demo_entry", "-o", elf, obj, "--no-as-needed", *stub_paths])
    manifest["elf_sha256"] = hashlib.sha256(elf.read_bytes()).hexdigest()
    if not args.elf_only:
        relinker = args.relinker or root / "build/host-tools/build/relinker/relinker"
        if not relinker.is_file():
            raise SystemExit(f"ELF built; relinker missing: {relinker}")
        pe = out / "demo.exe"
        run([relinker, "--windows", "--to-intel", "--skip-sce-module", elf, pe])
        manifest["pe_sha256"] = hashlib.sha256(pe.read_bytes()).hexdigest()
    (out / "demo-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Original demo built in {out}; not executed. Runtime HLE files are not the link-only-imports.")


if __name__ == "__main__":
    main()
