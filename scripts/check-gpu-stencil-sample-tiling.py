#!/usr/bin/env python3
"""Validate original sample-tiling shaders and production ownership contract, without GPU claims."""
import argparse, os, re, shlex, struct, subprocess, tempfile
from pathlib import Path
root = Path(__file__).resolve().parent.parent
source = root/'upstreams/AnyPS5'
graphics = source/'core/libs/prx/libSceAgcDriver/Graphics'
parser = argparse.ArgumentParser()
parser.add_argument('--regenerate-shaders', action='store_true')
parser.add_argument('--shaders-only', action='store_true')
args = parser.parse_args()
header = graphics/'shaders/StencilSampleTiling_spv.h'
with tempfile.TemporaryDirectory(prefix='anyps5-gpu-stencil-sample-tiling-') as temporary:
    out = Path(temporary); binary = out/'shader.spv'
    subprocess.run([os.environ.get('GLSLANG_VALIDATOR', 'glslangValidator'), '-V', '--target-env', 'vulkan1.1', str(graphics/'shaders/StencilSampleTiling.comp'), '-o', str(binary)], check=True)
    subprocess.run([os.environ.get('SPIRV_VAL', 'spirv-val'), '--target-env', 'vulkan1.1', str(binary)], check=True)
    data = binary.read_bytes(); words = struct.unpack('<'+'I'*(len(data)//4), data)
    if args.regenerate_shaders:
        lines = ['// Original StencilSampleTiling.comp; regenerate with scripts/check-gpu-stencil-sample-tiling.py --regenerate-shaders.', '#ifndef ANYPS5_STENCIL_SAMPLE_TILING_SPV_H', '#define ANYPS5_STENCIL_SAMPLE_TILING_SPV_H', '#include <cstdint>', 'inline constexpr std::uint32_t STENCIL_SAMPLE_TILING_COMP_SPV[] = {']
        lines.extend('    '+', '.join(f'0x{x:08x}' for x in words[i:i+8])+',' for i in range(0, len(words), 8))
        header.write_text('\n'.join(lines+['};', '#endif', '']))
    else:
        match = re.search(r'STENCIL_SAMPLE_TILING_COMP_SPV\[\]\s*=\s*\{(.*?)\};', header.read_text(), re.S)
        if not match or tuple(int(w, 16) for w in re.findall(r'0x[0-9a-fA-F]+', match.group(1))) != words:
            raise SystemExit('Embedded sample tiling shader differs from compiled original')
    print('GPU stencil sample tiling original GLSL/embedded SPIR-V: Vulkan 1.1 PASS')
    if not args.shaders_only:
        binary = out/'contract'
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++'))+[
            '-std=c++20', '-O1', '-g', '-UNDEBUG', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-I'+str(source/'core/libs'), '-I'+str(source/'core/shader/recompiler'),
            '-I'+str(source/'3rdparty/Vulkan-Headers/include'),
            str(root/'tools/checks/gpu_stencil_sample_tiling_contract.cpp'),
            str(graphics/'src/DepthTargetLayout.cpp'), str(graphics/'src/GpuStencilSampleTiler.cpp'),
            '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)
