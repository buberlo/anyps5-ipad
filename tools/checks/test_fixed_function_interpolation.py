#!/usr/bin/env python3
"""Compile the production interpolation guard and its conservative rejection cases."""
from pathlib import Path
import argparse, os, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=ROOT/'upstreams/AnyPS5',help='Patched AnyPS5 checkout')
args=parser.parse_args()
SOURCE=args.source.resolve()/'core/shader/recompiler'
INCLUDES=[SOURCE]+[SOURCE/p/'include' for p in ('Translation','Optimization','IntermediateRepresentation','ControlFlow','RdnaDecoder')]
with tempfile.TemporaryDirectory(prefix='anyps5-interpolation-') as tmp:
    target=Path(tmp)/'check'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O0']+[a for i in INCLUDES for a in ('-I',str(i))]+[str(ROOT/'tools/checks/fixed_function_interpolation.cpp'),'-o',str(target)],check=True)
    subprocess.run([str(target)],check=True)
