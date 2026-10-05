#!/usr/bin/env bash
# Compile and run the FEXBridge AVX decision against pinned FEX headers.
# The host binary checks the getenv logic. The aarch64 binary, when the
# cross compiler is installed, is only a compile check (run under qemu if
# present). FEXBridge.mm is not compiled.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/tools/checks/fexbridge_avx_decision.cpp"
inc=(
    -I "$root/upstreams/FEX/FEXCore/include"
    -I "$root/upstreams/FEX/External/fmt/include"
)
out="$root/build/fexbridge-avx-decision"
mkdir -p "$root/build"
if [ ! -f "$root/upstreams/FEX/External/fmt/include/fmt/format.h" ]; then
    git -C "$root/upstreams/FEX" submodule update --init --depth 1 External/fmt
fi

# Upstream headers warn (unused parameters). The decision TU itself is clean.
g++ -std=c++20 -Wall -Wextra -Wno-unused-parameter "${inc[@]}" "$src" -o "$out"
set +e
"$out"; host_default=$?
MADEIRA_FEX_AVX=0 "$out"; host_off=$?
MADEIRA_FEX_AVX=1 "$out"; host_on=$?
set -e
echo "host default=$host_default off=$host_off on=$host_on"
if [ "$host_default" -ne 0 ] || [ "$host_off" -ne 1 ] || [ "$host_on" -ne 0 ]; then
    echo "FEXBridge AVX decision failed on the host" >&2
    exit 1
fi

if command -v clang++ >/dev/null 2>&1 && command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    clang++ --target=aarch64-linux-gnu -std=c++20 -Wall -Wextra -Wno-unused-parameter "${inc[@]}" "$src" -o "$out-aarch64"
    echo "aarch64 TU compiled: $out-aarch64"
    if command -v qemu-aarch64-static >/dev/null 2>&1; then
        set +e
        qemu-aarch64-static -L /usr/aarch64-linux-gnu "$out-aarch64"; q=$?
        set -e
        echo "qemu aarch64 default=$q"
        [ "$q" -eq 0 ]
    fi
fi
echo "fexbridge avx decision checked"
