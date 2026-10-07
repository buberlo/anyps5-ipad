#!/usr/bin/env python3
"""Run portable HLE contracts on macOS with sanitizers, using production sources.

This is not Windows/SysV ABI qualification. Run the CMake Windows test targets
with the pinned WinLibs compiler for the descriptor/thread/exception boundaries.
No game data, device access or GitHub Actions are involved.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LIBS = ROOT / "upstreams/AnyPS5/core/libs"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--source", type=Path, default=ROOT / "upstreams/AnyPS5",
                        help="Patched isolated AnyPS5 checkout")
    args = parser.parse_args()
    libs = args.source.resolve() / "core/libs"
    if sys.platform != "darwin":
        parser.error("This sanitizer adapter currently supports macOS; use the Windows CMake targets on Windows")
    groups = [
        ("GuestJson", ["libSceJson2/Export.cpp"]),
        ("GuestJson2Initialization", ["libSceJson2/Export.cpp"]),
        ("GuestCompatibilityApis", ["libSceSsl/Export.cpp", "libSceHttp2/Export.cpp",
            "libSceNpEntitlementAccess/Export.cpp", "libSceNpSessionSignaling/Export.cpp",
            "libSceNpManager/Export.cpp", "libSceUserService/Export.cpp", "libSceMsgDialog.native/Export.cpp"]),
    ]
    with tempfile.TemporaryDirectory(prefix="anyps5-hle-contracts-") as directory:
        temp = Path(directory)
        shim = temp / "unsupported.cpp"
        shim.write_text('#include <stdexcept>\nextern "C" void NotImplemented_nid_no_patch(const char* name) { throw std::runtime_error(name); }\n')
        for name, sources in groups:
            output = temp / name
            subprocess.run(shlex.split(args.cxx) + ["-std=c++20", "-g", "-fsanitize=address,undefined",
                "-Wno-return-type-c-linkage", "-Wl,-dead_strip", "-I" + str(libs),
                str(libs / "tests" / (name + ".cpp")), *[str(libs / "prx" / source) for source in sources],
                str(shim), "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True, timeout=30)
            print("PASS production " + name, flush=True)
    print("Portable sanitizer contracts passed; Windows ABI and iPad gameplay remain separate checks.")

if __name__ == "__main__":
    main()
