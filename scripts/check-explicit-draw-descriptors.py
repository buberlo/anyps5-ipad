#!/usr/bin/env python3
"""Locally validate owned initial descriptor replay; no GPU or game is involved."""
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
source = args.anyps5_source.resolve()
with tempfile.TemporaryDirectory(prefix='anyps5-explicit-descriptors-') as temp:
    executable = Path(temp) / 'check'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++20', '-O1', '-g', '-UNDEBUG', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-I' + str(source / 'core/libs'),
        '-I' + str(source / '3rdparty/Vulkan-Headers/include'),
        str(root / 'tools/checks/explicit_draw_descriptors.cpp'), '-o', str(executable),
    ], check=True)
    for value in (None, '0', 'true', '1'):
        environment = dict(os.environ)
        environment.pop('APS5_EXPLICIT_DRAW_DESCRIPTORS', None)
        if value is not None:
            environment['APS5_EXPLICIT_DRAW_DESCRIPTORS'] = value
        command = [str(executable)] + (['--enabled'] if value == '1' else [])
        subprocess.run(command, env=environment, check=True, timeout=30)
