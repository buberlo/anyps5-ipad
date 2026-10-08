#!/usr/bin/env python3
"""Compile the production split-draw guard; validate synthetic shaders first.

Requires local glslangValidator and spirv-val. Downloads nothing and uses no game
data, GPU, device, network, or GitHub Actions. Tests eligibility, not alias safety.
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
parser.add_argument("--glslang", default="glslangValidator")
parser.add_argument("--spirv-val", default="spirv-val")
args = parser.parse_args()
source = args.source.resolve()
for tool in (args.glslang, args.spirv_val):
    if not shutil.which(tool):
        sys.exit(f"Missing local prerequisite: {tool}")

VERTEX = """#version 450
out gl_PerVertex { vec4 gl_Position; };
layout(location=0) out vec2 uv;
void main() { uv = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1); gl_Position = vec4(uv * 2.0 - 1.0, 0, 1); }
"""
FRAGMENT = """#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
void main() { color = vec4(uv, 0.25, 1); }
"""

def pixel(declaration, body, extensions=""):
    return "#version 450\n" + extensions + "\nlayout(location=0) in vec2 uv;\nlayout(location=0) out vec4 color;\n" + declaration + "\nvoid main() { " + body + " }\n"

fixtures = [
    ("center", FRAGMENT, "pass"),
    ("function_local", pixel("vec4 value(vec2 p) { vec4 v = vec4(p, 0, 1); v.z = 0.5; return v; }", "color = value(uv);"), "pass"),
    ("helper_no_derivatives", pixel("", "if (gl_HelperInvocation) discard; color = vec4(uv, 0, 1);"), "pass"),
    ("read_only_buffer", pixel("layout(set=0,binding=0,std430) readonly buffer Data { float values[]; };", "color = vec4(values[0]);"), "pass"),
    ("storage_write", pixel("layout(set=0,binding=0,std430) buffer Data { float values[]; };", "values[0] = uv.x; color = vec4(uv,0,1);"), "nonlocal storage"),
    ("function_storage_write", pixel("layout(set=0,binding=0,std430) buffer Data { float values[]; };\nvoid store(float x) { values[0] = x; }", "store(uv.x); color=vec4(uv,0,1);"), "nonlocal storage"),
    ("image_write", pixel("layout(set=0,binding=0,rgba8) uniform image2D target;", "imageStore(target, ivec2(0), vec4(uv,0,1)); color=vec4(1);"), "image writes"),
    ("clock", pixel("", "uvec2 tick=clock2x32ARB(); color=vec4(float(tick.x & 255u));", "#extension GL_ARB_shader_clock : require"), "shader clock"),
    ("sample_id", pixel("", "color=vec4(float(gl_SampleID));"), "builtin"),
    ("sample_position", pixel("", "color=vec4(gl_SamplePosition,0,1);"), "builtin"),
    ("sample_mask_input", pixel("", "color=vec4(float(gl_SampleMaskIn[0]));"), "builtin"),
    ("sample_mask_output", pixel("", "gl_SampleMask[0]=1; color=vec4(1);"), "builtin"),
    ("centroid", FRAGMENT.replace("in vec2", "centroid in vec2"), "centroid"),
    ("sample_interpolation", FRAGMENT.replace("in vec2", "sample in vec2"), "interpolation"),
    ("explicit_sample_interpolation", pixel("", "color=vec4(interpolateAtSample(uv, 0),0,1);"), "explicit interpolation"),
    ("subgroup_input", pixel("", "color=vec4(float(gl_SubgroupInvocationID));", "#extension GL_KHR_shader_subgroup_basic : require"), "builtin"),
    ("helper_derivative", pixel("", "if(gl_HelperInvocation) discard; color=vec4(dFdx(uv),0,1);"), "helper coverage"),
    ("helper_implicit_lod", pixel("layout(set=0,binding=0) uniform sampler2D image;", "if(gl_HelperInvocation) discard; color=texture(image,uv);"), "helper coverage"),
    ("memory_barrier", pixel("", "memoryBarrier(); color=vec4(uv,0,1);"), "barrier"),
    ("atomic_add", pixel("layout(set=0,binding=0,std430) buffer Data { uint value; };", "color=vec4(float(atomicAdd(value,0u)));"), "atomic access"),
    ("interlock", pixel("layout(pixel_interlock_ordered) in;", "beginInvocationInterlockARB(); color=vec4(uv,0,1); endInvocationInterlockARB();", "#extension GL_ARB_fragment_shader_interlock : require"), "interlock"),
]

def run(command):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        sys.stderr.write(result.stdout)
        raise subprocess.CalledProcessError(result.returncode, command)
    return result.stdout

with tempfile.TemporaryDirectory(prefix="anyps5-split-shaders-") as scratch:
    scratch = Path(scratch)
    binary = scratch / "check"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    includes = [source / "core/libs", source / "core/shader/recompiler", source / "3rdparty/Vulkan-Headers/include", source / "3rdparty/SPIRV-Headers/include"]
    link = ["-Wl,-dead_strip"] if sys.platform == "darwin" else ["-Wl,--gc-sections"]
    run(compiler + ["-std=c++20", "-O1", "-g", "-UNDEBUG", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections"] + ["-I" + str(p) for p in includes] + [str(ROOT / "tools/checks/split_draw_shader_validation.cpp"), str(source / "core/libs/prx/libSceAgcDriver/Graphics/src/ShaderValidation.cpp")] + link + ["-o", str(binary)])

    def compile_fixture(name, stage, text):
        path = scratch / (name + "." + stage)
        path.write_text(text)
        target = path.with_suffix(".spv")
        run([args.glslang, "-V", "--target-env", "vulkan1.1", "-Od", str(path), "-o", str(target)])
        run([args.spirv_val, "--target-env", "vulkan1.1", str(target)])
        return target

    vertex = compile_fixture("vertex", "vert", VERTEX)
    paths = {}
    command = [str(binary)]
    for name, text, expected in fixtures:
        fragment = compile_fixture(name, "frag", text)
        paths[name] = fragment
        command.extend([str(vertex), str(fragment), expected])
    store_vertex = compile_fixture("storage_vertex", "vert", VERTEX.replace("void main()", "layout(set=0,binding=0,std430) buffer Data { float value; };\nvoid main()").replace("uv =", "value = 1; uv ="))
    command.extend([str(store_vertex), str(paths["center"]), "nonlocal storage"])

    def mutate(name, original, change, expected, validate=False):
        words = list(struct.unpack("<" + "I" * (original.stat().st_size // 4), original.read_bytes()))
        change(words)
        target = scratch / (name + ".spv")
        target.write_bytes(struct.pack("<" + "I" * len(words), *words))
        if validate:
            run([args.spirv_val, "--target-env", "vulkan1.1", str(target)])
        command.extend([str(vertex), str(target), expected])

    def instructions(words):
        cursor = 5
        while cursor < len(words):
            count = words[cursor] >> 16
            yield cursor, words[cursor] & 65535, count
            cursor += count

    def atomic_load(words):
        at, _, count = next(i for i in instructions(words) if i[1] == 234)
        assert count == 7
        words[at] = (6 << 16) | 227
        del words[at + 6]
    mutate("atomic_load", paths["atomic_add"], atomic_load, "atomic access", validate=True)
    mutate("truncated_header", paths["center"], lambda w: w.__delitem__(slice(0, len(w)-4)), "header")
    mutate("zero_word_count", paths["center"], lambda w: w.__setitem__(5, 17), "truncated")
    mutate("missing_function_end", paths["center"], lambda w: w.pop(), "incomplete")
    def unknown_opcode(words):
        at, _, _ = next(i for i in instructions(words) if i[1] == 253)
        words[at] = (1 << 16) | 65535
    mutate("unknown_operation", paths["center"], unknown_opcode, "unsupported")
    def unresolved_store(words):
        at, _, _ = next(i for i in instructions(words) if i[1] == 62)
        words[at + 1] = words[3] - 1
    mutate("unresolved_pointer", paths["center"], unresolved_store, "unresolved pointer")
    def unresolved_call(words):
        at, _, _ = next(i for i in instructions(words) if i[1] == 57)
        words[at + 3] = words[3] - 1
    mutate("unresolved_call", paths["function_local"], unresolved_call, "unresolved external")
    def pointer_storage_cast(words):
        at, _, _ = next(i for i in instructions(words) if i[1] == 65)
        old_type = words[at + 1]
        type_at, _, _ = next(i for i in instructions(words) if i[1] == 32 and words[i[0] + 1] == old_type)
        pointee = words[type_at + 3]
        new_type = words[3]
        words[3] += 1
        words[at + 1] = new_type
        first_function, _, _ = next(i for i in instructions(words) if i[1] == 54)
        words[first_function:first_function] = [(4 << 16) | 32, new_type, 7, pointee]
    mutate("pointer_storage_change", paths["storage_write"], pointer_storage_cast, "storage class")
    def unsupported_load_operands(words):
        at, _, count = next(i for i in instructions(words) if i[1] == 61)
        assert count == 4
        words[at] = (5 << 16) | 61
        words.insert(at + 4, 0)
    mutate("explicit_load_none", paths["center"], unsupported_load_operands, "unsupported", validate=True)
    print(run(command), end="")
    print("24 independently validated shader fixtures plus 7 malformed-module rejection cases; alias/GPU semantics remain separate")
