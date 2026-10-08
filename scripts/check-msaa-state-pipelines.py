#!/usr/bin/env python3
"""Check production state decoding and pipeline Vulkan structures with ASan/UBSan.

Descriptor resource and Vulkan calls are explicit mocks. Native mapped guest
memory is checked by the existing macOS adapter. This is not GPU or game proof.
An optional private register capture is consumed in memory and never written.
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
parser.add_argument("--private-registers", type=Path)
args = parser.parse_args()
if sys.platform != "darwin":
    parser.error("The explicit mapped-memory test adapter is macOS-only")
source = args.anyps5_source.resolve()
libs = source / "core/libs"
recompiler = source / "core/shader/recompiler"
includes = [libs, libs / "prx/libc/include", recompiler,
            source / "3rdparty/Vulkan-Headers/include", source / "3rdparty/SPIRV-Headers/include"]
includes += sorted(recompiler.glob("*/include"))
with tempfile.TemporaryDirectory(prefix="anyps5-msaa-pipeline-") as temp:
    scratch = Path(temp)
    boundary = scratch / "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
    boundary.parent.mkdir(parents=True)
    boundary.write_bytes((root / "tools/checks/msaa_pipeline_test_bindings.hpp").read_bytes())
    executable = scratch / "check"
    production = ["State.cpp", "Pipeline.cpp", "ColorTargetLayout.cpp", "TextureTiling.cpp", "TextureFormat.cpp", "DccMetadata.cpp"]
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++20", "-O1", "-g", "-UNDEBUG", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections", "-Wl,-dead_strip",
        "-DAPS5_ENABLE_TIMING_LOG=0", "-Wno-return-type-c-linkage", "-I" + str(scratch),
        *["-I" + str(path) for path in includes],
        str(root / "tools/checks/msaa_state_pipeline_commands.cpp"),
        *[str(libs / "prx/libSceAgcDriver/Graphics/src" / name) for name in production],
        str(root / "tools/checks/color_target_samples_memory_adapter.cpp"),
        "-o", str(executable),
    ], check=True)
    command = [str(executable)]
    if args.private_registers:
        command += [str(args.private_registers.resolve())]
    subprocess.run(command, check=True, timeout=60)
print("Explicit descriptor/Vulkan mocks; no GPU, descriptor dispatch or game execution qualified.")
