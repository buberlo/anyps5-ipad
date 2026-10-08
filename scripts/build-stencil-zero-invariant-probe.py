#!/usr/bin/env python3
"""Build/run original native GPU proof of production StencilZeroTransfer.

This compiles original partial-raster and independent color/Z readback shaders,
then compares the production zero clear with StencilSampleTransfer's legacy zero
upload on the same repeatedly poisoned image. No dependencies are downloaded.
Synthetic image/raster ownership does not qualify actual Draw, DepthSurface,
GuestMemory, Wine/FEX, an iPad app, a game, or a performance gain.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--vulkan-library', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, default=root / 'build/zero-stencil-invariant-production/native')
parser.add_argument('--run', action='store_true')
args = parser.parse_args()
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'manifest.json').unlink(missing_ok=True)
(out / 'run.log').unlink(missing_ok=True)
source = root / 'upstreams/AnyPS5'
libs = source / 'core/libs'
graphics = libs / 'prx/libSceAgcDriver/Graphics'
fixture = root / 'tools/gpu-probe/stencil_zero_invariant_probe.cpp'
production = [graphics / 'src' / name for name in ('StencilZeroInvariant.cpp', 'StencilSampleTransfer.cpp')]
vulkan = args.vulkan_library.resolve()
shader_specs = [
    ('stencil_zero_raster.vert', 'STENCIL_ZERO_VERTEX_SPV'),
    ('stencil_zero_raster.frag', 'STENCIL_ZERO_FRAGMENT_SPV'),
    ('stencil_zero_color_depth_readback.comp', 'STENCIL_ZERO_READBACK_SPV'),
]
shaders = [root / 'tools/gpu-probe' / name for name, _ in shader_specs]
commands = []
generated = []
compiler = shlex.split(os.environ.get('CXX', 'c++'))
include = ['-I' + str(libs), '-I' + str(source / 'core/shader/recompiler'),
           '-I' + str(source / '3rdparty/Vulkan-Headers/include'),
           '-I' + str(source / '3rdparty/SPIRV-Headers/include'), '-I' + str(out)]
flags = ['-std=c++20', '-O2', '-g', '-UNDEBUG', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-Wno-missing-field-initializers']
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
initial_translation_hashes = {path: sha(path) for path in [fixture, *production, *shaders]}

with (out / 'compile.log').open('w') as log:
    header = '#include <cstdint>\n'
    for (name, symbol), shader in zip(shader_specs, shaders):
        spv = out / (name + '.spv')
        for command in ([os.environ.get('GLSLANG_VALIDATOR', 'glslangValidator'), '-V', '--target-env', 'vulkan1.1', str(shader), '-o', str(spv)],
                        [os.environ.get('SPIRV_VAL', 'spirv-val'), '--target-env', 'vulkan1.1', str(spv)]):
            commands.append(command)
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        data = spv.read_bytes()
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        header += 'inline constexpr std::uint32_t ' + symbol + '[] = {\n'
        header += ''.join('  ' + ','.join(f'0x{x:08x}u' for x in words[i:i + 8]) + ',\n' for i in range(0, len(words), 8)) + '};\n'
        generated.append(spv)
    embedded = out / 'StencilZeroProbe_spv.h'
    embedded.write_text(header)
    generated.append(embedded)
    objects = []
    dependencies = []
    for index, translation_unit in enumerate([fixture, *production]):
        obj = out / f'translation-{index}.o'
        dep = out / f'translation-{index}.d'
        command = compiler + flags + include + ['-MMD', '-MF', str(dep), '-c', str(translation_unit), '-o', str(obj)]
        commands.append(command)
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        objects.append(obj)
        dependencies.append(dep)
    command = compiler + flags + list(map(str, objects)) + [str(vulkan), '-Wl,-rpath,' + str(vulkan.parent), '-o', str(out / 'probe')]
    commands.append(command)
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)

# Compiler dependency files include escaped spaces in absolute paths. shlex
# handles those after continuation joins; the target before ':' is irrelevant.
dependency_paths = set([fixture, *production, *shaders])
for dep in dependencies:
    content = dep.read_text().replace('\\\n', ' ')
    dependency_paths.update(Path(token).resolve() for token in shlex.split(content.split(':', 1)[1]))
compiled_input_hashes = {path: sha(path) for path in dependency_paths if path.exists()}
for path, digest in initial_translation_hashes.items():
    if sha(path) != digest:
        raise SystemExit('Native proof input changed during compilation: ' + str(path))

run = None
summary = None
if args.run:
    with (out / 'run.log').open('w') as log:
        result = subprocess.run([str(out / 'probe')], stdout=log, stderr=subprocess.STDOUT, timeout=180)
    match = re.search(r'\[zero-summary\] pairs=(\d+) negative_cases=(\d+) formats=(\d+) skipped_formats=(\d+) repeat_poison=(\d+) color_samples=(\d+) depth_samples=(\d+) stencil_samples=(\d+) negative_nonzero=(\d+) helper_allocations=(\d+) errors=(\d+) status=(PASS|FAIL)', (out / 'run.log').read_text())
    run = {'exitCode': result.returncode, 'status': 'PASS' if result.returncode == 0 and match and match[12] == 'PASS' else 'FAIL'}
    if match:
        summary = dict(zip(['comparisonPairs', 'missingClearNegativeCases', 'qualifiedFormats', 'excludedUnqualifiedFormats', 'sameImagePoisonIterations', 'colorSamplesChecked', 'depthSamplesChecked', 'stencilSamplesChecked', 'negativeNonzeroUncoveredSamples', 'helperVulkanObjectAllocations', 'errors'], map(int, match.groups()[:11])))
        details = re.search(r'\[zero-details\] front_samples=(\d+) back_samples=(\d+) discard_removed_samples=(\d+)', (out / 'run.log').read_text())
        if details:
            summary.update(zip(['actualFrontFaceSamples', 'actualBackFaceSamples', 'samplesRemovedByFragmentDiscard'], map(int, details.groups())))

for path, digest in compiled_input_hashes.items():
    if sha(path) != digest:
        raise SystemExit('Native proof input changed during execution: ' + str(path))
source_hashes = {str(path.relative_to(root)) if path.is_relative_to(root) else str(path): digest
                 for path, digest in sorted(compiled_input_hashes.items())}
artifacts = [out / 'probe', out / 'compile.log', *generated, *objects, *dependencies]
if args.run:
    artifacts.append(out / 'run.log')
manifest = {
    'schemaVersion': 1,
    'scope': 'Production D32S8-only StencilZeroTransfer clear/finish versus legacy StencilSampleTransfer zero upload. Same repeatedly poisoned synthetic D32S8 image, original partial raster/scissor/front-back/discard pipeline, exact color and all8 S8 comparisons, independent nonzero per-layer Z readback, required missing-clear negative control. D16S8 capability query does not qualify that format; production admission excludes it. No actual DepthSurface/Draw/GuestMemory/Wine/FEX/iPad/game/performance qualification.',
    'commands': commands,
    'cpuSanitizers': ['ASan', 'UBSan'],
    'run': run,
    'summary': summary,
    'fixtureSha256': sha(fixture),
    'scriptSha256': sha(Path(__file__)),
    'sources': source_hashes,
    'vulkanLibrary': {'path': str(vulkan), 'sha256': sha(vulkan)},
    'artifacts': {path.name: sha(path) for path in artifacts if path.exists()},
}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Stencil zero invariant native proof ' + ('built; ' + run['status'] if run else 'built, unexecuted') + ': ' + str(out))
if run and run['status'] != 'PASS':
    raise SystemExit('GPU probe failed; inspect ' + str(out / 'run.log'))
