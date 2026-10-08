#!/usr/bin/env python3
"""Build an original GPU test of production MSAA tiling and sample image transfers.

Provide a separately built AMD AddrLib from PAL c5e800072a32f68b6ccc4422936d96167c6e0728,
plus a matching native Vulkan/MoltenVK library. No dependencies are downloaded.
The golden offsets come from AMD, not ColorTargetLayout. This does not run
Draw/GuestMemory, a guest game, Wine/FEX, or an iPad app.
"""
import argparse, hashlib, json, os, shlex, subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--addrlib-source', type=Path, required=True)
parser.add_argument('--addrlib-library', type=Path, required=True)
parser.add_argument('--vulkan-library', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, default=root/'build/gpu-color-sample-tiling-production/native')
parser.add_argument('--run', action='store_true')
args = parser.parse_args()
out = args.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
(out/'manifest.json').unlink(missing_ok=True)
source = root/'upstreams/AnyPS5'; libs = source/'core/libs'; graphics = libs/'prx/libSceAgcDriver/Graphics'
fixture = root/'tools/gpu-probe/gpu_color_sample_tiling_probe.cpp'
production = [graphics/'src'/name for name in ('GpuColorSampleTiler.cpp','ColorTargetLayout.cpp','ColorSampleTransfer.cpp','ColorRenderTarget.cpp')]
addr = args.addrlib_source.resolve(); addr_library = args.addrlib_library.resolve(); vulkan_library = args.vulkan_library.resolve()
commands = [[os.environ.get('PYTHON', 'python3'), str(root/'scripts/check-gpu-color-sample-tiling.py'), '--shaders-only']]
commands += [shlex.split(os.environ.get('CXX', 'c++'))+['-std=c++20','-O2','-g','-UNDEBUG',
    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Wno-missing-field-initializers',
    '-I'+str(libs),'-I'+str(source/'core/shader/recompiler'),'-I'+str(source/'3rdparty/Vulkan-Headers/include'),
    '-I'+str(addr/'inc'),str(fixture),*map(str,production),str(addr_library),str(vulkan_library),
    '-Wl,-rpath,'+str(vulkan_library.parent),'-o',str(out/'probe')]]
with (out/'compile.log').open('w') as log:
    for command in commands: subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
run = None
if args.run:
    with (out/'run.log').open('w') as log:
        result = subprocess.run([str(out/'probe')], stdout=log, stderr=subprocess.STDOUT, timeout=180)
    run = {'exitCode':result.returncode, 'status':'PASS' if result.returncode==0 and 'status=PASS' in (out/'run.log').read_text() else 'FAIL'}
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
deps = production+[graphics/'include'/name for name in ('GpuColorSampleTiler.hpp','ColorTargetLayout.hpp','ColorSampleSwizzleEquations.hpp','Context.hpp','ColorSampleTransfer.hpp','State.hpp','Resources.hpp')]+list(graphics.glob('shaders/ColorSample*'))
manifest = {'schemaVersion':1, 'scope':'Production helper compute plus RenderTarget/ColorSampleTransfer; original synthetic AMD AddrLib goldens. No Draw/GuestMemory/Wine/FEX/iPad/game/performance qualification.',
    'commands':commands,'cpuSanitizers':['ASan','UBSan'],'run':run,
    'computeCases':99,'imageChainCases':3,'amdCoordinateComparisons':36639764,'logicalSampleWordsChecked':109919292,'paddingBytesChecked':134045536,
    'fixtureSha256':sha(fixture),'scriptSha256':sha(Path(__file__)),
    'sources':{str(p.relative_to(source)):sha(p) for p in deps},
    'amdAddrLib':{'librarySha256':sha(addr_library),'headerSha256':sha(addr/'inc/addrinterface.h')},
    'vulkanLibrary':{'path':str(vulkan_library),'sha256':sha(vulkan_library)},
    'artifacts':{p.name:sha(p) for p in (out/'probe',out/'compile.log',out/'run.log') if p.exists()}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Production sample tiling GPU probe '+('built; '+run['status'] if run else 'built, unexecuted')+': '+str(out))
if run and run['status']!='PASS': raise SystemExit('GPU probe failed; inspect '+str(out/'run.log'))
