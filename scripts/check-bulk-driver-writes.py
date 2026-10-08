#!/usr/bin/env python3
"""Check actual AGC tracking and Windows shared HostWrite scopes locally.

Uses existing MinGW/Wine only. The test interposes VirtualProtect solely in
the compiled GuestArena object, delegates ordinary calls to Windows, and
injects bounded failures. No device, GPU, downloads or GitHub Actions.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--wine', type=Path, required=True)
parser.add_argument('--wine-prefix', type=Path)
parser.add_argument('--mingw-cxx', type=Path, default=root / 'build/toolchains/winlibs/mingw64/bin/g++.exe')
parser.add_argument('--anyps5-source', type=Path, default=root / 'upstreams/AnyPS5')
parser.add_argument('--output-dir', type=Path, default=root / 'build/bulk-driver-writes/windows')
args = parser.parse_args()
source = args.anyps5_source.resolve()
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
(output / 'report.json').unlink(missing_ok=True)
env = os.environ.copy()
env['WINEDEBUG'] = '-all'
env['MVK_CONFIG_LOG_LEVEL'] = '0'
if args.wine_prefix:
    env['WINEPREFIX'] = str(args.wine_prefix.resolve())
def win(path):
    return 'Z:' + str(path.resolve()).replace('/', '\\')
base = [str(args.wine.resolve()), win(args.mingw_cxx), '-std=c++20', '-O2', '-DNOMINMAX',
        '-DAPS5_ENABLE_TIMING_LOG=0', '-DAPS5_SLIM', '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00',
        '-I' + win(source / 'core/libs'), '-I' + win(source / 'core/shader/recompiler')]
inputs = [
    source / 'core/libs/prx/libSceAgcDriver/Execution/src/GuestMemory.cpp',
    source / 'core/libs/prx/libc/src/GuestArena.cpp',
    source / 'core/libs/prx/libc/src/GuestAllocations.cpp',
    root / 'tools/checks/bulk_driver_write_windows.cpp',
]
source_dependencies = inputs[:3] + [
    source / 'core/libs/prx/libc/include/WindowsMappings.hpp',
    source / 'core/libs/prx/libSceAgcDriver/tests/WriteTracking.cpp',
]
fixture_dependencies = [inputs[-1], root / 'tools/checks/bulk_driver_write_protection.hpp',
                        Path(__file__).resolve()]
commands, objects = [], []
with (output / 'compile.log').open('w') as log:
    for path in inputs:
        obj = output / (path.stem + '.o')
        command = base + (['-include', win(root / 'tools/checks/bulk_driver_write_protection.hpp')]
                          if path.stem == 'GuestArena' else [])
        command += ['-c', win(path), '-o', win(obj)]
        commands.append(command)
        subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        objects.append(obj)
    exe = output / 'bulk-driver-write-tests.exe'
    command = base + [win(obj) for obj in objects] + ['-static-libgcc', '-static-libstdc++', '-o', win(exe)]
    commands.append(command)
    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
pthread = args.mingw_cxx.resolve().parent / 'libwinpthread-1.dll'
if pthread.exists():
    shutil.copy2(pthread, output / pthread.name)
runs = []
for option in (None, '0', '1', 'yes'):
    runenv = env.copy()
    for key in tuple(runenv):
        if key.startswith('APS5_'):
            del runenv[key]
    runenv.update(APS5_GUEST_ARENA_BASE='0x7400000000', APS5_GUEST_ARENA_SIZE='0x10000000',
                  APS5_GUEST_ARENA_CHUNK='0x10000000', APS5_GUEST_ARENA_LAZY='1')
    if option is not None:
        runenv['APS5_BULK_DRIVER_WRITES'] = option
    logfile = output / ('run-' + (option or 'unset') + '.log')
    command = [str(args.wine.resolve()), win(exe)]
    with logfile.open('w') as log:
        result = subprocess.run(command, env=runenv, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    text = logfile.read_text()
    if result.returncode != 0 or 'scope checks=7 ' not in text or 'write tracking tests passed' not in text:
        raise SystemExit('Actual Windows bulk-write check failed: ' + str(logfile))
    scoped = '[guestmem] scoped bulk driver write executed with a registered mapping lease' in text
    if scoped != (option == '1'):
        raise SystemExit('Exact opt-in scope execution differs: ' + str(logfile))
    runs.append({'option': option, 'exitCode': result.returncode, 'scopedBulkWriteExecuted': scoped,
                 'logSha256': hashlib.sha256(logfile.read_bytes()).hexdigest()})
report = {
    'status': 'actual_windows_shared_scope_and_agc_tracking_checks_passed',
    'sourceSha256': {str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in source_dependencies},
    'fixtureSha256': {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in fixture_dependencies},
    'executableSha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
    'commands': commands, 'runs': runs,
    'scope': 'Real local Wine Windows mappings and production AGC tracking; test-only syscall failure injection and own VEH, no ARM64EC/iPad/GPU/performance proof.'
}
(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('Actual Windows shared HostWrite + AGC classifications: four exact-option runs passed; no device/performance claim')
