#!/usr/bin/env python3
"""Compile real AnyPS5 IR passes; prove invariant exports without hiding subgroups."""
from pathlib import Path
import os,shlex,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'upstreams/AnyPS5/core/shader/recompiler'
INCLUDES=[SOURCE,SOURCE/'IntermediateRepresentation/include',SOURCE/'Optimization/include',SOURCE/'SpirvBackend/include',SOURCE/'ControlFlow/include',SOURCE/'RdnaDecoder/include',ROOT/'upstreams/AnyPS5/core/libs',ROOT/'upstreams/AnyPS5/3rdparty/SPIRV-Headers/include']
SOURCES=list((SOURCE/'IntermediateRepresentation/src').glob('*.cpp'))+list((SOURCE/'IntermediateRepresentation/src/IrBuilder').glob('*.cpp'))+[SOURCE/'Optimization/src/ConstantFolder.cpp',SOURCE/'Optimization/src/DeadCodeEliminator.cpp',SOURCE/'SpirvBackend/src/SpirvAnalysis.cpp',ROOT/'tools/checks/masked_shift_fold.cpp']
with tempfile.TemporaryDirectory(prefix='anyps5-masked-shift-') as tmp:
 target=Path(tmp)/'check'
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O0']+[a for i in INCLUDES for a in ('-I',str(i))]+[str(p) for p in SOURCES]+['-o',str(target)],check=True)
 subprocess.run([str(target)],check=True)
