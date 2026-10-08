#!/usr/bin/env bash
# Build the pinned FEX monthly release with the local iOS port for aarch64 Linux.
# On aarch64 this is a native Clang build. Elsewhere it cross-compiles
# with scripts/cmake/aarch64-linux-gnu-clang.cmake and -DTUNE_CPU=cortex-a78.
# Clang is required; FEX rejects GCC. This is the slim FEX configuration
# (docs/SLIM.md): BUILD_THUNKS=OFF, BUILD_FEXCONFIG=OFF, no i386/WOW64
# target. Thunks are Linux host libraries; the iPad path is Wine + xtajit64,
# not FEX thunks. Jemalloc's glibc hook is disabled because that subproject
# does not cross-configure cleanly. rpmalloc stays on.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$root/scripts/apply-patches.sh"
native=0
if [ "$(uname -m)" = "aarch64" ]; then
    native=1
    if ! command -v clang++ >/dev/null 2>&1; then
        echo "need clang++" >&2
        exit 1
    fi
    echo "native aarch64 FEX ($(clang++ --version | head -1))"
elif ! command -v clang++ >/dev/null 2>&1 || ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    echo "need clang++ and g++-aarch64-linux-gnu" >&2
    exit 1
fi

git -C "$root/upstreams/FEX" submodule update --init --depth 1 -- \
    External/unordered_dense External/rpmalloc External/xxhash External/fmt \
    External/range-v3 External/drm-headers Source/Common/cpp-optparse

cmake_args=(
    -S "$root/upstreams/FEX" -B "$root/build/fex-aarch64" -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DENABLE_LTO=OFF
    -DENABLE_CCACHE=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_FEXCONFIG=OFF
    -DBUILD_THUNKS=OFF
    -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF
    -DENABLE_OFFLINE_TELEMETRY=OFF
    -DENABLE_STRICT_WERROR=OFF
    -DENABLE_WERROR=OFF
    -DBUILD_FEX_LINUX_TESTS=OFF
)
if [ "$native" = 1 ]; then
    # Host detection works on a real ARM64 machine. The x86 cross build
    # pins cortex-a78 because FEX's CPU probe crashes on x86 /proc/cpuinfo.
    cmake_args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
else
    cmake_args+=(
        -DCMAKE_TOOLCHAIN_FILE="$root/scripts/cmake/aarch64-linux-gnu-clang.cmake"
        -DTUNE_CPU=cortex-a78
    )
fi
cmake "${cmake_args[@]}"
# The executable target is FEX. install() also copies it to FEXInterpreter.
# FEX refuses to start unless it can exec FEXServer from the same directory.
cmake --build "$root/build/fex-aarch64" --target FEX --target FEXServer --parallel "${JOBS:-$(nproc)}"
echo "FEX: $root/build/fex-aarch64/Bin/FEX"
echo "FEXServer: $root/build/fex-aarch64/Bin/FEXServer"
