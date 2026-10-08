#!/usr/bin/env python3
"""Check the production raw/legacy resolve snapshot paths with native sanitizers.

Links actual snapshot, tiled transfers and layout. Native mapped-range checks
are real; the flush/write-generation boundary is a counted explicit adapter.
This does not qualify Wine tracking, GPU aliases, device output or speed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--anyps5-source', type=Path, default=root/'upstreams/AnyPS5')
parser.add_argument('--output-dir', type=Path, default=root/'build/raw-color-resolve-snapshot/native')
args = parser.parse_args()
if sys.platform != 'darwin': parser.error('Native mapped-range adapter is macOS-only')
source, output = args.anyps5_source.resolve(), args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
(output/'report.json').unlink(missing_ok=True)
libs = source/'core/libs'
recompiler = source/'core/shader/recompiler'
graphics = libs/'prx/libSceAgcDriver/Graphics'
includes = [libs, libs/'prx/libc/include', recompiler, source/'3rdparty/Vulkan-Headers/include',
            source/'3rdparty/SPIRV-Headers/include', *sorted(recompiler.glob('*/include'))]
flags = ['-std=c++20', '-O1', '-g', '-UNDEBUG', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', '-ffunction-sections', '-fdata-sections',
         '-DAPS5_ENABLE_TIMING_LOG=0', '-Wno-return-type-c-linkage', *['-I'+str(p) for p in includes]]
compiler = shlex.split(os.environ.get('CXX', 'c++'))
adapter = root/'tools/checks/color_target_samples_memory_adapter.cpp'
fixture = root/'tools/checks/raw_color_resolve_snapshot.cpp'
production = [graphics/'src'/name for name in ('ColorResolve.cpp', 'ColorTargetTransfer.cpp', 'ColorTargetLayout.cpp')]
obj, exe = output/'adapter.o', output/'check'
commands = [compiler+flags+['-DRead=AdapterRead','-DWrite=AdapterWrite','-c',str(adapter),'-o',str(obj)],
            compiler+flags+[str(fixture),*map(str,production),str(obj),'-Wl,-dead_strip','-o',str(exe)]]
with (output/'compile.log').open('w') as log:
    for command in commands: subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
runs=[]
for option in (None,'0','1','yes'):
    env={k:v for k,v in os.environ.items() if not k.startswith('APS5_')}
    if option is not None: env['APS5_RAW_RESOLVE_SNAPSHOT']=option
    p=output/('run-'+(option or 'unset')+'.log')
    with p.open('w') as log: result=subprocess.run([str(exe)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    text=p.read_text()
    if result.returncode or 'PASS' not in text or ('raw='+str(int(option=='1'))) not in text:
        raise SystemExit('Snapshot check failed: '+str(p))
    runs.append({'option':option,'exitCode':result.returncode,'logSha256':sha(p)})
dependencies = production+[graphics/'include/ColorResolve.hpp', graphics/'include/ColorTargetLayout.hpp']
report={'schemaVersion':1,'status':'production_snapshot_native_adapter_checks_passed',
        'sourceSha256':{str(p.relative_to(source)):sha(p) for p in dependencies},
        'fixtureSha256':sha(fixture),'adapterSha256':sha(adapter),'scriptSha256':sha(Path(__file__)),
        'executableSha256':sha(exe),'commands':commands,'runs':runs,
        'scope':'Actual production helper/layout/transfer, ASan/UBSan and native mapped-range checks; mocked pending flush and generation stamps, no Wine/GPU/device/performance proof.'}
(output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Production raw/legacy resolve snapshot: exact option, samples2/4/8, padding, diagnostics, ordering and retained backing PASS')
