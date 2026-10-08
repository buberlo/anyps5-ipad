#!/usr/bin/env python3
"""Run actual Windows zero-stencil identity commit/alias checks locally; no device or downloads."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--wine',type=Path,required=True);p.add_argument('--wine-prefix',type=Path,required=True);p.add_argument('--mingw-cxx',type=Path,default=root/'build/toolchains/winlibs/mingw64/bin/g++.exe');p.add_argument('--output-dir',type=Path,default=root/'build/zero-stencil-invariant-production/windows');args=p.parse_args()
source=root/'upstreams/AnyPS5';out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);(out/'report.json').unlink(missing_ok=True)
env=os.environ.copy();env.update(WINEPREFIX=str(args.wine_prefix.resolve()),WINEDEBUG='-all',MVK_CONFIG_LOG_LEVEL='0')
def win(path):return 'Z:'+str(path.resolve()).replace('/','\\')
base=[str(args.wine.resolve()),win(args.mingw_cxx),'-std=c++20','-O2','-DNOMINMAX','-DAPS5_ENABLE_TIMING_LOG=0','-DAPS5_SLIM','-D_WIN32_WINNT=0x0A00','-DWINVER=0x0A00','-I'+win(source/'core/libs'),'-I'+win(source/'core/shader/recompiler')]
inputs=[source/'core/libs/prx/libSceAgcDriver/Execution/src/GuestMemory.cpp',source/'core/libs/prx/libc/src/GuestArena.cpp',source/'core/libs/prx/libc/src/GuestAllocations.cpp',root/'tools/checks/stencil_zero_writeback_windows.cpp']
commands=[];objects=[]
with (out/'compile.log').open('w') as log:
    for path in inputs:
        obj=out/(path.stem+'.o');command=base+['-c',win(path),'-o',win(obj)];commands.append(command);subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120);objects.append(obj)
    binary=out/'zero-stencil-writeback.exe';command=base+[win(obj) for obj in objects]+['-static-libgcc','-static-libstdc++','-o',win(binary)];commands.append(command);subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
pthread=args.mingw_cxx.resolve().parent/'libwinpthread-1.dll'
if pthread.exists():shutil.copy2(pthread,out/pthread.name)
runs=[]
for option in [None,'0','1','yes']:
    runenv=env.copy()
    for key in list(runenv):
        if key.startswith('APS5_'):del runenv[key]
    runenv.update(APS5_GUEST_ARENA_BASE='0x7400000000',APS5_GUEST_ARENA_SIZE='0x10000000',APS5_GUEST_ARENA_CHUNK='0x10000000',APS5_GUEST_ARENA_LAZY='1')
    if option is not None:runenv['APS5_BULK_DRIVER_WRITES']=option
    logpath=out/('run-'+(option or 'unset')+'.log')
    with logpath.open('w') as log:result=subprocess.run([str(args.wine.resolve()),win(binary)],env=runenv,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    text=logpath.read_text()
    if result.returncode!=0 or 'zero stencil identity WriteChanged:' not in text or 'write tracking tests passed' not in text:raise SystemExit('Zero-stencil actual Windows fixture failed: '+str(logpath))
    runs.append({'option':option,'exitCode':result.returncode,'logSha256':hashlib.sha256(logpath.read_bytes()).hexdigest()})
dependencies=inputs+[source/'core/libs/prx/libSceAgcDriver/tests/WriteTracking.cpp',source/'core/libs/prx/libc/include/WindowsMappings.hpp',Path(__file__).resolve(),binary,out/'compile.log']
report={'status':'actual_windows_identity_writeback_checks_passed','scope':'Actual local Wine Windows GuestMemory/GuestArena shared aliases and production tracking; synthetic CPU fence interleaving, no GPU/ARM64EC/iPad/performance claim','commands':commands,'runs':runs,'sha256':{str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in dependencies}}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('Actual Windows identity WriteChanged + CPU/alias/protection checks: four bulk-option runs passed')
