#!/usr/bin/env python3
"""Build/run isolated candidate63 native GPU pairs against AMD AddrLib.

Only the frozen overlay's GpuColorSampleTiler is compiled as candidate code.
The retained57 fixture, retained61 raster shaders and original image transfer
production sources are dependencies. No downloads, device work or broad build.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GRAPHICS = Path('core/libs/prx/libSceAgcDriver/Graphics')
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root', type=Path, default=ROOT/'build/gpu-color-sample-pixel-owned-production/candidate')
p.add_argument('--freeze-manifest', type=Path, required=True)
p.add_argument('--addrlib-source', type=Path, required=True)
p.add_argument('--addrlib-library', type=Path, required=True)
p.add_argument('--vulkan-library', type=Path, required=True)
p.add_argument('--output-dir', type=Path, default=ROOT/'build/gpu-color-sample-pixel-owned-production/native')
p.add_argument('--run', action='store_true')
args = p.parse_args()
source = ROOT/'upstreams/AnyPS5'
overlay = args.source_root.resolve()
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
(out/'manifest.json').unlink(missing_ok=True)
addr = args.addrlib_source.resolve()
addr_library = args.addrlib_library.resolve()
vulkan = args.vulkan_library.resolve()
freeze = args.freeze_manifest.resolve()
fixture = ROOT/'tools/gpu-probe/gpu_color_sample_pixel_owned_probe.cpp'
production = [overlay/GRAPHICS/'src/GpuColorSampleTiler.cpp'] + [source/GRAPHICS/'src'/name for name in ('ColorTargetLayout.cpp','ColorSampleTransfer.cpp','ColorRenderTarget.cpp')]
candidate_files = [overlay/GRAPHICS/name for name in ('src/GpuColorSampleTiler.cpp','include/GpuColorSampleTiler.hpp','shaders/ColorSamplePixelOwnedTiling.comp','shaders/ColorSamplePixelOwnedTiling_spv.h')]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
if not freeze.is_file():
    raise SystemExit('Source freeze manifest must exist before compiling')
for path in candidate_files:
    if not path.is_file():
        raise SystemExit('Missing frozen candidate file: '+str(path))
before = {str(path):sha(path) for path in candidate_files}
freeze_digest = sha(freeze)
frozen = json.loads(freeze.read_text())
for relative, digest in frozen['owned'].items():
    if sha(overlay/relative) != digest:
        raise SystemExit('Candidate does not match source freeze: '+relative)
for relative, digest in frozen['retained_old_shader'].items():
    if sha(source/relative) != digest:
        raise SystemExit('Retained shader does not match source freeze: '+relative)
commands = []
headers = []
with (out/'compile.log').open('w') as log:
    pixel_shader = overlay/GRAPHICS/'shaders/ColorSamplePixelOwnedTiling.comp'
    pixel_binary = out/'pixel.comp.spv'
    command = [os.environ.get('GLSLANG_VALIDATOR','glslangValidator'),'-V','--target-env','vulkan1.1',str(pixel_shader),'-o',str(pixel_binary)]
    commands.append(command)
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    command = [os.environ.get('SPIRV_VAL','spirv-val'),'--target-env','vulkan1.1',str(pixel_binary)]
    commands.append(command)
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    embedded_words = [int(word,16) for word in re.findall(r'0x[0-9a-fA-F]{8}',(overlay/GRAPHICS/'shaders/ColorSamplePixelOwnedTiling_spv.h').read_text())]
    if struct.pack('<'+'I'*len(embedded_words),*embedded_words) != pixel_binary.read_bytes():
        raise SystemExit('Frozen pixel GLSL and embedded SPIR-V differ')
    for stage, label in [('vert','COLOR_SNAPSHOT_RASTER_VERT_SPV'),('frag','COLOR_SNAPSHOT_RASTER_FRAG_SPV')]:
        shader = ROOT/('tools/gpu-probe/color_snapshot_copy_raster.'+stage)
        binary = out/('raster.'+stage+'.spv')
        command = [os.environ.get('GLSLANG_VALIDATOR','glslangValidator'),'-V','--target-env','vulkan1.1',str(shader),'-o',str(binary)]
        commands.append(command)
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        command = [os.environ.get('SPIRV_VAL','spirv-val'),'--target-env','vulkan1.1',str(binary)]
        commands.append(command)
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        data = binary.read_bytes()
        words = struct.unpack('<'+'I'*(len(data)//4),data)
        headers.append('inline constexpr std::uint32_t '+label+'[] = {')
        headers.extend('    '+', '.join('0x%08x'%word for word in words[i:i+8])+',' for i in range(0,len(words),8))
        headers.append('};')
    (out/'ColorSnapshotRaster_spv.h').write_text('#include <cstdint>\n'+'\n'.join(headers)+'\n')
    flags = shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O2','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Wno-missing-field-initializers','-I'+str(overlay/'core/libs'),'-I'+str(out)]
    flags += ['-I'+str(source/path) for path in ('core/libs','core/shader/recompiler','3rdparty/Vulkan-Headers/include')]+['-I'+str(addr/'inc')]
    # Compiler dependency closure includes every project, vendor and SDK header.
    dependency_paths = set()
    for index, path in enumerate([fixture,*production]):
        dep = out/('dependency-'+str(index)+'.d')
        command = flags+['-M','-MT','probe','-MF',str(dep),str(path)]
        commands.append(command)
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        text = dep.read_text().replace('\\\n',' ')
        dependency_paths.update(Path(token).resolve() for token in shlex.split(text.split(':',1)[1]))
    dependencies = dependency_paths|set(production+candidate_files+[fixture,Path(__file__).resolve(),freeze,addr_library,vulkan])
    dependencies |= {ROOT/'tools/gpu-probe/color_snapshot_copy_raster.vert',ROOT/'tools/gpu-probe/color_snapshot_copy_raster.frag'}
    dependencies |= {path.resolve() for path in addr.rglob('*') if path.is_file() and (path.suffix in ('.cpp','.h','.hpp','.cmake') or path.name=='CMakeLists.txt')}
    dependency_before = {str(path):sha(path) for path in sorted(dependencies)}
    command = flags+[str(fixture),*map(str,production),str(addr_library),str(vulkan),'-Wl,-rpath,'+str(vulkan.parent),'-o',str(out/'probe')]
    commands.append(command)
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
run = None
if args.run:
    with (out/'run.log').open('w') as log:
        result = subprocess.run([str(out/'probe')], stdout=log, stderr=subprocess.STDOUT, timeout=300)
    output = (out/'run.log').read_text()
    summary = next((line for line in output.splitlines() if line.startswith('[pixel-owned] ')), '')
    run = {'exitCode':result.returncode,'status':'PASS' if result.returncode==0 and 'errors=0 status=PASS' in summary else 'FAIL','summary':summary,'timestamp':next((line for line in output.splitlines() if line.startswith('[pixel-timestamp] ')),None)}
after = {str(path):sha(path) for path in candidate_files}
if before != after or sha(freeze) != freeze_digest:
    raise SystemExit('Candidate or source freeze changed during qualification')
dependency_after = {str(path):sha(path) for path in sorted(dependencies)}
if dependency_before != dependency_after:
    raise SystemExit('A compiler/link dependency changed during qualification')
manifest = {'schemaVersion':1,'scope':'Native Mac GPU candidate63 owner gate/program/cache/dispatch and paired AMD golden compute plus actual ColorSampleTransfer unchanged/partial/discard/full synthetic raster. No Draw/GuestMemory/Wine/FEX/iPad/game qualification or iPad speed claim.','commands':commands,'run':run,'cpuSanitizers':['ASan','UBSan'],'freezeManifest':{'path':str(freeze),'sha256':freeze_digest},'frozenCandidate':before,'dependencyHashes':dependency_after,'dependencyHashesStableBeforeAfter':True,'pixelGlslEmbeddedSpirvEqual':True,'artifacts':{path.name:sha(path) for path in sorted(out.iterdir()) if path.is_file() and path.name!='manifest.json'},'githubActionsUsed':False}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Native candidate63 pixel-owned GPU pairs '+str(run)+': '+str(out))
if run and run['status']!='PASS':
    raise SystemExit('Native probe failed; inspect '+str(out/'run.log'))
