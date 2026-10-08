#!/usr/bin/env python3
"""Fail before the native build if the pinned guest-state bridge ABI changed."""
import argparse
from pathlib import Path
import subprocess
import re
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fex", type=Path, default=ROOT / "upstreams/FEX")
    parser.add_argument("--madeira", type=Path, default=ROOT / "upstreams/Madeira")
    args = parser.parse_args()
    # This flag advertises that FEX reconstructed upper YMM state for an AV.
    # Its value is independent of the CPUState field offsets checked below.
    header = (args.fex / "Source/Windows/include/winnt.h").read_text()
    flags = re.findall(r"^#define\s+CONTEXT_ARM64_FEX_YMMSTATE\s+\(CONTEXT_ARM64\s*\|\s*(0x[0-9a-fA-F]+)\)", header, re.MULTILINE)
    if len(flags) != 1 or int(flags[0], 16) != 0x40:
        raise RuntimeError("Pinned FEX upper-YMM context flag no longer matches the native bridge")
    with tempfile.TemporaryDirectory(prefix="aps5-flags-layout-") as temporary:
        executable = Path(temporary) / "check-layout"
        subprocess.run([
            "xcrun", "clang++", "-std=c++20", "-DFMT_HEADER_ONLY",
            "-I" + str(args.fex.resolve() / "FEXCore/include"),
            "-I" + str(args.fex.resolve() / "External/fmt/include"),
            "-I" + str(args.madeira.resolve() / "build/ntdll-unix"),
            str(ROOT / "tools/checks/check_async_flags_layout.cpp"), "-o", str(executable)
        ], check=True)
        subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    main()
