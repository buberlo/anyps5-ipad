#!/usr/bin/env python3
"""Native GPU pairs of the actual snapshot owner and ColorSampleTransfer.

Independent AMD AddrLib offsets, synthetic unchanged/partial/discard/full draws,
poisoned intermediates and missing full-padding-copy negative controls. Native
Buffer allocation and guest-reader adapters do not qualify Draw/BufferPool/Wine
or physical iPad/game/performance. Existing dependencies only, no downloads.
"""
import argparse,hashlib,json,os,shlex,struct,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',type=Path,default=root/'upstreams/AnyPS5');p.add_argument('--addrlib-source',type=Path,required=True);p.add_argument('--addrlib-library',type=Path,required=True);p.add_argument('--vulkan-library',type=Path,required=True);p.add_argument('--output-dir',type=Path,default=root/'build/gpu-color-sample-staging-copy-production/native');p.add_argument('--run',action='store_true');args=p.parse_args()
source=root/'upstreams/AnyPS5';overlay=args.source_root.resolve();out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);(out/'manifest.json').unlink(missing_ok=True)
def production(name):
 relative=Path('core/libs/prx/libSceAgcDriver/Graphics/src')/name
 return overlay/relative if (overlay/relative).exists() else source/relative
fixture=root/'tools/gpu-probe/color_sample_snapshot_copy_probe.cpp';production_paths=[production(name)for name in ['ColorSampleSnapshotCopy.cpp','GpuColorSampleTiler.cpp','ColorTargetLayout.cpp','ColorSampleTransfer.cpp','ColorRenderTarget.cpp']]
commands=[];addr=args.addrlib_source.resolve();addr_library=args.addrlib_library.resolve();vulkan=args.vulkan_library.resolve();header=[]
with(out/'compile.log').open('w')as log:
 for stage,label in [('vert','COLOR_SNAPSHOT_RASTER_VERT_SPV'),('frag','COLOR_SNAPSHOT_RASTER_FRAG_SPV')]:
  shader=root/('tools/gpu-probe/color_snapshot_copy_raster.'+stage);binary=out/('raster.'+stage+'.spv');command=[os.environ.get('GLSLANG_VALIDATOR','glslangValidator'),'-V','--target-env','vulkan1.1',str(shader),'-o',str(binary)];commands.append(command);subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
  command=[os.environ.get('SPIRV_VAL','spirv-val'),'--target-env','vulkan1.1',str(binary)];commands.append(command);subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
  data=binary.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data);header.append('inline constexpr std::uint32_t '+label+'[] = {');header.extend('    '+', '.join('0x%08x'%word for word in words[i:i+8])+','for i in range(0,len(words),8));header.append('};')
 (out/'ColorSnapshotRaster_spv.h').write_text('#include <cstdint>\n'+'\n'.join(header)+'\n')
 command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O2','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Wno-missing-field-initializers','-I'+str(overlay/'core/libs'),'-I'+str(out)]+['-I'+str(source/path)for path in ['core/libs','core/shader/recompiler','3rdparty/Vulkan-Headers/include']]+['-I'+str(addr/'inc'),str(fixture),*map(str,production_paths),str(addr_library),str(vulkan),'-Wl,-rpath,'+str(vulkan.parent),'-o',str(out/'probe')];commands.append(command);subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
run=None
if args.run:
 with(out/'run.log').open('w')as log:result=subprocess.run([str(out/'probe')],stdout=log,stderr=subprocess.STDOUT,timeout=180)
 text=(out/'run.log').read_text();run={'exitCode':result.returncode,'status':'PASS'if result.returncode==0 and 'errors=0 status=PASS'in text else'FAIL'}
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
deps=production_paths+[fixture,root/'tools/gpu-probe/gpu_color_sample_tiling_probe.cpp',root/'tools/gpu-probe/color_snapshot_copy_raster.vert',root/'tools/gpu-probe/color_snapshot_copy_raster.frag',overlay/'core/libs/prx/libSceAgcDriver/Graphics/include/ColorSampleSnapshotCopy.hpp',Path(__file__).resolve()]+[source/'core/libs/prx/libSceAgcDriver/Graphics/include'/name for name in ['GpuColorSampleTiler.hpp','ColorTargetLayout.hpp','ColorSampleTransfer.hpp','Resources.hpp','Context.hpp','State.hpp','ColorSampleSwizzleEquations.hpp']]+list((source/'core/libs/prx/libSceAgcDriver/Graphics/shaders').glob('ColorSample*'))
manifest={'schema':1,'scope':'Actual snapshot owner, GPU tiler, RenderTarget and ColorSampleTransfer; independent AMD golden offsets/padding; synthetic raster+native allocator/guest-reader adapters. No production Draw/BufferPool/GuestArena/Wine/FEX/iPad/game/performance claim','commands':commands,'run':run,'hashes':{str(path.relative_to(root)):sha(path)for path in deps},'dependencies':{'addrLibrary':sha(addr_library),'addrHeader':sha(addr/'inc/addrinterface.h'),'vulkanLibrary':sha(vulkan)},'artifacts':{path.name:sha(path)for path in out.iterdir()if path.is_file()and path.name!='manifest.json'},'github_actions_used':False}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Native actual color snapshot-copy pairs '+str(run)+': '+str(out))
if run and run['status']!='PASS':raise SystemExit('Native probe failed; inspect '+str(out/'run.log'))
