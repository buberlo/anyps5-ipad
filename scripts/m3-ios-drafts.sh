#!/usr/bin/env bash
# Compile the iOS Vulkan drafts with the iPhoneOS SDK. vulkan_metal_ios.c
# and moltenvk_static_loader.c are translation units. vulkan_ios.c includes
# Wine's vulkan.c, so this also configures the Wine tree far enough to
# generate that TU's headers when they are missing. A failure here is a
# compile failure, not a device boot.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "the iOS SDK is on macOS" >&2
    exit 1
fi
"$root/scripts/apply-patches.sh"
"$root/scripts/m3-winevulkan-macos.sh"

sdk="$(xcrun --sdk iphoneos --show-sdk-path)"
wine="$root/upstreams/wine"
madeira="$root/upstreams/Madeira"
build="$root/build/wine-macos"
drafts="$madeira/build/win32u-unix"
out="$root/build/ios-drafts"
mkdir -p "$out"

compile() {
    local src="$1"
    local name="$2"
    shift 2
    echo "== $name =="
    xcrun -sdk iphoneos clang \
        -arch arm64 -isysroot "$sdk" -miphoneos-version-min=17.0 \
        -fPIC -fvisibility=hidden -Wno-implicit-function-declaration \
        -Wno-int-conversion -Wno-unused-parameter \
        -include "$drafts/config_ios.h" \
        -I"$drafts" \
        -I"$build/include" \
        -I"$wine/include" \
        -I"$wine/dlls/win32u" \
        -I"$build/dlls/win32u" \
        -D__WINESRC__ -D_WIN32U_ -DWINE_UNIX_LIB -DWINE_IOS=1 \
        -DMADEIRA_WITH_VULKAN=1 \
        "$@" \
        -c "$src" -o "$out/$name.o"
    echo "OK $out/$name.o"
}

compile "$drafts/moltenvk_static_loader.c" moltenvk_static_loader
compile "$drafts/vulkan_metal_ios.c" vulkan_metal_ios
compile "$drafts/vulkan_ios.c" vulkan_ios
echo "iOS drafts compiled with $(xcrun -sdk iphoneos clang --version | head -1)"
