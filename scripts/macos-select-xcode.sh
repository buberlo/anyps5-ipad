#!/usr/bin/env bash
# Point xcode-select at the newest Xcode 26 or later on a GitHub macOS
# image. The default on macos-15 is Xcode 16.4, whose iOS SDK does not
# have BGContinuedProcessingTask. macos-14's Xcode 15.4 does not either.
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
    echo "xcode-select is a macOS tool" >&2
    exit 1
fi

selected="$(python3 - << 'PY'
import glob, os, re
cands = glob.glob("/Applications/Xcode_*.app") + glob.glob("/Applications/Xcode.app")
def key(path):
    match = re.search(r"Xcode_(\d+(?:\.\d+)*)", os.path.basename(path))
    if not match:
        return (0,)
    return tuple(int(part) for part in match.group(1).split("."))
usable = [path for path in cands if key(path) >= (26,)]
if not usable:
    raise SystemExit(0)
print(max(usable, key=key))
PY
)"

if [ -n "$selected" ]; then
    sudo xcode-select -s "$selected"
    echo "xcode-select: $selected"
else
    echo "no Xcode 26+ under /Applications; keeping the default" >&2
fi
xcodebuild -version
xcrun --sdk iphoneos --show-sdk-version || true
xcrun --sdk iphonesimulator --show-sdk-version || true
