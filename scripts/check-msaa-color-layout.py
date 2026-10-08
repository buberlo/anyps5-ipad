#!/usr/bin/env python3
"""Check actual color/sample layout code against a separately built AMD AddrLib.

Provide AMD's source headers and native library explicitly; this script does not
download dependencies or use game data. The reference checkpoint is PAL
c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/imported/addrlib.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--addrlib-source", type=Path, required=True)
parser.add_argument("--addrlib-library", type=Path, required=True)
parser.add_argument("--anyps5-source", type=Path, default=root / "upstreams/AnyPS5")
args = parser.parse_args()
source = args.anyps5_source.resolve()
with tempfile.TemporaryDirectory(prefix="anyps5-color-samples-") as scratch:
    binary = Path(scratch) / "test"
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++20", "-O1", "-g", "-UNDEBUG", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(source / "core/libs"), "-I" + str(source / "core/shader/recompiler"),
        "-I" + str(args.addrlib_source.resolve() / "inc"),
        str(root / "tools/checks/msaa_color_layout_reference.cpp"),
        str(source / "core/libs/prx/libSceAgcDriver/Graphics/src/ColorTargetLayout.cpp"),
        str(args.addrlib_library.resolve()), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=180)
