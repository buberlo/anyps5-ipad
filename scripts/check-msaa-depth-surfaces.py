#!/usr/bin/env python3
"""Capture real DepthSurface allocation/cache commands against explicit mocks.

This compiles actual production code with ASan/UBSan. It does not execute a GPU
or qualify command scheduling, guest transfers or depth/stencil rendering.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--anyps5-source", type=Path, default=root / "upstreams/AnyPS5")
args = parser.parse_args()
source = args.anyps5_source.resolve()
includes = [source / "core/libs", source / "core/shader/recompiler", source / "3rdparty/Vulkan-Headers/include"]
includes += sorted((source / "core/shader/recompiler").glob("*/include"))
with tempfile.TemporaryDirectory(prefix="anyps5-depth-surfaces-") as scratch:
    binary = Path(scratch) / "test"
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++20", "-O1", "-g", "-UNDEBUG", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        *["-I" + str(path) for path in includes],
        str(root / "tools/checks/msaa_depth_surface_commands.cpp"),
        str(source / "core/libs/prx/libSceAgcDriver/Graphics/src/DepthSurface.cpp"),
        str(source / "core/libs/prx/libSceAgcDriver/Graphics/src/ColorRenderTarget.cpp"),
        str(source / "core/shader/recompiler/RdnaDecoder/src/RdnaDescriptorFormat.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=60)
