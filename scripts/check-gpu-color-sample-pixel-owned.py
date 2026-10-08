#!/usr/bin/env python3
"""Compile original/new tiling shaders and validate captured gate/schema ownership contracts.

No GPU, Draw, tracker, device-runtime or performance claim is made by this checker.
The separate native workflow compares actual Vulkan results against AMD AddrLib.
"""
import argparse, hashlib, json, os, re, shlex, struct, subprocess
from pathlib import Path
root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=root/'upstreams/AnyPS5')
parser.add_argument('--output', type=Path, default=root/'build/gpu-color-sample-pixel-owned-contract')
parser.add_argument('--regenerate-shaders', action='store_true')
args = parser.parse_args()
source=args.source.resolve(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
report=output/'report.json'; report.unlink(missing_ok=True)
original=root/'upstreams/AnyPS5'; graphics=Path('core/libs/prx/libSceAgcDriver/Graphics')
def actual(path):
    candidate=source/path
    return candidate if candidate.is_file() else original/path
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def run(command, label, expected=True, env=None):
    result=subprocess.run([str(x) for x in command],capture_output=True,text=True,env=env,timeout=90)
    log=output/(label+'.log');log.write_text(result.stdout+result.stderr)
    if expected and result.returncode: raise RuntimeError(f'{label} failed: {result.returncode}; {log}')
    if not expected and not result.returncode: raise RuntimeError(f'{label} mutation unexpectedly passed')
    return {'command':[str(x) for x in command], 'exit_code':result.returncode,'log_sha256':sha(log)}
results={};inputs={};artifacts={}
for stem,symbol in [('ColorSampleTiling','COLOR_SAMPLE_TILING_COMP_SPV'),('ColorSamplePixelOwnedTiling','COLOR_SAMPLE_PIXEL_OWNED_TILING_COMP_SPV')]:
    shader=actual(graphics/f'shaders/{stem}.comp'); header=actual(graphics/f'shaders/{stem}_spv.h');binary=output/(stem+'.spv')
    results[stem+'_compile']=run([os.environ.get('GLSLANG_VALIDATOR','glslangValidator'),'-V','--target-env','vulkan1.1',shader,'-o',binary],stem+'-compile')
    results[stem+'_validate']=run([os.environ.get('SPIRV_VAL','spirv-val'),'--target-env','vulkan1.1',binary],stem+'-validate')
    data=binary.read_bytes(); words=struct.unpack('<'+'I'*(len(data)//4),data)
    if args.regenerate_shaders and stem=='ColorSamplePixelOwnedTiling':
        lines=['// Original pixel-owned8 shader; regenerate with scripts/check-gpu-color-sample-pixel-owned.py --regenerate-shaders.','#ifndef ANYPS5_COLOR_SAMPLE_PIXEL_OWNED_TILING_SPV_H','#define ANYPS5_COLOR_SAMPLE_PIXEL_OWNED_TILING_SPV_H','#include <cstdint>',f'inline constexpr std::uint32_t {symbol}[] = {{']
        lines.extend('    '+', '.join(f'0x{x:08x}' for x in words[i:i+8])+',' for i in range(0,len(words),8))
        header.write_text('\n'.join(lines+['};','#endif','']))
    match=re.search(re.escape(symbol)+r'\[\]\s*=\s*\{(.*?)\};',header.read_text(),re.S)
    if not match or tuple(int(w,16) for w in re.findall(r'0x[0-9a-fA-F]+',match.group(1)))!=words: raise RuntimeError(f'{stem} embedded bytes differ')
    inputs[str(graphics/f'shaders/{stem}.comp')]=sha(shader);inputs[str(graphics/f'shaders/{stem}_spv.h')]=sha(header);artifacts[binary.name]=sha(binary)
helper=actual(graphics/'src/GpuColorSampleTiler.cpp');layout=actual(graphics/'src/ColorTargetLayout.cpp')
base=shlex.split(os.environ.get('CXX','clang++'))+['-std=c++20','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(source/'core/libs'),'-I'+str(original/'core/libs'),'-I'+str(original/'core/shader/recompiler'),'-I'+str(original/'3rdparty/Vulkan-Headers/include')]
def contract(helper_path,label,fixture,negative=False):
    binary=output/(label+'.exe')
    results[label+'_compile']=run(base+[str(fixture),str(layout),str(helper_path),'-o',str(binary)],label+'-compile')
    env=dict(os.environ);env.pop('APS5_GPU_COLOR_SAMPLE_PIXEL_OWNED',None)
    results[label+'_run']=run([binary],label+'-run',expected=not negative,env=env);artifacts[binary.name]=sha(binary)
fixture=root/'tools/checks/gpu_color_sample_pixel_owned_contract.cpp'
contract(helper,'candidate',fixture)
contract(helper,'retained-legacy',root/'tools/checks/gpu_color_sample_tiling_contract.cpp')
mutations={
 'duplicate-z8':('owner.pixelOwned8 && samples == 8 ? 1u : samples','samples'),
 'wrong-xor':('    std::array<VkSpecializationMapEntry, 23> entries{};','    if (pixelOwned) values[20] ^= 4u;\n    std::array<VkSpecializationMapEntry, 23> entries{};'),
 'gate-always-on':('return value && std::string_view(value) == "1";','return true;')}
for label,(needle,replace) in mutations.items():
    text=helper.read_text();assert text.count(needle)==1, label
    path=output/(label+'.cpp');path.write_text(text.replace(needle,replace))
    contract(path,label,fixture,negative=True);artifacts[path.name]=sha(path)
for path in [graphics/'src/GpuColorSampleTiler.cpp',graphics/'include/GpuColorSampleTiler.hpp',graphics/'include/ColorSampleSwizzleEquations.hpp',graphics/'src/ColorTargetLayout.cpp']:
    inputs[str(path)]=sha(actual(path))
inputs['tools/checks/gpu_color_sample_pixel_owned_contract.cpp']=sha(fixture)
inputs['tools/checks/gpu_color_sample_tiling_contract.cpp']=sha(root/'tools/checks/gpu_color_sample_tiling_contract.cpp')
inputs['scripts/check-gpu-color-sample-pixel-owned.py']=sha(Path(__file__).resolve())
record={'status':'PASS','scope':'ASan/UBSan production helper contract plus Vulkan1.1 shader/embedded-byte check; no actual GPU/Draw/tracker/iPad performance','gate':'exact APS5_GPU_COLOR_SAMPLE_PIXEL_OWNED=1; captured once per owner; default off','contract':{'captured_option_cases':12,'metadata_fallbacks_per_case':22,'schemas':[20,23],'pixel_owned_samples':8,'pixel_dispatch_z':1,'retained_legacy_samples':[2,4,8],'warm_owner_device_separation':True,'distinct_retained_descriptors':True,'barriers_preserved':True,'lazy_partial_module_pipeline_failure_retry':True},'mutation_negative_controls':list(mutations),'results':results,'source_sha256':inputs,'artifact_sha256':artifacts}
report.write_text(json.dumps(record,indent=2)+'\n');print('pixel-owned8 contract/shaders PASS; report',sha(report))
