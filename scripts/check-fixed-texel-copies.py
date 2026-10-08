#!/usr/bin/env python3
"""Compile production color tiling with optimized MinGW and check its hot copies.

The AMD-reference byte/guard regressions are separately run by
check-msaa-color-layout.py. This check does not execute a game or use a GPU.
Provide a working local Wine runtime and MinGW compiler; nothing is downloaded.
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
parser.add_argument('--output-dir', type=Path, default=root / 'build/fixed-texel-copy/mingw')
parser.add_argument('--source-file', type=Path, help='Optional prior production TU for a compiler/checker negative control')
args = parser.parse_args()
source = args.anyps5_source.resolve()
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
production = (args.source_file or source / 'core/libs/prx/libSceAgcDriver/Graphics/src/ColorTargetLayout.cpp').resolve()
obj = output / 'ColorTargetLayout.o'
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
(output / 'ColorTargetLayout.disassembly.txt').write_text(disassembly)
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
       if 'ColorTargetLayout::' in name and ('::Tile(' in name or '::Detile(' in name)
       and '(.cold)' not in name]
for method in ('Tile', 'Detile'):
    if not any('::' + method + '(' in name for name, _ in hot):
        raise SystemExit('Missing production optimized function: ' + method)
# Literal small copies should lower to instructions in every specialization.
# A call/relocation to a CRT copy in a hot specialization is a failed check.
violations = [(name, line.strip()) for name, body in hot for line in body.splitlines()
              if re.search(r'\b(memcpy|memmove|memset)\b', line)]
if violations:
    raise SystemExit('CRT memory routine remains in a hot color-copy function: ' + repr(violations))
report = {
    'schemaVersion': 1, 'status': 'optimized_mingw_production_copy_check_passed',
    'optimization': '-O3 -DNDEBUG', 'command': command,
    'sourceSha256': hashlib.sha256(production.read_bytes()).hexdigest(),
    'objectSha256': hashlib.sha256(obj.read_bytes()).hexdigest(),
    'disassemblySha256': hashlib.sha256(disassembly.encode()).hexdigest(),
    'checkedFunctions': [name for name, _ in hot], 'crtCopyMentionsInHotFunctions': 0,
    'scope': 'Actual production translation unit; no game execution, GPU or speed measurement.'
}
(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('Optimized MinGW production Tile/Detile: no CRT memory-copy call in', len(hot), 'hot functions; source/obj/disassembly hashes recorded')
