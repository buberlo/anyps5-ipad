#!/usr/bin/env python3
"""Test production graphics inspector on original opaque-descriptor shaders.

Uses ASan/UBSan, production ValidateShaders, resolve PS factory and canonical
rectangle stages. No GPU, device, private shader data or full HLE build.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import sys
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,default=root/'upstreams/AnyPS5')
p.add_argument('--build',type=Path)
p.add_argument('--shader-validation-source',type=Path)
p.add_argument('--expect-legacy-rejection',action='store_true')
a=p.parse_args();source=a.source.resolve()
vertex='''#version 450
out gl_PerVertex { vec4 gl_Position; };
void main() { vec2 p=vec2(gl_VertexIndex & 1,gl_VertexIndex >> 1); gl_Position=vec4(p*2-1,0,1); }
'''
separate='''#version 450
layout(set=0,binding=0) uniform texture2D tex;
layout(set=0,binding=1) uniform sampler smp;
layout(location=0) out vec4 color;
void main() { color=texture(sampler2D(tex,smp),vec2(0.5)); }
'''
storage='''#version 450
layout(rgba8,set=0,binding=0) readonly uniform image2D tex;
layout(location=0) out vec4 color;
void main() { color=imageLoad(tex,ivec2(0)); }
'''
combined='''#version 450
layout(set=0,binding=0) uniform sampler2D tex;
layout(location=0) out vec4 color;
void main() { color=texture(tex,vec2(0.5)); }
'''
def run(command):
    r=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if r.returncode:sys.stderr.write(r.stdout);raise subprocess.CalledProcessError(r.returncode,command)
    return r.stdout

def check(build):
    build.mkdir(parents=True,exist_ok=True);fixtures=build/'fixtures';fixtures.mkdir(exist_ok=True)
    for name,stage,text in [('vertex','vert',vertex),('separate','frag',separate),
      ('separate-array-one','frag',separate.replace('texture2D tex;', 'texture2D tex[1];').replace('sampler smp;','sampler smp[1];').replace('sampler2D(tex,smp)','sampler2D(tex[0],smp[0])')),
      ('separate-array','frag',separate.replace('texture2D tex;', 'texture2D tex[2];').replace('sampler smp;','sampler smp[2];').replace('sampler2D(tex,smp)','sampler2D(tex[1],smp[1])')),
      ('storage','frag',storage),('combined','frag',combined)]:
        file=fixtures/(name+'.'+stage);file.write_text(text)
        run([os.environ.get('GLSLANG_VALIDATOR','glslangValidator'),'-V','--target-env','vulkan1.1',str(file),'-o',str(fixtures/(name+'.spv'))])
        run([os.environ.get('SPIRV_VAL','spirv-val'),'--target-env','vulkan1.1',str(fixtures/(name+'.spv'))])
    rc=source/'core/shader/recompiler';gfx=source/'core/libs/prx/libSceAgcDriver/Graphics'
    includes=[source/'core/libs',rc,source/'3rdparty/Vulkan-Headers/include',source/'3rdparty/SPIRV-Headers/include']+sorted(rc.glob('*/include'))
    executable=build/'check'
    validation=a.shader_validation_source.resolve() if a.shader_validation_source else gfx/'src/ShaderValidation.cpp'
    cmd=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections','-DANYPS5_ENABLE_SPIRV_TOOLS=0']
    cmd+=['-I'+str(x) for x in includes]
    cmd+=[str(root/'tools/checks/opaque_descriptor_shader_validation.cpp'),str(validation),str(gfx/'src/ColorResolveFragment.cpp'),str(rc/'SpirvBackend/src/RectListShaders.cpp'),str(rc/'SpirvBackend/src/SpirvModule.cpp'),'-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections','-o',str(executable)]
    run(cmd);generated=build/'generated';generated.mkdir(exist_ok=True)
    command=[str(executable),str(fixtures),str(generated)]
    if a.expect_legacy_rejection:command+=['legacy']
    print(run(command),end='')
    if not a.expect_legacy_rejection:
        shaders=sorted(generated.glob('*.spv'))
        for file in shaders:run([os.environ.get('SPIRV_VAL','spirv-val'),'--target-env','vulkan1.2',str(file)])
        print('Original6 fixtures and production-generated'+str(len(shaders))+' factory/rectangle modules passed independent spirv-val.')
if a.build:check(a.build.resolve())
else:
    with tempfile.TemporaryDirectory(prefix='aps5-opaque-types-') as directory:check(Path(directory))
