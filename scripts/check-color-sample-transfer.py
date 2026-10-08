#!/usr/bin/env python3
"""Validate original shaders and production transfer API ownership; this is not GPU testing."""
import argparse, os, re, shlex, struct, subprocess, tempfile
from pathlib import Path
root = Path(__file__).resolve().parent.parent
source = root/'upstreams/AnyPS5'
graphics = source/'core/libs/prx/libSceAgcDriver/Graphics'
parser = argparse.ArgumentParser()
parser.add_argument('--regenerate-shaders', action='store_true')
parser.add_argument('--shaders-only', action='store_true')
args = parser.parse_args()
programs = [('ColorSampleUpload.vert', 'COLOR_SAMPLE_UPLOAD_VERT_SPV'),
            ('ColorSampleUpload.frag', 'COLOR_SAMPLE_UPLOAD_FRAG_SPV'),
            ('ColorSampleReadback.comp', 'COLOR_SAMPLE_READBACK_COMP_SPV')]
header = graphics/'shaders/ColorSampleTransfer_spv.h'
with tempfile.TemporaryDirectory(prefix='anyps5-color-sample-transfer-') as temporary:
    out = Path(temporary)
    generated = ['// Generated from the adjacent original GLSL with glslangValidator -V --target-env vulkan1.1.',
                 '// Regenerate and validate with scripts/check-color-sample-transfer.py --regenerate-shaders.',
                 '#ifndef ANYPS5_COLOR_SAMPLE_TRANSFER_SPV_H', '#define ANYPS5_COLOR_SAMPLE_TRANSFER_SPV_H', '#include <cstdint>', '']
    for filename, symbol in programs:
        binary = out/(filename+'.spv')
        subprocess.run([os.environ.get('GLSLANG_VALIDATOR', 'glslangValidator'), '-V', '--target-env', 'vulkan1.1', str(graphics/'shaders'/filename), '-o', str(binary)], check=True)
        subprocess.run([os.environ.get('SPIRV_VAL', 'spirv-val'), '--target-env', 'vulkan1.1', str(binary)], check=True)
        data = binary.read_bytes()
        words = struct.unpack('<'+'I'*(len(data)//4), data)
        generated.append('inline constexpr std::uint32_t '+symbol+'[] = {')
        generated.extend('    '+', '.join(f'0x{x:08x}' for x in words[i:i+8])+',' for i in range(0, len(words), 8))
        generated.extend(['};', ''])
        if not args.regenerate_shaders:
            text = header.read_text()
            match = re.search(r'\b'+symbol+r'\[\]\s*=\s*\{(.*?)\};', text, re.S)
            if not match:
                raise SystemExit('Missing embedded shader: '+symbol)
            embedded = tuple(int(word, 16) for word in re.findall(r'0x[0-9a-fA-F]+', match.group(1)))
            if embedded != words:
                raise SystemExit('Embedded shader differs from source output: '+filename+'; inspect tool version before regenerating')
            embedded_file = out/(filename+'.embedded.spv')
            embedded_file.write_bytes(struct.pack('<'+'I'*len(embedded), *embedded))
            subprocess.run([os.environ.get('SPIRV_VAL', 'spirv-val'), '--target-env', 'vulkan1.1', str(embedded_file)], check=True)
    generated.extend(['#endif', ''])
    if args.regenerate_shaders:
        header.write_text('\n'.join(generated))
    print('Color sample transfer original GLSL and embedded SPIR-V: 3 Vulkan 1.1 programs passed')
    if not args.shaders_only:
        binary = out/'test'
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++'))+[
            '-std=c++20', '-O1', '-g', '-UNDEBUG', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-I'+str(source/'core/libs'), '-I'+str(source/'core/shader/recompiler'),
            '-I'+str(source/'3rdparty/Vulkan-Headers/include'),
            str(root/'tools/checks/color_sample_transfer_contract.cpp'),
            str(graphics/'src/ColorRenderTarget.cpp'), str(graphics/'src/ColorSampleTransfer.cpp'),
            '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)
