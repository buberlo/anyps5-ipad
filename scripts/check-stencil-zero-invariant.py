#!/usr/bin/env python3
"""Check actual zero-stencil semantic admission and Vulkan recording adapter locally."""
import hashlib, json, os, shlex, subprocess, argparse
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,default=root/'build/zero-stencil-invariant-production/contract');args=p.parse_args()
out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);(out/'report.json').unlink(missing_ok=True)
source=root/'upstreams/AnyPS5';fixture=root/'tools/checks/stencil_zero_invariant_contract.cpp';helper=source/'core/libs/prx/libSceAgcDriver/Graphics/src/StencilZeroInvariant.cpp';binary=out/'contract'
command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all']+['-I'+str(source/path) for path in ['core/libs','core/shader/recompiler','3rdparty/Vulkan-Headers/include','3rdparty/SPIRV-Headers/include']]+[str(fixture),str(helper),'-o',str(binary)]
with (out/'compile.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
with (out/'run.log').open('w') as log:subprocess.run([str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
runs=[]
for value in [None,'0','1','yes']:
    env=os.environ.copy();env.pop('APS5_ZERO_STENCIL_INVARIANT',None)
    if value is not None:env['APS5_ZERO_STENCIL_INVARIANT']=value
    subprocess.run([str(binary),'--env','1' if value=='1' else '0'],env=env,check=True,timeout=30);runs.append({'option':value,'enabled':value=='1'})
paths=[fixture,helper,source/'core/libs/prx/libSceAgcDriver/Graphics/include/StencilZeroInvariant.hpp',Path(__file__).resolve(),out/'compile.log',out/'run.log',binary]
report={'status':'production_admission_and_recording_adapter_passed','scope':'ASan/UBSan actual helper with original semantic fixtures and counted Vulkan adapter; not actual GPU/Draw/Windows/iPad/performance','commands':[command],'exactOptionRuns':runs,'sha256':{str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print((out/'run.log').read_text(),end='')
