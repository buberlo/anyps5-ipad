#!/usr/bin/env python3
"""Check the production render-target API contract and failure unwinding, not GPU execution."""
import os, shlex, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parent.parent
source=root/'upstreams/AnyPS5'
with tempfile.TemporaryDirectory(prefix='anyps5-render-target-') as out:
    binary=Path(out)/'test'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+[
        '-std=c++20','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-I'+str(source/'core/libs'),'-I'+str(source/'core/shader/recompiler'),
        '-I'+str(source/'3rdparty/Vulkan-Headers/include'),
        str(root/'tools/checks/color_render_target_contract.cpp'),
        str(source/'core/libs/prx/libSceAgcDriver/Graphics/src/ColorRenderTarget.cpp'),
        '-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
