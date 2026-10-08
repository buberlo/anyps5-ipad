#!/usr/bin/env python3
"""Verify production canonical rect-list eligibility with synthetic shaders.

Compiles only ShaderValidation, RectListShaders and SpirvModule, with ASan/UBSan.
Uses local glslangValidator and spirv-val; no full HLE/recompiler build, device,
network, downloads, game data or GitHub Actions. Passing tests establish static
shader provenance only, not dynamic resource alias safety or GPU equivalence.
"""
from pathlib import Path
import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=ROOT / "upstreams/AnyPS5")
parser.add_argument("--build", type=Path, help="Keep the small compiler/fixture outputs in this directory.")
parser.add_argument("--glslang", default="glslangValidator")
parser.add_argument("--spirv-val", default="spirv-val")
args = parser.parse_args()
source = args.source.resolve()
for tool in (args.glslang, args.spirv_val):
    if not shutil.which(tool):
        sys.exit(f"Missing local prerequisite: {tool}")

VERTEX = """#version 450
out gl_PerVertex { vec4 gl_Position; };
layout(location=0) out vec4 parameter;
layout(set=0,binding=2,std430) readonly buffer VertexData { float value; };
void main() {
    vec2 uv = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
    parameter = vec4(uv, value, 1);
    gl_Position = vec4(uv * 2.0 - 1.0, 0, 1);
}
"""
FRAGMENT = """#version 450
layout(location=0) in vec4 parameter;
layout(location=0) out vec4 color;
layout(set=0,binding=5,std430) readonly buffer FragmentData { float value; };
void main() { color = parameter + vec4(value); }
"""
CONTROL = """#version 450
layout(vertices=4) out;
void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID % 3].gl_Position;
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0]=1; gl_TessLevelOuter[1]=1;
        gl_TessLevelOuter[2]=1; gl_TessLevelOuter[3]=1;
        gl_TessLevelInner[0]=1; gl_TessLevelInner[1]=1;
    }
}
"""
EVALUATION = """#version 450
layout(quads, equal_spacing, cw) in;
void main() { gl_Position = gl_in[0].gl_Position; }
"""


def run(command, **kwargs):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        sys.stderr.write(result.stdout)
        raise subprocess.CalledProcessError(result.returncode, command)
    return result.stdout


def check(build):
    build.mkdir(parents=True, exist_ok=True)
    binary = build / "check"
    recompiler = source / "core/shader/recompiler"
    includes = [source / "core/libs", recompiler,
                source / "3rdparty/Vulkan-Headers/include", source / "3rdparty/SPIRV-Headers/include"]
    includes += sorted(recompiler.glob("*/include"))
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    command = compiler + ["-std=c++20", "-O1", "-g", "-UNDEBUG", "-DANYPS5_ENABLE_SPIRV_TOOLS=0",
                          "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                          "-ffunction-sections", "-fdata-sections"]
    command += ["-I" + str(path) for path in includes]
    command += [str(ROOT / "tools/checks/msaa_rectlist_shader_validation.cpp"),
                str(source / "core/libs/prx/libSceAgcDriver/Graphics/src/ShaderValidation.cpp"),
                str(recompiler / "SpirvBackend/src/RectListShaders.cpp"),
                str(recompiler / "SpirvBackend/src/SpirvModule.cpp")]
    command += ["-Wl,-dead_strip"] if sys.platform == "darwin" else ["-Wl,--gc-sections"]
    command += ["-o", str(binary)]
    run(command)

    def compile_fixture(name, stage, text):
        path = build / (name + "." + stage)
        path.write_text(text)
        target = path.with_suffix(".spv")
        run([args.glslang, "-V", "--target-env", "vulkan1.1", "-Od", str(path), "-o", str(target)])
        run([args.spirv_val, "--target-env", "vulkan1.2", str(target)])
        return target

    fixtures = [
        compile_fixture("vertex", "vert", VERTEX),
        compile_fixture("fragment", "frag", FRAGMENT),
        compile_fixture("write_vertex", "vert", VERTEX.replace("readonly buffer", "buffer").replace("parameter =", "value = 1; parameter =")),
        compile_fixture("write_fragment", "frag", FRAGMENT.replace("readonly buffer", "buffer").replace("color =", "value = 1; color =")),
        compile_fixture("sample_fragment", "frag", FRAGMENT.replace("color = parameter", "color = vec4(float(gl_SampleID)) + parameter")),
        compile_fixture("arbitrary_control", "tesc", CONTROL),
        compile_fixture("arbitrary_evaluation", "tese", EVALUATION),
    ]
    output = build / "generated"
    output.mkdir(exist_ok=True)
    # Retained output directories may contain a previous matrix. Do not count or
    # validate stale modules as results from this invocation.
    for path in output.glob("*.spv"):
        path.unlink()
    print(run([str(binary)] + list(map(str, fixtures)) + [str(output)]), end="")
    generated = sorted(output.glob("*.spv"))
    for path in generated:
        run([args.spirv_val, "--target-env", "vulkan1.2", str(path)])
    print(f"{len(fixtures)} original fixtures and {len(generated)} production-generated/mutated modules independently passed spirv-val.")
    print("Only the three focused production translation units were compiled; dynamic alias/GPU qualification remains separate.")


if args.build:
    check(args.build.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="anyps5-rectlist-shaders-") as scratch:
        check(Path(scratch))
