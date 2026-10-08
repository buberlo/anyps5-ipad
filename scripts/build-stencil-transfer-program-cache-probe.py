#!/usr/bin/env python3
"""Build an original native test of production immutable stencil program cache plus S8/8 tiling and transfer.

Supply independently built AMD AddrLib from PAL c5e800072a32f68b6ccc4422936d96167c6e0728
and a matching native Vulkan/MoltenVK library. No dependencies are downloaded.
Goldens come from AMD, not DepthTargetLayout. The synthetic D32S8 owner and
uniform-depth reader do not qualify DepthSurface/Draw/GuestMemory, Wine/FEX,
an iPad app, a real game, or performance.
"""
import argparse,hashlib,json,os,re,shlex,struct,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--addrlib-source',type=Path,required=True)
parser.add_argument('--addrlib-library',type=Path,required=True)
parser.add_argument('--vulkan-library',type=Path,required=True)
parser.add_argument('--output-dir',type=Path,default=root/'build/stencil-transfer-program-cache-production/native')
parser.add_argument('--run',action='store_true')
args=parser.parse_args()
out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);(out/'manifest.json').unlink(missing_ok=True)
source=root/'upstreams/AnyPS5';libs=source/'core/libs';graphics=libs/'prx/libSceAgcDriver/Graphics'
fixture=root/'tools/gpu-probe/stencil_transfer_program_cache_probe.cpp'
production=[graphics/'src'/name for name in ('GpuStencilSampleTiler.cpp','DepthTargetLayout.cpp','StencilSampleTransfer.cpp')]
addr=args.addrlib_source.resolve();addr_library=args.addrlib_library.resolve();vulkan_library=args.vulkan_library.resolve()
depthShader=root/'tools/gpu-probe/stencil_sample_depth_readback.comp'
commands=[['python3',str(root/'scripts/check-gpu-stencil-sample-tiling.py'),'--shaders-only'],
 [os.environ.get('GLSLANG_VALIDATOR','glslangValidator'),'-V','--target-env','vulkan1.1',str(depthShader),'-o',str(out/'depth-probe.spv')],
 [os.environ.get('SPIRV_VAL','spirv-val'),'--target-env','vulkan1.1',str(out/'depth-probe.spv')]]
with (out/'compile.log').open('w') as log:
    for command in commands:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    data=(out/'depth-probe.spv').read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
    (out/'StencilSampleDepthProbe_spv.h').write_text('#include <cstdint>\ninline constexpr std::uint32_t STENCIL_DEPTH_PROBE_SPV[] = {\n'+''.join('  '+','.join(f'0x{x:08x}u' for x in words[i:i+8])+',\n' for i in range(0,len(words),8))+'};\n')
    command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O2','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Wno-missing-field-initializers',
        '-I'+str(libs),'-I'+str(source/'core/shader/recompiler'),'-I'+str(source/'3rdparty/Vulkan-Headers/include'),'-I'+str(addr/'inc'),'-I'+str(out),str(fixture),*map(str,production),str(addr_library),str(vulkan_library),'-Wl,-rpath,'+str(vulkan_library.parent),'-o',str(out/'probe')]
    commands.append(command);subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
run=None;summary=None
if args.run:
    with (out/'run.log').open('w') as log:result=subprocess.run([str(out/'probe')],stdout=log,stderr=subprocess.STDOUT,timeout=180)
    match=re.search(r'\[s8-summary\] compute_cases=(\d+) image_chain_cases=(\d+) AMD_coordinates=(\d+) logical_bytes_checked=(\d+) padding_bytes_checked=(\d+) unchanged_depth_samples=(\d+) s8_values=(\d+) errors=(\d+) status=(PASS|FAIL)',(out/'run.log').read_text())
    run={'exitCode':result.returncode,'status':'PASS' if result.returncode==0 and match and match[9]=='PASS' else 'FAIL'}
    if match:summary=dict(zip(['computeCases','imageChainCases','amdCoordinateComparisons','logicalOutputBytesChecked','paddingBytesChecked','unchangedDepthSamples','distinctS8Values','errors'],map(int,match.groups()[:8])))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
deps=production+[graphics/'include'/name for name in ('GpuStencilSampleTiler.hpp','DepthTargetLayout.hpp','DepthSampleSwizzleEquations.hpp','Context.hpp','StencilSampleTransfer.hpp','ColorSampleTransfer.hpp','State.hpp','Resources.hpp')]+list(graphics.glob('shaders/StencilSample*'))+[depthShader]
manifest={'schemaVersion':1,'scope':'Production immutable stencil cache hit/change/clear plus S8/8 tiler and StencilSampleTransfer with original synthetic D32S8 owner/uniform unchanged-Z reader. No actual DepthSurface/Draw/GuestMemory/Wine/FEX/iPad/game/performance qualification.',
    'commands':commands,'cpuSanitizers':['ASan','UBSan'],'run':run,'summary':summary,'fixtureSha256':sha(fixture),'scriptSha256':sha(Path(__file__)),
    'sources':{str(p.relative_to(source)) if p.is_relative_to(source) else str(p.relative_to(root)):sha(p) for p in deps},
    'amdAddrLib':{'librarySha256':sha(addr_library),'headerSha256':sha(addr/'inc/addrinterface.h')},
    'vulkanLibrary':{'path':str(vulkan_library),'sha256':sha(vulkan_library)},'cacheCases':{'misses':3,'hits':3,'newStencilPrograms':27,'clearWithLiveTransferBeforeSubmit':True},'artifacts':{p.name:sha(p) for p in (out/'probe',out/'compile.log',out/'run.log',out/'depth-probe.spv',out/'StencilSampleDepthProbe_spv.h') if p.exists()}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Production S8/8 tiling GPU probe '+('built; '+run['status'] if run else 'built, unexecuted')+': '+str(out))
if run and run['status']!='PASS':raise SystemExit('GPU probe failed; inspect '+str(out/'run.log'))
