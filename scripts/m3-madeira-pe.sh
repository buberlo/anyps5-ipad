#!/usr/bin/env bash
# Rebuild the ARM64EC components that cross the FEX/Wine/Vulkan ABI boundary.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
madeira="$root/upstreams/Madeira"
fex="$root/upstreams/FEX"
wine="$root/upstreams/wine"
compiler="$(command -v arm64ec-w64-mingw32-clang || true)"
[ -n "$compiler" ] || { echo "Pinned llvm-mingw ARM64EC compiler is required" >&2; exit 1; }
tc="$(dirname "$compiler")"
jobs="${JOBS:-2}"
git -C "$fex" submodule update --init --depth 1 Source/Common/cpp-optparse
# llvm-mingw 20260421's ARM64EC ThinLTO link cannot resolve EC libc++ symbols
# (also reproducible with a tiny std::mutex DLL). Preserve native object code.
cmake -S "$fex" -B "$fex/build-arm64ec" \
    -DCMAKE_BUILD_TYPE=Release -DMINGW_TRIPLE=arm64ec-w64-mingw32 \
    -DCMAKE_TOOLCHAIN_FILE="$fex/Data/CMake/toolchain_mingw.cmake" \
    -DCMAKE_C_FLAGS=-DFEX_IOS_HOST -DCMAKE_CXX_FLAGS=-DFEX_IOS_HOST \
    -DCMAKE_ASM_FLAGS=-DFEX_IOS_HOST \
    -DTUNE_CPU=none -DENABLE_FEX_ALLOCATOR=ON -DENABLE_JEMALLOC_GLIBC_ALLOC=ON \
    -DFEX_IOS_HOST_BUILD=ON -DENABLE_OFFLINE_RUNTIME=ON -DBUILD_FEXCONFIG=OFF -DENABLE_CLANG_THUNKS=ON \
    -DENABLE_LTO=OFF -DENABLE_CCACHE=OFF -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DENABLE_ASSERTIONS=OFF
cmake --build "$fex/build-arm64ec" --target arm64ecfex --parallel "$jobs"
cp "$fex/build-arm64ec/Bin/libarm64ecfex.dll" "$madeira/app/Madeira/arm64ec-windows/xtajit64.dll"

build="$wine/build-arm64ec"
mkdir -p "$build"
# Never alias this tree's include/ to the host configuration: configure writes
# architecture-specific headers there and would corrupt the host build.
[ ! -L "$build/include" ] || { echo "ARM64EC include/ must not be a symlink" >&2; exit 1; }
if [ ! -f "$build/config.status" ]; then
    (cd "$build" && "$wine/configure" --enable-archs=arm64ec --without-x \
        --disable-tests --without-gstreamer --without-freetype --without-fontconfig \
        --without-alsa --without-pulse --without-dbus --without-cups --without-oss \
        --without-sdl --without-gphoto --without-pcap)
fi
make -C "$build" -j"$jobs" \
    dlls/ntdll/arm64ec-windows/ntdll.dll \
    dlls/winevulkan/arm64ec-windows/winevulkan.dll \
    dlls/vulkan-1/arm64ec-windows/vulkan-1.dll \
    dlls/win32u/arm64ec-windows/win32u.dll \
    dlls/mscoree/arm64ec-windows/mscoree.dll \
    dlls/xinput1_1/arm64ec-windows/xinput1_1.dll \
    dlls/xinput1_2/arm64ec-windows/xinput1_2.dll \
    dlls/xinput1_3/arm64ec-windows/xinput1_3.dll \
    dlls/xinput1_4/arm64ec-windows/xinput1_4.dll \
    dlls/xinput9_1_0/arm64ec-windows/xinput9_1_0.dll
for module in ntdll winevulkan vulkan-1 win32u mscoree xinput1_1 xinput1_2 xinput1_3 xinput1_4 xinput9_1_0; do
    source="$build/dlls/$module/arm64ec-windows/$module.dll"
    target="$madeira/app/Madeira/arm64ec-windows/$module.dll"
    cp "$source" "$target.tmp"
    "$tc/arm64ec-w64-mingw32-strip" --strip-debug "$target.tmp"
    if [ "$module" = ntdll ]; then
        # The native loader maps a file image with this exact upstream slack.
        python3 - "$target.tmp" <<'PY'
import struct, sys
from pathlib import Path
path = Path(sys.argv[1])
data = path.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
target = struct.unpack_from('<I', data, pe + 24 + 56)[0] + 0x50000
if len(data) > target:
    raise ValueError('Stripped ntdll exceeds the loader padding contract')
path.write_bytes(data + b'\0' * (target - len(data)))
PY
    fi
    mv "$target.tmp" "$target"
done
for module in ntdll xtajit64 winevulkan vulkan-1; do
    file "$madeira/app/Madeira/arm64ec-windows/$module.dll"
done
python3 "$root/scripts/check-ios-pe.py" record
