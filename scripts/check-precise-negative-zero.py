#!/usr/bin/env python3
"""Test the exact MoltenVK SPIRV-Cross dependency; optionally execute emitted MSL.

No downloads, game data, iPad operations or MoltenVK rebuild. The negative control
restores positive-zero FMA addends in generated MSL, without changing source.
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
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'upstreams/MoltenVK/External/SPIRV-Cross'
PATCH = ROOT / 'patches/spirv-cross/0001-precise-multiply-negative-zero.patch'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/precise-negative-zero')
    parser.add_argument('--metal', action='store_true', help='Require real native Mac Metal execution and negative control')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error('--jobs must be between 1 and 4')
    output = args.output.resolve()
    if output == ROOT or ROOT not in output.parents or 'build' not in output.relative_to(ROOT).parts:
        parser.error('--output must be below this project\'s ignored build directory')
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    receipt = {'schemaVersion': 1, 'status': 'started', 'metalRequested': args.metal,
               'boundary': 'Exact production SPIRV-Cross translation and optional native Mac Metal. No rebuilt MoltenVK archive, native iOS app, physical iPad, or game acceptance.'}
    commands = []
    with (output / 'validation.log').open('w') as log:
        def run(command, timeout=300, expect=0):
            command = [str(x) for x in command]
            commands.append(command)
            log.write('$ ' + repr(command) + '\n')
            log.flush()
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=timeout, check=False)
            log.write(result.stdout)
            log.flush()
            if result.returncode != expect:
                raise RuntimeError(f'command exit {result.returncode}, expected {expect}: {command[0]}\n{result.stdout[-6000:]}')
            return result.stdout
        try:
            expected_pin = (ROOT / 'upstreams/MoltenVK/ExternalRevisions/SPIRV-Cross_repo_revision').read_text().strip()
            actual_pin = run(['git', '-C', SOURCE, 'rev-parse', 'HEAD']).strip()
            if actual_pin != expected_pin:
                raise RuntimeError('SPIRV-Cross HEAD differs from actual MoltenVK dependency pin')
            run(['git', '-C', SOURCE, 'apply', '--reverse', '--check', PATCH])
            for name in ('cmake', 'glslangValidator', 'spirv-val', 'spirv-dis', 'xcrun'):
                if not shutil.which(name):
                    raise RuntimeError('Required local tool is unavailable: ' + name)
            cmake_build = output / 'compiler'
            run(['cmake', '-S', SOURCE, '-B', cmake_build, '-DCMAKE_BUILD_TYPE=Release',
                 '-DSPIRV_CROSS_CLI=OFF', '-DSPIRV_CROSS_ENABLE_TESTS=OFF',
                 '-DSPIRV_CROSS_ENABLE_GLSL=ON', '-DSPIRV_CROSS_ENABLE_MSL=ON',
                 '-DSPIRV_CROSS_ENABLE_HLSL=OFF', '-DSPIRV_CROSS_ENABLE_CPP=OFF',
                 '-DSPIRV_CROSS_ENABLE_REFLECT=OFF', '-DSPIRV_CROSS_ENABLE_C_API=OFF',
                 '-DSPIRV_CROSS_ENABLE_UTIL=OFF', '-DSPIRV_CROSS_SKIP_INSTALL=ON',
                 '-DSPIRV_CROSS_NAMESPACE_OVERRIDE=MVK_spirv_cross'])
            run(['cmake', '--build', cmake_build, '--target', 'spirv-cross-msl',
                 '--parallel', args.jobs], timeout=900)
            translator = output / 'translator'
            fixture = ROOT / 'tools/shader-probes/precise-negative-zero.comp'
            driver = ROOT / 'tools/shader-probes/spirv_msl_translate.cpp'
            run(['xcrun', 'clang++', '-std=c++17', '-O2', '-DSPIRV_CROSS_NAMESPACE_OVERRIDE=MVK_spirv_cross',
                 '-I' + str(SOURCE), driver, cmake_build / 'libspirv-cross-msl.a',
                 cmake_build / 'libspirv-cross-glsl.a', cmake_build / 'libspirv-cross-core.a',
                 '-o', translator])
            spv, msl = output / 'fixture.spv', output / 'fixture.metal'
            run(['glslangValidator', '-V', '--target-env', 'vulkan1.1', '-Od', fixture, '-o', spv])
            run(['spirv-val', '--target-env', 'vulkan1.1', spv])
            disassembly = run(['spirv-dis', spv])
            (output / 'fixture.spvasm').write_text(disassembly)
            no_contraction = disassembly.count(' NoContraction')
            if no_contraction < 16:
                raise RuntimeError('Fixture lost required NoContraction decorations')
            run([translator, spv, msl])
            generated = msl.read_text()
            required = ('return fma(l, r, T(-0.0));',
                        'vec<T, Cols> res = vec<T, Cols>(T(-0.0));',
                        'vec<T, Rows> res = vec<T, Rows>(T(-0.0));',
                        'vec<T, LRows> tmp(T(-0.0));')
            if not all(generated.count(x) == 1 for x in required):
                raise RuntimeError('Production translator did not emit all four corrected helpers')
            if 'vec<T, Cols> tmp(0);' not in generated:
                raise RuntimeError('Uninitialized temporary fixture contract unexpectedly changed')
            for expression in ('spvFMulVectorMatrix(', 'spvFMulMatrixVector(', 'spvFMulMatrixMatrix('):
                if generated.count(expression) < (5 if 'MatrixMatrix' in expression else 3):
                    raise RuntimeError('Fixture did not exercise float and half matrix multiplication: ' + expression)
            if not re.search(r'results\s+\[\[buffer\(0\)\]\]', generated) or not re.search(r'inputs\s+\[\[buffer\(1\)\]\]', generated):
                raise RuntimeError('Metal fixture resource indices changed')
            native = {'executed': False}
            if args.metal:
                host = ROOT / 'tools/shader-probes/precise_negative_zero_metal.mm'
                probe = output / 'metal-probe'
                run(['xcrun', 'clang++', '-std=c++17', '-O2', '-fobjc-arc', host,
                     '-framework', 'Metal', '-framework', 'Foundation', '-o', probe])
                native_output = run([probe, msl], timeout=60)
                (output / 'metal-patched.log').write_text(native_output)
                mutation = output / 'positive-zero-control.metal'
                mutated = generated.replace('T(-0.0)', 'T(0)')
                if mutated == generated:
                    raise RuntimeError('Negative-control mutation did not change emitted MSL')
                mutation.write_text(mutated)
                control_output = run([probe, mutation], timeout=60, expect=1)
                (output / 'metal-positive-zero-control.log').write_text(control_output)
                def rows(text):
                    found = re.findall(r'row=(\d+) type=(float|half) actual=([0-9a-f]{8}) expected=([0-9a-f]{8}) pass=([01])', text)
                    if len(found) != 40 or [int(row[0]) for row in found] != list(range(40)):
                        raise RuntimeError('Metal result rows missing or duplicated')
                    return found
                patched_rows, control_rows = rows(native_output), rows(control_output)
                failures = [int(row[0]) for row in control_rows if row[4] == '0']
                negative_zero_rows = [int(row[0]) for row in control_rows if row[3] == '80000000']
                if any(row[4] != '1' for row in patched_rows) or failures != negative_zero_rows:
                    raise RuntimeError('Negative control did not fail exactly the signed-zero channels')
                native = {'executed': True, 'device': re.search(r'^device=(.+)$', native_output, re.M).group(1),
                          'channels': 40, 'floatAndHalf': True, 'scalarVectorMatrix': True,
                          'patchedFailures': 0, 'positiveZeroControlFailures': len(failures),
                          'positiveZeroControlFailedExactlyNegativeZeroChannels': True,
                          'fastMathEnabled': False}
            receipt.update(status='passed', dependencyPin=actual_pin, patchSha256=digest(PATCH),
                           compilerSourceSha256=digest(SOURCE / 'spirv_msl.cpp'),
                           scriptSha256=digest(Path(__file__).resolve()),
                           compilerLibraries=[{'name': name, 'sha256': digest(cmake_build / name)}
                                              for name in ('libspirv-cross-msl.a', 'libspirv-cross-glsl.a', 'libspirv-cross-core.a')],
                           fixtureSha256=digest(fixture), driverSha256=digest(driver),
                           translatorSha256=digest(translator), spirvSha256=digest(spv),
                           generatedMslSha256=digest(msl), noContractionDecorations=no_contraction,
                           nativeMetal=native)
            if args.metal:
                receipt['nativeHostSha256'] = digest(host)
            print('PASS: exact MoltenVK CompilerMSL emits all signed-zero helpers' +
                  ('; native Metal passes 40 channels and rejects the positive-zero control' if args.metal else ''))
        except Exception as error:
            receipt.update(status='failed', error=str(error))
            print(str(error), file=sys.stderr)
        finally:
            receipt.update(elapsedSeconds=time.monotonic() - started, commands=commands)
            (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return 0 if receipt['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
