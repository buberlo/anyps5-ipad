#!/usr/bin/env python3
"""Compile production depth/stencil tiling with optimized MinGW and inspect copies.

Run check-msaa-depth-layout.py separately for the independent AMD AddrLib oracle,
misaligned span guards and sample round trips. This check downloads nothing and
does not execute a game or use a GPU. --expect-crt-copies verifies a prior source
negative control rather than accepting hot copy calls in the current source.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--wine', type=Path, required=True)
parser.add_argument('--wine-prefix', type=Path)
parser.add_argument('--mingw-cxx', type=Path, default=root / 'build/toolchains/winlibs/mingw64/bin/g++.exe')
parser.add_argument('--objdump', type=Path, default=Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-objdump'))
parser.add_argument('--anyps5-source', type=Path, default=root / 'upstreams/AnyPS5')
parser.add_argument('--output-dir', type=Path, default=root / 'build/fixed-depth-texel-copy/mingw')
parser.add_argument('--source-file', type=Path, help='Prior production TU for a compiler/checker negative control')
parser.add_argument('--expect-crt-copies', action='store_true', help='Require a CRT memcpy reference in both prior Tile/Detile hot functions')
args = parser.parse_args()
if args.expect_crt_copies and not args.source_file:
    parser.error('--expect-crt-copies requires an explicit prior --source-file')
source = args.anyps5_source.resolve()
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
# A failed rerun must never leave an earlier PASS artifact looking current.
report_path = output / 'report.json'
report_path.unlink(missing_ok=True)
production = (args.source_file or source / 'core/libs/prx/libSceAgcDriver/Graphics/src/DepthTargetLayout.cpp').resolve()
obj = output / 'DepthTargetLayout.o'

def win(path):
    return 'Z:' + str(path.resolve()).replace('/', '\\')

env = os.environ.copy()
env['WINEDEBUG'] = '-all'
if args.wine_prefix:
    env['WINEPREFIX'] = str(args.wine_prefix.resolve())
command = [str(args.wine.resolve()), win(args.mingw_cxx), '-std=c++20', '-O3', '-DNDEBUG',
           '-DAPS5_ENABLE_TIMING_LOG=0', '-DAPS5_SLIM', '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00',
           '-I' + win(source / 'core/libs'), '-I' + win(source / 'core/shader/recompiler'),
           '-I' + win(source / '3rdparty/Vulkan-Headers/include'), '-c', win(production), '-o', win(obj)]
with (output / 'compile.log').open('w') as log:
    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
disassembly = subprocess.run([str(args.objdump.resolve()), '--disassemble', '--reloc', '--demangle', str(obj)],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout.decode('utf8', 'replace')
(output / 'DepthTargetLayout.disassembly.txt').write_text(disassembly)
functions = []
current = None
for line in disassembly.splitlines():
    header = re.match(r'^[0-9a-fA-F]+ <(.+)>:$', line)
    if header:
        current = [header.group(1), []]
        functions.append(current)
    elif current:
        current[1].append(line)
hot = [(name, '\n'.join(lines)) for name, lines in functions
       if 'DepthTargetLayout::' in name and ('::Tile(' in name or '::Detile(' in name)
       and '(.cold)' not in name]
for method in ('Tile', 'Detile'):
    matching = [(name, body) for name, body in hot if '::' + method + '(' in name]
    if not matching:
        raise SystemExit('Missing production optimized function: ' + method)
    if args.expect_crt_copies and not any(re.search(r'\bmemcpy\b', body) for _, body in matching):
        raise SystemExit('Prior production negative control lost expected memcpy reference: ' + method)
mentions = [(name, line.strip()) for name, body in hot for line in body.splitlines()
            if re.search(r'\b(memcpy|memmove|memset)\b', line)]
if mentions and not args.expect_crt_copies:
    raise SystemExit('CRT memory routine remains in a hot depth-copy function: ' + repr(mentions))
report = {
    'schemaVersion': 1,
    'status': 'prior_optimized_mingw_crt_negative_control_confirmed' if args.expect_crt_copies else 'optimized_mingw_production_copy_check_passed',
    'optimization': '-O3 -DNDEBUG', 'command': command,
    'sourceSha256': hashlib.sha256(production.read_bytes()).hexdigest(),
    'objectSha256': hashlib.sha256(obj.read_bytes()).hexdigest(),
    'disassemblySha256': hashlib.sha256(disassembly.encode()).hexdigest(),
    'checkedFunctions': [name for name, _ in hot],
    'crtCopyMentionsInHotFunctions': len(mentions), 'crtCopyMentions': mentions,
    'scope': 'Actual production translation unit; no game execution, GPU or speed measurement.'
}
report_path.write_text(json.dumps(report, indent=2) + '\n')
print(report['status'], 'functions=', len(hot), 'CRT_copy_mentions=', len(mentions))
