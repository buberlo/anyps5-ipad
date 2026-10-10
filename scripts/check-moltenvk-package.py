#!/usr/bin/env python3
"""Reject stale MoltenVK archives before linking the patched iOS app."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IOS_ARCHIVE = 'Package/Release/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a'
HELPERS = (
    b"return fma(l, r, T(-0.0));",
    b"vec<T, Cols> res = vec<T, Cols>(T(-0.0));",
    b"vec<T, Rows> res = vec<T, Rows>(T(-0.0));",
    b"vec<T, LRows> tmp(T(-0.0));",
)


def digest(path):
    with path.open('rb') as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
        return result.hexdigest()


def revision(path):
    return subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()


def verify(library, receipt_path):
    mvk = ROOT / 'upstreams/MoltenVK'
    cross = mvk / 'External/SPIRV-Cross'
    receipt = json.loads(receipt_path.read_text())
    if receipt.get('status') != 'built_not_installed' or receipt.get('platform') != 'ios':
        raise ValueError('Expected a successful local iPhoneOS MoltenVK build receipt')
    if receipt.get('moltenvkPin') != revision(mvk) or receipt.get('spirvCrossPin') != revision(cross):
        raise ValueError('MoltenVK build receipt source revisions differ from the checkouts')
    expected_pin = (mvk / 'ExternalRevisions/SPIRV-Cross_repo_revision').read_text().strip()
    if receipt['spirvCrossPin'] != expected_pin:
        raise ValueError('SPIRV-Cross differs from the pinned MoltenVK dependency')
    if receipt.get('spirvCrossPatchSha256') != digest(ROOT / 'patches/spirv-cross/0001-precise-multiply-negative-zero.patch'):
        raise ValueError('MoltenVK archive predates the current SPIRV-Cross patch')
    if receipt.get('spirvCrossEmitterSha256') != digest(cross / 'spirv_msl.cpp'):
        raise ValueError('MoltenVK archive was built from a different MSL emitter')
    actual = digest(library)
    products = [entry for entry in receipt.get('libraries', [])
                if entry.get('path') == IOS_ARCHIVE
                and entry.get('libraryIdentifier') == 'ios-arm64'
                and entry.get('supportedPlatform') == 'ios'
                and not entry.get('supportedPlatformVariant')
                and entry.get('architectures') == ['arm64']]
    if len(products) != 1:
        raise ValueError('Expected exactly one native iPhoneOS ios-arm64 static archive in the receipt')
    if products[0].get('sha256') != actual or products[0].get('bytes') != library.stat().st_size:
        raise ValueError('MoltenVK archive does not match a recorded iPhoneOS build product')
    missing, tail = set(HELPERS), b''
    with library.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            combined = tail + block
            missing.difference_update(helper for helper in tuple(missing) if helper in combined)
            tail = combined[-128:]
    if missing:
        raise ValueError('MoltenVK archive does not contain all four corrected MSL emitter helpers')
    print('Verified patched iPhoneOS MoltenVK archive: ' + actual)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--receipt', type=Path, default=ROOT / 'build/moltenvk-local/ios/receipt.json')
    args = parser.parse_args()
    try:
        verify(args.library.resolve(), args.receipt.resolve())
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + '\nBuild the matching archive with scripts/build-moltenvk-local.sh ios\n')


if __name__ == '__main__':
    main()
