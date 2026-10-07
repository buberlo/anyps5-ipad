#!/usr/bin/env python3
"""Build AnyPS5 HLE locally with the pinned supported WinLibs compiler.

Run on Windows, or set APS5_WINE to a local Wine executable on macOS/Linux.
Positional targets select any existing PRX targets; omission builds every PRX.
No CI, device, game dump, or cloud execution is involved.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '15.2.0posix-14.0.0-ucrt-r7'
TOOLCHAIN_URL = f'https://github.com/brechtsanders/winlibs_mingw/releases/download/{VERSION}/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-14.0.0-r7.7z'
TOOLCHAIN_SHA = 'a914feafd7462126637d4b8196a31f3fb856ad929768ceb092c99969b43675b3'
FFMPEG_COMMIT = '9ac4cfd195f1'
FFMPEG_SHA = 'e4be9938b321da5f53e248d8ae947c984390e09d0f59fb6855644a972126e26b'
PYTHON_SHA = '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
TESTS = ('windows_exception_tests', 'guest_formatting_tests', 'host_thread_local_tests',
         'guest_environment_tests', 'file_position_tests', 'guest_math_tests',
         'guest_json_tests', 'guest_json2_initialization_tests', 'guest_compatibility_api_tests',
         'guest_filesystem_tests', 'guest_pthread_attr_tests', 'guest_shader_alignment_tests',
         'guest_memory_tests', 'guest_raise_exception_tests',
         'uniform_wave_branch_tests', 'wave32_wide_subgroup_tests',
         'audio_out2_pad_mix_tests', 'audio_out_mix_level_pad_spk_tests',
         'audio_out_last_output_time_tests', 'audio_out2_latency_tests',
         'audio_out2_port_layouts_tests', 'audio_out2_timing_tests')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(url, destination, expected):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.is_file():
        temporary = destination.with_suffix(destination.suffix + '.download')
        subprocess.run(['curl', '-fL', '--retry', '3', '-o', str(temporary), url], check=True)
        if digest(temporary) != expected:
            raise ValueError('Download checksum mismatch: ' + destination.name)
        temporary.rename(destination)
    if digest(destination) != expected:
        raise ValueError('Cached archive checksum mismatch: ' + destination.name)


def choose_targets(source, explicit, manifest):
    available = {p.name for p in (source / 'core/libs/prx').iterdir() if p.is_dir()}
    targets = list(explicit)
    if manifest:
        with zipfile.ZipFile(manifest) as archive:
            info = json.loads(archive.read('hle-manifest.json'))
        targets += [Path(name).stem for name in info['files'] if name.startswith('unpatched/') and name.endswith('.prx')]
    if not targets:
        targets = sorted(available)
    for name in targets:
        if not re.fullmatch(r'lib[A-Za-z0-9_.-]+', name) or name not in available:
            raise ValueError('Unknown PRX target: ' + name)
    return sorted(set(targets) | {'libc', 'libkernel'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('targets', nargs='*', help='PRX CMake targets; default: all available targets')
    parser.add_argument('--from-hle', type=Path, help='Select targets from a previous public HLE archive, without copying binaries')
    parser.add_argument('--build', type=Path, default=ROOT / 'build/anyps5-winlibs-local')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--source', type=Path, default=ROOT / 'upstreams/AnyPS5',
                        help='Patched AnyPS5 checkout (use an isolated checkout for pin upgrades)')
    parser.add_argument('--wine', default=os.environ.get('APS5_WINE'))
    parser.add_argument('--toolchain', type=Path, default=ROOT / 'build/toolchains/winlibs/mingw64')
    parser.add_argument('--ffmpeg', type=Path, default=ROOT / f'build/toolchains/ffmpeg-{FFMPEG_COMMIT}')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/hle-runtime/hle-runtime.zip')
    parser.add_argument('--skip-tests', action='store_true', help='Export as untested; never mark runtime verified')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    source = args.source.resolve()
    targets = choose_targets(source, args.targets, args.from_hle)
    if os.name != 'nt' and not args.wine:
        parser.error('Set APS5_WINE or --wine to the local Wine executable')
    args.build.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    toolarchive = ROOT / 'build/toolchains/winlibs-15.2.0posix-seh.7z'
    fetch(TOOLCHAIN_URL, toolarchive, TOOLCHAIN_SHA)
    if not (args.toolchain / 'bin/g++.exe').is_file():
        extractor = shutil.which('7zz') or shutil.which('7z')
        if not extractor:
            raise ValueError('7zz/7z is required to extract the pinned compiler')
        subprocess.run([extractor, 'x', '-y', '-o' + str(args.toolchain.parent), str(toolarchive)], check=True)
    ffarchive = ROOT / f'build/toolchains/{FFMPEG_COMMIT}-ffmpeg-mingw-x64.zip'
    fetch(f'https://github.com/KytyPS5/ext-ffmpeg-core/releases/download/{FFMPEG_COMMIT}/ffmpeg-mingw-x64.zip', ffarchive, FFMPEG_SHA)
    if not (args.ffmpeg / 'include/libavcodec/avcodec.h').is_file():
        args.ffmpeg.mkdir(parents=True, exist_ok=True)
        # Only the hash-pinned public dependency archive is extracted here.
        with zipfile.ZipFile(ffarchive) as archive:
            archive.extractall(args.ffmpeg)

    python_archive = ROOT / 'build/toolchains/python-3.12.10-embed-amd64.zip'
    python_home = ROOT / 'build/toolchains/python-3.12.10'
    fetch('https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip', python_archive, PYTHON_SHA)
    if not (python_home / 'python.exe').is_file():
        python_home.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(python_archive) as archive:
            archive.extractall(python_home)

    def win(path):
        path = Path(path).resolve()
        return str(path) if os.name == 'nt' else 'Z:' + str(path).replace('/', '\\')

    def quote(value):
        value = str(value)
        if re.search(r'[\r\n"%!^&|<>]', value):
            raise ValueError('Unsupported command character: ' + value)
        return '"' + value + '"'

    environment = dict(os.environ)
    environment['APS5_SOURCE_COMMIT'] = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    if os.name != 'nt':
        environment.update(WINEPREFIX=str(ROOT / 'build/toolchains/winlibs-wine-prefix'),
                           WINEDEBUG='-all', WINEDLLOVERRIDES='winemenubuilder.exe=d')
    def run(name, command, extra_path=(), extra_environment=None, working_directory=None, timeout=None):
        batch = args.build / (name + '.cmd')
        search = [args.toolchain / 'bin', *extra_path]
        body = '@echo off\r\nset "PATH=' + ';'.join(win(p) for p in search) + ';%PATH%"\r\n'
        body += ' '.join(quote(v) for v in command) + '\r\nexit /b %ERRORLEVEL%\r\n'
        batch.write_bytes(body.encode('utf-8'))
        # Tests deliberately change cwd; resolve the batch path before invoking cmd.
        invocation = ['cmd', '/c', win(batch)] if os.name == 'nt' else [args.wine, 'cmd', '/c', win(batch)]
        log = args.build / (name + '.log')
        print(name + ': ' + str(log), flush=True)
        with log.open('w') as stream:
            subprocess.run(invocation, cwd=working_directory or ROOT, env={**environment, **(extra_environment or {})},
                           stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=timeout)

    run('compiler-version', ['g++', '--version'])
    if '15.2.0' not in (args.build / 'compiler-version.log').read_text():
        raise ValueError('Expected the pinned WinLibs GCC15.2 compiler')
    run('configure', ['cmake', '-S', win(source), '-B', win(args.build), '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++',
                     '-DCMAKE_OBJCOPY=' + win(args.toolchain / 'bin/objcopy.exe'),
                     '-DBUILD_TESTING=ON', '-DAPS5_SLIM=ON', '-DAPS5_ENABLE_TIMING_LOG=OFF',
                     '-DAPS5_AGC_CREATE_LOG=OFF', '-DFFMPEG_PREBUILT_DIR=' + win(args.ffmpeg),
                     '-DPython3_EXECUTABLE=' + win(python_home / 'python.exe')])
    run('build', ['cmake', '--build', win(args.build), '--target', 'relinker', 'nid_patcher',
                  *targets, *TESTS, '--parallel', str(args.jobs)])
    passed = []
    # These CPU/API contracts need an address arena, not the full console map.
    # Reserve 8–12 GiB lazily; an explicit caller setting is still respected.
    test_arena = {name: environment.get(name, value) for name, value in {
        'APS5_GUEST_ARENA_LAZY': '1', 'APS5_GUEST_ARENA_BASE': '0x200000000',
        'APS5_GUEST_ARENA_SIZE': '0x100000000', 'APS5_GUEST_ARENA_CHUNK': '0x10000000',
    }.items()}
    if not args.skip_tests:
        for target in TESTS:
            run('test-' + target, [win(args.build / 'tests' / (target + '.exe'))],
                [args.build / 'core/libs/libs/unpatched', args.build / 'tests'], test_arena, args.build, timeout=45)
            passed.append(target)
    provenance = {
        'schema': 1, 'kind': 'local_anyps5_hle_build', 'targets': targets,
        'project_commit': environment['APS5_SOURCE_COMMIT'],
        'compiler_archive_sha256': TOOLCHAIN_SHA, 'ffmpeg_archive_sha256': FFMPEG_SHA,
        'python_archive_sha256': PYTHON_SHA,
        'compiler_files': {str(p.relative_to(args.toolchain)): digest(p)
                           for p in sorted(args.toolchain.rglob('*.exe'))
                           if p.name in ('g++.exe', 'gcc.exe', 'cc1.exe', 'cc1plus.exe', 'collect2.exe', 'ld.exe', 'cmake.exe', 'ninja.exe')},
        'ffmpeg_libraries': {p.name: digest(p) for p in sorted((args.ffmpeg / 'lib').glob('*.a'))},
        'anyps5_commit': subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
        'patches': {p.name: digest(p) for p in sorted((ROOT / 'patches/anyps5').glob('*.patch'))},
        'anyps5_diff_sha256': hashlib.sha256(subprocess.check_output(['git', '-C', str(source), 'diff', 'HEAD'])).hexdigest(),
        'flags': {'build_type': 'Release', 'APS5_SLIM': True, 'APS5_ENABLE_TIMING_LOG': False, 'APS5_AGC_CREATE_LOG': False},
        'host_tests_passed': passed, 'host_test_runtime': 'Windows' if os.name == 'nt' else 'local Wine',
        'host_test_arena': test_arena if passed else None,
        'device_or_gameplay_verified': False,
    }
    provenance_path = args.output.with_suffix('.provenance.json')
    provenance_path.write_text(json.dumps(provenance, indent=2) + '\n')
    # Stage only this invocation's selected PRXs; stale output from a previous
    # target selection must never enter a newly exported archive.
    import tempfile
    with tempfile.TemporaryDirectory(prefix='anyps5-hle-stage-') as temporary:
        stage = Path(temporary)
        for name in targets:
            shutil.copy2(args.build / 'core/libs/libs/unpatched' / (name + '.prx'), stage)
        subprocess.run([sys.executable, str(ROOT / 'scripts/export-hle-runtime.py'),
                        '--unpatched', str(stage), '--runtime-dir', str(args.toolchain / 'bin'),
                        '--output', str(args.output), '--provenance', str(provenance_path)], env=environment, check=True)
    provenance['archive_sha256'] = digest(args.output)
    provenance_path.write_text(json.dumps(provenance, indent=2) + '\n')
    print('Exported locally built HLE: ' + str(args.output))


if __name__ == '__main__':
    main()
