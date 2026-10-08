#!/usr/bin/env python3
"""Fail before the native build if the pinned guest-state bridge ABI changed."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fex", type=Path, default=ROOT / "upstreams/FEX")
    parser.add_argument("--madeira", type=Path, default=ROOT / "upstreams/Madeira")
    args = parser.parse_args()
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
