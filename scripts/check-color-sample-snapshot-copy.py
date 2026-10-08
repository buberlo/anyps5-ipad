#!/usr/bin/env python3
"""Local actual-helper snapshot ownership checks; no GPU, game or device claim."""
import argparse,hashlib,json,os,shlex,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',type=Path,default=root/'upstreams/AnyPS5');p.add_argument('--output-dir',type=Path,default=root/'build/gpu-color-sample-staging-copy-production/contract');args=p.parse_args()
source=root/'upstreams/AnyPS5';overlay=args.source_root.resolve();out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);(out/'report.json').unlink(missing_ok=True)
def production(relative):
 candidate=overlay/relative
 return candidate if candidate.exists() else source/relative
helper=production('core/libs/prx/libSceAgcDriver/Graphics/src/ColorSampleSnapshotCopy.cpp');header=production('core/libs/prx/libSceAgcDriver/Graphics/include/ColorSampleSnapshotCopy.hpp');fixture=root/'tools/checks/color_sample_snapshot_copy_contract.cpp'
base=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(overlay/'core/libs')]+['-I'+str(source/path) for path in ['core/libs','core/shader/recompiler','3rdparty/Vulkan-Headers/include']]
rest=[str(source/'core/libs/prx/libSceAgcDriver/Graphics/src/GpuColorSampleTiler.cpp'),str(source/'core/libs/prx/libSceAgcDriver/Graphics/src/ColorTargetLayout.cpp')]
commands=[]
def compile(path,binary,log):
 command=base+[str(fixture),str(path),*rest,'-o',str(binary)];commands.append(command)
 with log.open('w')as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
binary=out/'contract';compile(helper,binary,out/'compile.log')
with(out/'run.log').open('w')as f:subprocess.run([str(binary)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
options=[]
for option in [None,'0','1','yes','01','true',' 1','1 ']:
 env=os.environ.copy();env.pop('APS5_GPU_COLOR_SAMPLE_STAGING_COPY',None)
 if option is not None:env['APS5_GPU_COLOR_SAMPLE_STAGING_COPY']=option
 subprocess.run([str(binary),'--env','1' if option=='1' else '0'],env=env,check=True,timeout=30);options.append({'option':option,'enabled':option=='1'})
negative=[]
text=helper.read_text()
for name,old,new in [('missing-copy','copy(commands, baseline->Handle(), result->Handle(), 1, &region);','/* missing copy negative control */'),('missing-compute-visibility','before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;','before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;')]:
 if old not in text:raise SystemExit('Negative control source site missing')
 changed=out/(name+'.cpp');changed.write_text(text.replace(old,new,1));exe=out/(name+'.contract');compile(changed,exe,out/(name+'-compile.log'))
 with(out/(name+'-run.log')).open('w')as f:result=subprocess.run([str(exe)],stdout=f,stderr=subprocess.STDOUT,timeout=30)
 if result.returncode==0:raise SystemExit('Broken helper falsely passed: '+name)
 negative.append({'name':name,'exitCode':result.returncode,'rejected':True})
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
paths=[fixture,helper,header,Path(__file__).resolve(),*map(Path,rest),binary,out/'compile.log',out/'run.log']
report={'status':'actual_snapshot_owner_contract_passed','scope':'ASan/UBSan actual snapshot helper; modeled coherent Buffer allocation, reader flush/range and CommandBatch cancellation/submitted-wait ordering; no actual GPU/BufferPool/GuestArena/Draw/iPad/performance proof','commands':commands,'exactFlagRuns':options,'negativeControls':negative,'sha256':{str(path.relative_to(root)):sha(path) for path in paths},'negativeArtifactHashes':{path.name:sha(path) for path in out.glob('missing-*') if path.is_file()}}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print((out/'run.log').read_text(),end='')
