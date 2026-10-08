#!/usr/bin/env bash
# Compile actual FEX Darwin utilities with sanitizers; no device or Wine needed.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
fex="${1:-$root/upstreams/FEX}"
[ "$(uname -s)" = Darwin ] || { echo 'Requires Darwin' >&2; exit 1; }
scratch="$(mktemp -d "${TMPDIR:-/tmp/}fex-darwin-test.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT
DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}" \
  xcrun clang++ -std=c++20 -O1 -g -fsanitize=address,undefined \
  -I"$fex/FEXCore/include" -I"$fex/FEXHeaderUtils" -I"$fex/External/fmt/include" \
  "$root/tools/fex-darwin-utils-test.cpp" "$fex/FEXCore/Source/Utils/FileUtils.cpp" \
  -o "$scratch/test"
"$scratch/test"
