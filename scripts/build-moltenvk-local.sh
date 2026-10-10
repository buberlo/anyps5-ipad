#!/usr/bin/env bash
# Rebuild initialized pinned MoltenVK dependencies and package for one platform.
# No downloads, forced checkouts, app signing, device operations or Actions.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-}"
case "$platform" in
    ios) os=iOS; package_scheme='MoltenVK Package (iOS only)' ;;
    macos) os=macOS; package_scheme='MoltenVK Package (macOS only)' ;;
    *) echo "Usage: $0 ios|macos" >&2; exit 1 ;;
esac
[ "$#" = 1 ] || { echo "Expected exactly one platform" >&2; exit 1; }
[ "$(uname -s)" = Darwin ] || { echo "A macOS Xcode host is required" >&2; exit 1; }
jobs="${APS5_BUILD_JOBS:-2}"
case "$jobs" in 1|2|3|4) ;; *) echo "APS5_BUILD_JOBS must be 1..4" >&2; exit 1 ;; esac
mvk="$root/upstreams/MoltenVK"
# fetchDependencies uses checkout --force. It is deliberately not called by
# this wrapper. Initialize fresh dependencies separately, then patch and build.
for name in SPIRV-Cross SPIRV-Tools Vulkan-Headers cereal; do
    repo="$mvk/External/$name"
    [ -d "$repo/.git" ] || { echo "Missing initialized dependency $name" >&2; exit 1; }
    expected="$(tr -d '[:space:]' < "$mvk/ExternalRevisions/${name}_repo_revision")"
    actual="$(git -C "$repo" rev-parse HEAD)"
    [ "$actual" = "$expected" ] || { echo "Dependency pin mismatch: $name ($actual != $expected)" >&2; exit 1; }
done
"$root/scripts/apply-patches.sh" --only spirv-cross
out="$root/build/moltenvk-local/$platform"
mkdir -p "$out"
# Build source after applying the external series, then repack XCFrameworks.
# Keeping the dependency cache avoids destructive cleanup of other platforms.
(
    cd "$mvk"
    xcrun xcodebuild -project ExternalDependencies.xcodeproj \
        -scheme "ExternalDependencies-$os" -destination "generic/platform=$os" \
        -configuration Release -derivedDataPath "$mvk/External/build/Intermediates/$os" \
        -jobs "$jobs" CODE_SIGNING_ALLOWED=NO SKIP_PACKAGING=Y KEEP_CACHE=Y build
    PROJECT_DIR="$mvk" CONFIGURATION=Release SKIP_PACKAGING='' \
        bash -e "$mvk/Scripts/create_ext_lib_xcframeworks.sh"
    PROJECT_DIR="$mvk" CONFIGURATION=Release SKIP_PACKAGING='' KEEP_CACHE=Y \
        bash "$mvk/Scripts/package_ext_libs_finish.sh"
    xcrun xcodebuild -project MoltenVKPackaging.xcodeproj \
        -scheme "$package_scheme" -destination "generic/platform=$os" \
        -configuration Release -derivedDataPath "$out/MoltenVKDerivedData" \
        -jobs "$jobs" CODE_SIGNING_ALLOWED=NO build
) 2>&1 | tee "$out/build.log"
python3 - "$root" "$platform" "$out" <<'PY'
import hashlib, json, plistlib, subprocess, sys
from pathlib import Path
root, platform, output = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
mvk = root / 'upstreams/MoltenVK'
source = mvk / 'External/SPIRV-Cross'
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def revision(path): return subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'], text=True).strip()
package = mvk / 'Package/Release/MoltenVK/static/MoltenVK.xcframework'
with (package / 'Info.plist').open('rb') as stream:
    metadata = plistlib.load(stream)
slices = [entry for entry in metadata['AvailableLibraries']
          if entry.get('SupportedPlatform') == platform
          and not entry.get('SupportedPlatformVariant')
          and entry.get('LibraryPath') == 'libMoltenVK.a'
          and 'arm64' in entry.get('SupportedArchitectures', [])]
if len(slices) != 1:
    raise SystemExit('MoltenVK package must contain exactly one matching native static slice')
slice_info = slices[0]
if platform == 'ios' and slice_info['LibraryIdentifier'] != 'ios-arm64':
    raise SystemExit('Expected the iPhoneOS ios-arm64 slice, not a simulator package')
product = package / slice_info['LibraryIdentifier'] / slice_info['LibraryPath']
if not product.is_file(): raise SystemExit('MoltenVK package produced no matching static library')
receipt = {'schemaVersion':1, 'status':'built_not_installed', 'platform':platform,
 'moltenvkPin':revision(mvk), 'spirvCrossPin':revision(source),
 'spirvCrossPatchSha256':digest(root/'patches/spirv-cross/0001-precise-multiply-negative-zero.patch'),
 'spirvCrossEmitterSha256':digest(source/'spirv_msl.cpp'),
 'libraries':[{'path':str(product.relative_to(mvk)), 'bytes':product.stat().st_size,'sha256':digest(product),
               'libraryIdentifier':slice_info['LibraryIdentifier'],
               'supportedPlatform':slice_info['SupportedPlatform'],
               'supportedPlatformVariant':slice_info.get('SupportedPlatformVariant'),
               'architectures':slice_info['SupportedArchitectures']}],
 'boundary':'Local archive/package build only. Native app rebuilding, signing, installation and device/game validation remain separate.'}
(output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('MoltenVK rebuilt with patched pinned SPIRV-Cross; receipt: '+str(output/'receipt.json'))
PY
