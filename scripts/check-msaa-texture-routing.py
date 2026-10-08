#!/usr/bin/env python3
"""Build the production recompiler; verify grouped sample fetches with own fixtures.

Uses local CMake, C++ and spirv-val. No device, downloads, game data or Actions.
The native library build is Release. ASan/UBSan cover the focused decoder and
fixture translation units; this is not a fully sanitized recompiler build.
"""
from pathlib import Path
import argparse
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=ROOT / "upstreams/AnyPS5")
parser.add_argument("--build", type=Path, default=ROOT / "build/msaa-texture-routing-native")
parser.add_argument("--spirv-val", default="spirv-val")
parser.add_argument("--jobs", default="3")
args = parser.parse_args()
source = args.source.resolve()
build = args.build.resolve()
for tool in ("cmake", args.spirv_val):
    if not shutil.which(tool): sys.exit("Missing local prerequisite: " + tool)

def run(command, **kwargs):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        sys.stderr.write(result.stdout)
        raise subprocess.CalledProcessError(result.returncode, command)
    return result.stdout

def verify_routing(path):
    # Inspect actual production-emitted instructions. Evaluate their integer
    # coordinate computation independently from the C++ mapping helper.
    words = struct.unpack("<" + "I" * (path.stat().st_size // 4), path.read_bytes())
    values, fetches, ms_arrays, queries = {}, [], 0, 0
    one_mip_return = False
    cursor = 5
    while cursor < len(words):
        length, op = words[cursor] >> 16, words[cursor] & 65535
        operands = words[cursor + 1:cursor + length]
        if op == 43: values[operands[1]] = operands[2]  # OpConstant
        elif op in (128, 132, 134, 137):  # IAdd, IMul, UDiv, UMod
            left, right = values.get(operands[2]), values.get(operands[3])
            if isinstance(left, int) and isinstance(right, int):
                values[operands[1]] = {128: lambda: left + right, 132: lambda: left * right,
                                      134: lambda: left // right, 137: lambda: left % right}[op]()
        elif op == 80:
            if queries and len(operands) == 6 and values.get(operands[-1]) == 1:
                one_mip_return = True
            if all(operand in values for operand in operands[2:]):
                values[operands[1]] = tuple(values[operand] for operand in operands[2:])
        elif op == 95: fetches.append(operands)  # OpImageFetch
        elif op == 25 and operands[2] == 1 and operands[4] == 1 and operands[5] == 1:
            ms_arrays += 1  # OpTypeImage Dim2D Arrayed=1 MS=1
        elif op == 104: queries += 1  # OpImageQuerySize
        cursor += length
    mode, count, sample = path.stem.split("-")
    count, sample = int(count), int(sample)
    native, layer = min(count, 4), 2 if mode == "array" else 0
    expected_coordinate = (1, 2, layer * (count // native) + sample // native)
    if len(fetches) != 1 or ms_arrays != 1 or queries != 1 or not one_mip_return:
        raise AssertionError("missing real MS-array fetch/query: " + path.name)
    fetch = fetches[0]
    if fetch[4] != 64 or values.get(fetch[3]) != expected_coordinate or values.get(fetch[5]) != sample % native:
        raise AssertionError("production SPIR-V sample address mismatch: " + path.name)

build.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="anyps5-msaa-routing-") as scratch_name:
    scratch = Path(scratch_name)
    cmake_source = build / ".source"
    cmake_source.mkdir(exist_ok=True)
    cmake = cmake_source / "CMakeLists.txt"
    cmake.write_text('''cmake_minimum_required(VERSION 3.20)
project(AnyPS5MsaaRouting LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(PROJECT_SOURCE_DIR "${ANYPS5_SOURCE}")
set(ANYPS5_ENABLE_SPIRV_TOOLS OFF)
add_library(host_runtime INTERFACE)
add_subdirectory("${ANYPS5_SOURCE}/core/shader/recompiler" recompiler)
add_shader_recompiler(shader_recompiler host_runtime)
''')
    run(["cmake", "-S", str(cmake_source), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release", "-DANYPS5_SOURCE=" + str(source)])
    run(["cmake", "--build", str(build), "--target", "shader_recompiler", "-j", args.jobs])
    recompiler = source / "core/shader/recompiler"
    includes = [source / "core/libs", recompiler, source / "3rdparty/Vulkan-Headers/include", source / "3rdparty/SPIRV-Headers/include"]
    includes += sorted(recompiler.glob("*/include"))
    libraries = sorted(build.glob("**/libshader_recompiler.a")) + sorted(build.glob("3rdparty/glslang/**/*.a"))
    binary = scratch / "check"
    command = shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++20", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections"]
    command += ["-I" + str(path) for path in includes]
    command += [str(ROOT / "tools/checks/msaa_texture_sample_routing.cpp"), str(source / "core/libs/prx/libSceAgcDriver/Graphics/src/GuestTextureResource.cpp")]
    command += list(map(str, libraries)) + (["-Wl,-dead_strip"] if sys.platform == "darwin" else ["-Wl,--gc-sections"]) + ["-o", str(binary)]
    run(command)
    # A Release library and sanitized C++ caller instantiate some STL methods
    # separately. Disable container annotations for that mixed boundary; normal
    # heap/stack bounds and UB instrumentation remain active for the caller.
    environment = dict(os.environ)
    environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_container_overflow=0"
    shaders = scratch / "spirv"
    print(run([str(binary), str(shaders)], env=environment), end="")
    for path in sorted(shaders.glob("*.spv")):
        run([args.spirv_val, "--target-env", "vulkan1.1", str(path)])
        verify_routing(path)
    print("All production SPIR-V modules independently validate and route every logical sample correctly.")
