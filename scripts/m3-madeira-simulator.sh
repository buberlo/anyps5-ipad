#!/usr/bin/env bash
# Try Madeira's Xcode app for the iOS Simulator without signing.
# Madeira's own BUILDING.md lists inputs that are not in the clone
# (llvm-ios, vcruntime, a provisioning profile). This script does not
# invent those. A failure is the build log, not a signed device boot.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "xcodebuild needs macOS" >&2
    exit 1
fi
"$root/scripts/apply-patches.sh"
"$root/scripts/macos-select-xcode.sh"
git -C "$root" submodule update --init --depth 1 upstreams/Madeira upstreams/FEX

# The Xcode project searches $(SRCROOT)/../FEX, which is Madeira's own
# FEX submodule. This repo pins FEX at upstreams/FEX. An empty submodule
# directory is not a checkout.
fex_at="$root/upstreams/Madeira/FEX"
if [ ! -f "$fex_at/FEXCore/include/FEXCore/Config/Config.h" ]; then
    if [ -d "$fex_at/FEXCore" ]; then
        echo "Madeira/FEX is present and is not the pinned FEX checkout" >&2
        exit 1
    fi
    # Uninitialized nested submodule: empty directory, sometimes with a .git file.
    rm -rf "$fex_at"
    ln -s ../FEX "$fex_at"
fi

if ! command -v cmake >/dev/null 2>&1; then
    brew install cmake ninja
fi
# Generates FEXCore headers (ConfigValues.inl) and the static libraries
# the app project links. This does not build llvm-ios or fetch vcruntime.
"$root/upstreams/Madeira/build/fex-ios/build.sh"

# Device SDK matches fex-ios (iphoneos). Signing stays off. The project
# still references libdxmt_combined.a and x86_64-vcruntime, which are not
# in the clone.
if [ ! -f "$root/upstreams/Madeira/app/Madeira/libdxmt_combined.a" ]; then
    echo "missing libdxmt_combined.a (needs the llvm-ios toolchain in Madeira BUILDING.md)"
fi
if [ ! -d "$root/upstreams/Madeira/app/Madeira/x86_64-vcruntime" ]; then
    echo "missing x86_64-vcruntime (not in the clone)"
fi

xcodebuild \
    -project "$root/upstreams/Madeira/app/Madeira.xcodeproj" \
    -scheme Madeira \
    -destination 'generic/platform=iOS' \
    -configuration Debug \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    build
