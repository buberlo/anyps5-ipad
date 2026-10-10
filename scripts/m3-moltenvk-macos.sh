#!/usr/bin/env bash
# Build the pinned MoltenVK for macOS and run tools/vk-requirements
# against that ICD. Exit 1 from the tool (a hard requirement missed) is
# a result, not a script failure. Exit 2 or 3 means the loader did not
# produce a device and fails this script. Requires Xcode, Homebrew, and
# already initialized pinned MoltenVK external dependencies.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "MoltenVK macOS build needs Darwin (this is $(uname -s))" >&2
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required" >&2
    exit 1
fi
brew install cmake ninja python3 pkgconf vulkan-loader vulkan-headers

git -C "$root" submodule update --init --depth 1 upstreams/MoltenVK
mvk="$root/upstreams/MoltenVK"
# Build after the external patch is applied. fetchDependencies --macos would
# force-check out SPIRV-Cross and build an unpatched dependency first.
"$root/scripts/build-moltenvk-local.sh" macos

icd="$(find "$mvk/Package" -name 'MoltenVK_icd.json' | head -1 || true)"
dylib="$(find "$mvk/Package" -path '*macOS*' -name 'libMoltenVK.dylib' | head -1 || true)"
if [ -z "$dylib" ]; then
    echo "libMoltenVK.dylib was not produced" >&2
    find "$mvk/Package" -name 'libMoltenVK*' | head
    exit 1
fi
echo "MoltenVK dylib: $dylib"
if [ -z "$icd" ]; then
    icd="$(dirname "$dylib")/MoltenVK_icd.json"
    printf '%s\n' \
        '{' \
        '  "file_format_version": "1.0.0",' \
        '  "ICD": {' \
        '    "library_path": "./libMoltenVK.dylib",' \
        '    "api_version": "1.2.0"' \
        '  }' \
        '}' > "$icd"
fi
echo "ICD: $icd"
echo "ICD contents:"
cat "$icd"

export VK_DRIVER_FILES="$icd"
export VK_ICD_FILENAMES="$icd"
export VK_LOADER_LAYERS_DISABLE='~all~'
export VK_LOADER_DEBUG="${VK_LOADER_DEBUG:-error}"
export DYLD_LIBRARY_PATH="$(dirname "$dylib")${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

if [ -d /opt/homebrew/lib/pkgconfig ]; then
    export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi
make -C "$root/tools/vk-requirements" clean all
set +e
"$root/tools/vk-requirements/vk-requirements"
status=$?
set -e
echo "MOLTENVK_REQUIREMENTS_EXIT=$status"
if [ "$status" -ge 2 ]; then
    echo "vk-requirements could not enumerate a MoltenVK device" >&2
    exit "$status"
fi
echo "MoltenVK capability log finished (exit $status)"
