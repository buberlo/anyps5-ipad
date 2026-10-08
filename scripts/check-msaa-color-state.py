#!/usr/bin/env python3
"""Run production color sample decoding and transfer regressions with sanitizers.

The native macOS memory adapter checks actual OS mappings. Wine write watch,
guest page cache and deferred GPU writes remain separate Windows/iPad gates.
The same regression source is included in agc_driver_graphics_tests on Windows.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--anyps5-source", type=Path, default=root / "upstreams/AnyPS5")
args = parser.parse_args()
if sys.platform != "darwin":
    parser.error("Use agc_driver_graphics_tests on Windows; this native adapter is macOS-only")
source = args.anyps5_source.resolve()
libs = source / "core/libs"
recompiler = source / "core/shader/recompiler"
includes = [libs, libs / "prx/libc/include", recompiler,
            source / "3rdparty/Vulkan-Headers/include", source / "3rdparty/SPIRV-Headers/include"]
includes += [recompiler / area / "include" for area in
             ("ControlFlow", "IntermediateRepresentation", "Optimization", "RdnaDecoder", "Translation")]
production = ["State.cpp", "ColorTargetLayout.cpp", "ColorTargetTransfer.cpp",
              "TextureTiling.cpp", "TextureFormat.cpp", "DccMetadata.cpp", "DepthSurface.cpp"]
with tempfile.TemporaryDirectory(prefix="anyps5-color-state-") as temp:
    executable = Path(temp) / "check"
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++20", "-O1", "-g", "-UNDEBUG", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections", "-Wl,-dead_strip",
        "-DAPS5_COLOR_SAMPLES_TEST_STANDALONE", "-DAPS5_ENABLE_TIMING_LOG=0", "-Wno-return-type-c-linkage",
        *["-I" + str(path) for path in includes],
        str(libs / "prx/libSceAgcDriver/tests/ColorTargetSamples.cpp"),
        *[str(libs / "prx/libSceAgcDriver/Graphics/src" / name) for name in production],
        str(root / "tools/checks/color_target_samples_memory_adapter.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=60)
print("Native mapped-memory adapter only; Wine guest tracker and GPU rendering are not qualified.")
