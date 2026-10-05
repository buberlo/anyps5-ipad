#!/usr/bin/env bash
# Configure Madeira's Wine fork on macOS and link the host winevulkan.so
# with Vulkan enabled. The Apple side of sync.c stays the mach/QoS path.
# This does not boot a PE and does not talk to an iPad.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "this script is for macOS" >&2
    exit 1
fi
"$root/scripts/apply-patches.sh"

if command -v brew >/dev/null 2>&1; then
    brew install cmake ninja pkgconf bison flex gettext vulkan-loader vulkan-headers || true
    if [ -d /opt/homebrew/opt/bison/bin ]; then
        export PATH="/opt/homebrew/opt/bison/bin:$PATH"
    fi
fi

# Wine's --enable-archs=aarch64 configure errors unless it can compile a
# PE object. Host clang is not that compiler. llvm-mingw supplies
# aarch64-w64-mingw32-clang. The tarball is a toolchain, not firmware.
if ! command -v aarch64-w64-mingw32-clang >/dev/null 2>&1; then
    mingw_tag="20260421"
    mingw_root="$root/build/toolchains/llvm-mingw-${mingw_tag}-ucrt-macos-universal"
    if [ ! -x "$mingw_root/bin/aarch64-w64-mingw32-clang" ]; then
        mkdir -p "$root/build/toolchains"
        archive="$root/build/toolchains/llvm-mingw-${mingw_tag}-ucrt-macos-universal.tar.xz"
        url="https://github.com/mstorsjo/llvm-mingw/releases/download/${mingw_tag}/llvm-mingw-${mingw_tag}-ucrt-macos-universal.tar.xz"
        echo "downloading $url"
        curl -L --fail --retry 3 -o "$archive" "$url"
        tar -C "$root/build/toolchains" -xJf "$archive"
    fi
    export PATH="$mingw_root/bin:$PATH"
fi
echo "PE compiler: $(command -v aarch64-w64-mingw32-clang)"

git -C "$root" submodule update --init --depth 1 upstreams/wine upstreams/Madeira
src="$root/upstreams/wine"
build="$root/build/wine-macos"
mkdir -p "$root/upstreams/build" "$build"
ln -sfn ../Madeira/build/madeira_cfg.h "$root/upstreams/build/madeira_cfg.h"

arch="$(uname -m)"
case "$arch" in
    arm64) arch=aarch64 ;;
esac
if [ ! -f "$build/Makefile" ]; then
    (
        cd "$build"
        "$src/configure" \
            --enable-archs="$arch" \
            --disable-tests \
            --without-x \
            --without-alsa \
            --without-pulse \
            --without-gstreamer \
            --without-dbus \
            --without-sdl \
            --without-cups \
            --without-oss \
            --without-gphoto \
            --without-pcap \
            --without-freetype \
            --without-fontconfig \
            --prefix="$build/install"
    )
fi

make -C "$build" -j"$(sysctl -n hw.ncpu)" \
    dlls/ntdll/ntdll.so \
    dlls/win32u/win32u.so \
    dlls/winevulkan/winevulkan.so
test -f "$build/dlls/winevulkan/winevulkan.so"
file "$build/dlls/winevulkan/winevulkan.so"
echo "macOS winevulkan.so: $build/dlls/winevulkan/winevulkan.so"
