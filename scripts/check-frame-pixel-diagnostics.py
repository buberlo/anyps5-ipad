#!/usr/bin/env python3
"""Locally check bounded original pixel-statistics fixtures without a GPU or game."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--anyps5-source', type=Path, default=root / 'upstreams/AnyPS5')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='anyps5-frame-pixels-') as temp:
    executable = Path(temp) / 'check'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++20', '-O1', '-g', '-UNDEBUG', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-I' + str(args.anyps5_source.resolve() / 'core/libs'),
        str(root / 'tools/checks/frame_pixel_diagnostics.cpp'), '-o', str(executable),
    ], check=True)
    disabled = dict(os.environ)
    disabled.pop('APS5_FRAME_PIXEL_DIAGNOSTICS', None)
    subprocess.run([str(executable)], env=disabled, check=True, timeout=30)
    zero = dict(disabled, APS5_FRAME_PIXEL_DIAGNOSTICS='0')
    subprocess.run([str(executable)], env=zero, check=True, timeout=30)
    enabled = dict(disabled, APS5_FRAME_PIXEL_DIAGNOSTICS='1')
    subprocess.run([str(executable), '--enabled'], env=enabled, check=True, timeout=30)
