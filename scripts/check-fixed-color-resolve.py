#!/usr/bin/env python3
"""Check original resolve SPIR-V and production state/data factories, locally.

Native tests use an explicit macOS guest-memory boundary adapter, not Wine or a
GPU. The separate original GPU probe qualifies actual shader execution.
"""
from pathlib import Path
import argparse
import os
import re
import shlex
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--anyps5-source', type=Path, default=root / 'upstreams/AnyPS5')
parser.add_argument('--regenerate-shaders', action='store_true')
parser.add_argument('--shaders-only', action='store_true')
parser.add_argument('--private-registers', type=Path)
args = parser.parse_args()
source = args.anyps5_source.resolve()
graphics = source / 'core/libs/prx/libSceAgcDriver/Graphics'
header = graphics / 'shaders/ColorResolve_spv.h'
with tempfile.TemporaryDirectory(prefix='anyps5-fixed-resolve-') as temp:
    scratch = Path(temp)
    generated = ['// Original ColorResolve.frag, glslangValidator -V --target-env vulkan1.1.',
                 '// Regenerate with scripts/check-fixed-color-resolve.py --regenerate-shaders.',
                 '#ifndef ANYPS5_COLOR_RESOLVE_SPV_H', '#define ANYPS5_COLOR_RESOLVE_SPV_H',
                 '#include <cstdint>', '']
    for samples in (2, 4, 8):
        binary = scratch / f'resolve-{samples}.spv'
        subprocess.run([os.environ.get('GLSLANG_VALIDATOR', 'glslangValidator'), '-V', '--target-env', 'vulkan1.1',
                        '-DAPS5_RESOLVE_SAMPLES=' + str(samples), str(graphics / 'shaders/ColorResolve.frag'), '-o', str(binary)], check=True)
        subprocess.run([os.environ.get('SPIRV_VAL', 'spirv-val'), '--target-env', 'vulkan1.1', str(binary)], check=True)
        data = binary.read_bytes()
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        symbol = f'COLOR_RESOLVE_{samples}_SPV'
        generated.append(f'inline constexpr std::uint32_t {symbol}[] = {{')
        generated.extend('    ' + ', '.join(f'0x{x:08x}' for x in words[i:i+8]) + ',' for i in range(0, len(words), 8))
        generated.extend(['};', ''])
        if not args.regenerate_shaders:
            match = re.search(r'\b' + symbol + r'\[\]\s*=\s*\{(.*?)\};', header.read_text(), re.S)
            if match is None or tuple(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', match.group(1))) != words:
                raise SystemExit('Resolve embedded SPIR-V differs: inspect compiler version before regeneration')
    generated += ['#endif', '']
    if args.regenerate_shaders:
        header.write_text('\n'.join(generated))
    print('Original fixed-function resolve GLSL and embedded SPIR-V: 2/4/8 sample programs passed')
    if not args.shaders_only:
        libs = source / 'core/libs'
        recompiler = source / 'core/shader/recompiler'
        includes = [libs, libs / 'prx/libc/include', recompiler,
                    source / '3rdparty/Vulkan-Headers/include', source / '3rdparty/SPIRV-Headers/include']
        includes += sorted(recompiler.glob('*/include'))
        executable = scratch / 'check'
        production = ['State.cpp', 'ColorResolve.cpp', 'ColorResolveFragment.cpp', 'ColorTargetTransfer.cpp', 'ColorTargetLayout.cpp',
                      'TextureTiling.cpp', 'TextureFormat.cpp', 'DccMetadata.cpp', 'GuestTextureResource.cpp']
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++20', '-O1', '-g', '-UNDEBUG', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip', '-DAPS5_ENABLE_TIMING_LOG=0',
            '-Wno-return-type-c-linkage', *['-I' + str(p) for p in includes],
            str(root / 'tools/checks/fixed_color_resolve.cpp'),
            *[str(graphics / 'src' / p) for p in production],
            str(root / 'tools/checks/color_target_samples_memory_adapter.cpp'), '-o', str(executable),
        ], check=True)
        factories = scratch / 'factory-shaders'
        command = [str(executable), str(factories)]
        if args.private_registers:
            command.append(str(args.private_registers.resolve()))
        subprocess.run(command, check=True, timeout=60)
        for binary in sorted(factories.glob('*.spv')):
            subprocess.run([os.environ.get('SPIRV_VAL', 'spirv-val'), '--target-env', 'vulkan1.1', str(binary)], check=True)
        print('Production factory descriptor remaps: 2/4/8 samples, bindings 0/6 passed spirv-val')
