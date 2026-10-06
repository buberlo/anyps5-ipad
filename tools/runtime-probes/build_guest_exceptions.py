#!/usr/bin/env python3
"""Build a real ELF exception/heap test, retaining DWARF exception metadata."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/demo"))
from build_demo import nid, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build/guest-exceptions")
    parser.add_argument("--relinker", type=Path, default=ROOT / "build/host-tools/build/relinker/relinker")
    parser.add_argument("--elf-only", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    stubs = out / "link-only-imports"
    stubs.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("ELF_CC", "clang")
    ld = os.environ.get("ELF_LD") or shutil.which("ld.lld")
    objcopy = os.environ.get("ELF_OBJCOPY") or shutil.which("llvm-objcopy")
    nm = os.environ.get("ELF_NM") or shutil.which("llvm-nm")
    if not all((ld, objcopy, nm)):
        raise SystemExit("Set ELF_LD, ELF_OBJCOPY and ELF_NM to LLVM tools with ELF support")
    linker = [ld] + (["-flavor", "gnu"] if Path(ld).stem in ("lld", "rust-lld") else [])
    source = ROOT / "tools/runtime-probes/guest_exceptions.cpp"
    obj = out / "guest-exceptions.o"
    run([cc, "--target=x86_64-unknown-linux-gnu", "-std=c++17", "-ffreestanding", "-fPIC", "-fexceptions",
         "-funwind-tables", "-fno-stack-protector", "-fno-builtin", "-mno-red-zone", "-fvisibility=hidden",
         "-O1", "-Wall", "-Wextra", "-Werror", "-c", source, "-o", obj])
    symbols = subprocess.check_output([nm, "--undefined-only", "--format=posix", str(obj)], text=True)
    names = sorted(line.split()[0] for line in symbols.splitlines() if line.strip())
    allowed = {"sceKernelWrite", "malloc", "free", "__cxa_allocate_exception", "__cxa_throw",
               "__cxa_begin_catch", "__cxa_end_catch", "__cxa_rethrow", "__cxa_free_exception",
               "__gxx_personality_v0", "_Unwind_Resume", "_ZSt9terminatev",
               "_ZTVN10__cxxabiv117__class_type_infoE"}
    if set(names) - allowed:
        raise SystemExit(f"Unexpected ABI imports: {sorted(set(names) - allowed)}")
    rename = out / "rename-symbols.txt"
    rename.write_text("".join(f"{name} {nid(name)}\n" for name in names))
    run([objcopy, "--redefine-syms=" + str(rename), obj])
    imports, inputs = [], []
    for library in ("libkernel.prx", "libc.prx"):
        assembly = []
        for name in names:
            if (name == "sceKernelWrite") != (library == "libkernel.prx"):
                continue
            encoded = nid(name)
            is_data = name.startswith("_ZTV")
            assembly += [".data" if is_data else ".text", f'.globl "{encoded}"',
                         f'.type "{encoded}",@' + ("object" if is_data else "function"), f'"{encoded}":',
                         "  .zero 88" if is_data else "  ud2"]
            if is_data:
                assembly.append(f'.size "{encoded}",88')
            imports.append({"symbol":name,"nid":encoded,"library":library,"kind":"data" if is_data else "function"})
        asm, stub_obj, dso = (stubs / (library + suffix) for suffix in (".s", ".o", ""))
        asm.write_text("\n".join(assembly) + "\n")
        run([cc, "--target=x86_64-unknown-linux-gnu", "-c", asm, "-o", stub_obj])
        run(linker + ["-m", "elf_x86_64", "-shared", "--hash-style=sysv", "-soname", library, stub_obj, "-o", dso])
        inputs.append(dso)
    elf, pe = out / "guest-exceptions.elf", out / "guest-exceptions.exe"
    run(linker + ["-m", "elf_x86_64", "-shared", "-Bsymbolic", "-z", "now", "--hash-style=sysv",
                  "--eh-frame-hdr", "--no-undefined", "-e", "guest_exceptions_entry", "-o", elf, obj,
                  "--no-as-needed", *inputs])
    sources = ["tools/runtime-probes/guest_exceptions.cpp", "tools/runtime-probes/build_guest_exceptions.py", "tools/demo/build_demo.py"]
    manifest = {"schema":1,"kind":"original_synthetic_guest_exceptions","runtime_verified":False,
                "hard_timeout_seconds":90,"imports":imports,
                "coverage":["libc heap allocation and byte readback", "ELF DWARF exception unwind",
                            "typed class catch and nested rethrow", "destructors during stack unwinding"],
                "not_covered":["foreign exceptions", "exception across guest thread boundary", "arbitrary RTTI hierarchies"],
                "source_sha256":{name:hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources},
                "elf_sha256":hashlib.sha256(elf.read_bytes()).hexdigest()}
    if not args.elf_only:
        run([args.relinker, "--windows", "--to-intel", "--skip-sce-module", elf, pe])
        manifest["pe_sha256"] = hashlib.sha256(pe.read_bytes()).hexdigest()
    (out / "guest-exceptions-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Exception guest built in {out}; runtime has not been verified.")


if __name__ == "__main__":
    main()
