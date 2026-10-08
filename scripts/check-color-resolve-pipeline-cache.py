#!/usr/bin/env python3
"""Exercise production resolve pipeline caching with original shaders locally.

Links actual Pipeline/CachedPipeline, resolve factory, canonical rect emitter and
shader validator. Vulkan creation uses counted mocks and ShaderResources has an
explicit immutable-layout boundary adapter. This is not GPU, device, complete
resource/Recorder or performance qualification. No downloads or GitHub Actions.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--anyps5-source', type=Path, default=root / 'upstreams/AnyPS5')
parser.add_argument('--output-dir', type=Path, default=root / 'build/color-resolve-pipeline-cache/native')
parser.add_argument('--glslang', default='glslangValidator')
parser.add_argument('--spirv-val', default='spirv-val')
parser.add_argument('--pipeline-source', type=Path, help='Prior production Pipeline.cpp for a regression negative control')
args = parser.parse_args()
source, output = args.anyps5_source.resolve(), args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
(output / 'report.json').unlink(missing_ok=True)
recompiler = source / 'core/shader/recompiler'
graphics = source / 'core/libs/prx/libSceAgcDriver/Graphics'
vertex = output / 'original.vert'
vertex.write_text('''#version 450
out gl_PerVertex { vec4 gl_Position; };
void main() {
    vec2 uv = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
    gl_Position = vec4(uv * 2.0 - 1.0, 0, 1);
}
''')
spirv = output / 'original.spv'
commands = [[args.glslang, '-V', '--target-env', 'vulkan1.1', '-Od', str(vertex), '-o', str(spirv)],
            [args.spirv_val, '--target-env', 'vulkan1.1', str(spirv)]]
production = [(args.pipeline_source or graphics / 'src/Pipeline.cpp').resolve(), graphics / 'src/ColorResolveFragment.cpp',
              graphics / 'src/ShaderValidation.cpp', recompiler / 'SpirvBackend/src/RectListShaders.cpp',
              recompiler / 'SpirvBackend/src/SpirvModule.cpp']
fixture = root / 'tools/checks/color_resolve_pipeline_cache.cpp'
includes = [source / 'core/libs', recompiler, source / '3rdparty/Vulkan-Headers/include',
            source / '3rdparty/SPIRV-Headers/include', *sorted(recompiler.glob('*/include'))]
exe = output / 'check'
commands.append(shlex.split(os.environ.get('CXX', 'c++')) + [
    '-std=c++20', '-O1', '-g', '-UNDEBUG', '-DANYPS5_ENABLE_SPIRV_TOOLS=0', '-DAPS5_ENABLE_TIMING_LOG=0',
    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-ffunction-sections', '-fdata-sections',
    '-Wno-return-type-c-linkage', *['-I' + str(p) for p in includes],
    str(fixture), *map(str, production), '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-o', str(exe)])
with (output / 'compile.log').open('w') as log:
    for command in commands:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
runs = []
for option in (None, '0', '1', 'yes'):
    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith('APS5_'):
            del env[key]
    if option is not None:
        env['APS5_CACHE_COLOR_RESOLVE_PIPELINES'] = option
    logpath = output / ('run-' + (option or 'unset') + '.log')
    with logpath.open('w') as log:
        result = subprocess.run([str(exe), str(spirv)], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    text = logpath.read_text()
    if result.returncode or 'PASS' not in text or f'enabled={int(option == "1")}' not in text:
        raise SystemExit('Production pipeline-cache check failed: ' + str(logpath))
    runs.append({'option': option, 'exitCode': result.returncode, 'logSha256': sha(logpath)})
dependencies = production + [graphics / 'include/Shaders.hpp', graphics / 'include/Pipeline.hpp',
                             graphics / 'include/ShaderResources.hpp', graphics / 'shaders/ColorResolve_spv.h']
report = {'schemaVersion': 1, 'status': 'production_pipeline_cache_mock_checks_passed',
          'productionSha256': {str(p.relative_to(source)): sha(p) for p in dependencies},
          'fixtureSha256': sha(fixture), 'scriptSha256': sha(Path(__file__)),
          'executableSha256': sha(exe), 'commands': commands, 'runs': runs,
          'scope': 'Actual production cache/creation path, ASan/UBSan, original factory/rect/SPIR-V inspector; mocked Vulkan and immutable ShaderResources layout adapter. No GPU/device/full resource/Recorder or performance proof.'}
(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('Actual production resolve pipeline cache: exact opt-in, reuse/separation, private unknown stages, device/pool/LRU shared lifetimes PASS; no speed claim')
