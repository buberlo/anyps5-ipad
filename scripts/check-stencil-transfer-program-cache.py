#!/usr/bin/env python3
"""Check production immutable stencil program ownership/keys; no GPU claim."""
import os,shlex,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1];source=root/'upstreams/AnyPS5'
subprocess.run(['python3',str(root/'scripts/check-stencil-sample-transfer.py'),'--shaders-only'],check=True)
with tempfile.TemporaryDirectory(prefix='anyps5-stencil-program-cache-') as temporary:
    binary=Path(temporary)/'contract'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(source/'core/libs'),'-I'+str(source/'core/shader/recompiler'),'-I'+str(source/'3rdparty/Vulkan-Headers/include'),str(root/'tools/checks/stencil_transfer_program_cache_contract.cpp'),str(source/'core/libs/prx/libSceAgcDriver/Graphics/src/StencilSampleTransfer.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
