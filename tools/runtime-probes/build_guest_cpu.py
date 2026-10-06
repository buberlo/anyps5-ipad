#!/usr/bin/env python3
"""Build an original NID-importing ELF/PE for HLE, thread and guest TLS checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/demo"))
from build_demo import nid, run

IMPORTS = [
    ("sceKernelWrite", "i64", "int, const void*, usize"),
    ("sceKernelUsleep", "int", "unsigned"),
    ("sceKernelGetProcessTime", "u64", "void"),
    ("scePthreadCreate", "int", "void**, const void*, ThreadEntry, void*, const char*"),
    ("scePthreadJoin", "int", "void*, void**"),
    ("scePthreadKeyCreate", "int", "int*, KeyDestructor"),
    ("scePthreadKeyDelete", "int", "int"),
    ("scePthreadGetspecific", "void*", "int"),
    ("scePthreadSetspecific", "int", "int, void*"),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build/guest-cpu")
    parser.add_argument("--relinker", type=Path, default=ROOT / "build/host-tools/build/relinker/relinker")
    parser.add_argument("--elf-only", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    stubs = out / "link-only-imports"
    stubs.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("ELF_CC", "clang")
    ld = os.environ.get("ELF_LD") or shutil.which("ld.lld")
    if not ld:
        raise SystemExit("Set ELF_LD to an ELF-compatible ld.lld or rust-lld")
    linker = [ld] + (["-flavor", "gnu"] if Path(ld).stem in ("lld", "rust-lld") else [])
    compiler = [cc, "--target=x86_64-unknown-linux-gnu", "-ffreestanding", "-fPIE", "-fno-stack-protector",
                "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-fno-builtin", "-mno-red-zone"]
    declarations, assembly, imports = [], [".text"], []
    for name, result, parameters in IMPORTS:
        encoded = nid(name)
        declarations.append(f'extern {result} {name}({parameters}) __asm__("{encoded}");')
        assembly += [f'.globl "{encoded}"', f'.type "{encoded}",@function', f'"{encoded}":', "  ud2"]
        imports.append({"symbol": name, "nid": encoded, "library": "libkernel.prx"})
    (out / "imports.h").write_text("\n".join(declarations) + "\n")
    (stubs / "libkernel.s").write_text("\n".join(assembly) + "\n")
    run([cc, "--target=x86_64-unknown-linux-gnu", "-c", stubs / "libkernel.s", "-o", stubs / "libkernel.o"])
    run(linker + ["-m", "elf_x86_64", "-shared", "--hash-style=sysv", "-soname", "libkernel.prx",
                  stubs / "libkernel.o", "-o", stubs / "libkernel.prx"])
    source = ROOT / "tools/runtime-probes"
    run(compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fvisibility=hidden", "-I", out,
                    "-c", source / "guest_cpu.c", "-o", out / "guest_cpu.o"])
    run(compiler + ["-c", source / "guest_tls.S", "-o", out / "guest_tls.o"])
    elf, pe = out / "guest-cpu.elf", out / "guest-cpu.exe"
    # PIE resolves local-exec TLS relocations while preserving public PLT imports.
    run(linker + ["-m", "elf_x86_64", "-pie", "--no-dynamic-linker", "-z", "now", "--hash-style=sysv",
                  "--no-undefined", "-e", "guest_cpu_entry", "-o", elf, out / "guest_cpu.o", out / "guest_tls.o",
                  "--no-as-needed", stubs / "libkernel.prx"])
    sources = ["tools/runtime-probes/guest_cpu.c", "tools/runtime-probes/guest_tls.S",
               "tools/runtime-probes/build_guest_cpu.py", "tools/demo/build_demo.py"]
    manifest = {"schema": 1, "kind": "original_synthetic_guest_cpu", "runtime_verified": False,
                "hard_timeout_seconds": 90, "imports": imports,
                "coverage": ["SysV integer registers and stack arguments", "public sce kernel HLE calls",
                             "two HLE-created threads calling guest callbacks", "9216-byte guest stack frame",
                             "ELF PT_TLS initialized data and zero-fill on three threads",
                             "local-exec TLS via FS:0 and TPOFF address calculation",
                             "pthread key isolation and destructor callbacks", "64-bit concurrent atomic increments"],
                "not_covered": ["general-dynamic TLS", "arbitrary compiler-emitted FS stores", "C++ exceptions",
                                "AVX/SSE instruction correctness", "arbitrary PRX dynamic loading"],
                "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources},
                "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest()}
    if not args.elf_only:
        run([args.relinker, "--windows", "--to-intel", "--skip-sce-module", elf, pe])
        manifest["pe_sha256"] = hashlib.sha256(pe.read_bytes()).hexdigest()
    (out / "guest-cpu-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"CPU guest built in {out}; execution not verified. Link-only stubs must not ship.")


if __name__ == "__main__":
    main()
