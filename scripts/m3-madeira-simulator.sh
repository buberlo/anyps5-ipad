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
git -C "$root" submodule update --init --depth 1 upstreams/Madeira

xcodebuild \
    -project "$root/upstreams/Madeira/app/Madeira.xcodeproj" \
    -scheme Madeira \
    -destination 'generic/platform=iOS Simulator' \
    -configuration Debug \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    build
