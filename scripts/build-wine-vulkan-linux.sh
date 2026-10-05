#!/usr/bin/env bash
# Configure Madeira's Wine fork (madeira-lgpl, Wine 11) for an x86-64 Linux
# host with Vulkan left enabled, then build ntdll.so, win32u.so, and
# winevulkan.so. patches/wine/0001 supplies the Linux side of the Apple
# sync calls. This does not compile vulkan_ios.c or vulkan_metal_ios.c;
# those stay iOS drafts. The .so is not executed here.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$root/scripts/apply-patches.sh"
src="$root/upstreams/wine"
build="$root/build/wine-linux"
# APS5_SLIM=1 is the same Vulkan targets in a separate tree, with the host
# drivers this chain does not call turned off at configure time. Vulkan
# itself stays on. The default tree is unchanged.
slim_args=()
if [ "${APS5_SLIM:-}" = 1 ]; then
    build="$root/build/wine-linux-slim"
    slim_args=(
        --without-opengl
        --without-wayland
        --without-fontconfig
        --without-freetype
        --without-gnutls
        --without-udev
        --without-usb
        --without-v4l2
        --without-sane
        --without-opencl
        --without-ffmpeg
        --without-pcsclite
        --without-krb5
        --without-gssapi
        --without-netapi
        --without-capi
        --disable-win16
    )
    echo "APS5_SLIM=1 configure extras: ${slim_args[*]}"
fi
mkdir -p "$build"

# ntdll's unix sources include ../../../../build/madeira_cfg.h, which is
# Madeira/build/madeira_cfg.h when wine is the nested submodule. With wine
# checked out as a sibling, that relative path lands in upstreams/build/.
mkdir -p "$root/upstreams/build"
ln -sfn ../Madeira/build/madeira_cfg.h "$root/upstreams/build/madeira_cfg.h"

if [ ! -f "$build/Makefile" ]; then
    (
        cd "$build"
        "$src/configure" \
            --enable-archs=x86_64 \
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
            "${slim_args[@]}" \
            --prefix="$build/install"
    )
fi

# patches/wine/0001 guards the Apple QoS and mach time calls in ntdll
# unix/sync.c, and makes ios_srcwatch_arm a weak miss in win32u. With
# that applied, the unix libraries link on Linux. The .so dlopens
# libvulkan.so.1 through win32u; it does not create a device.
make -C "$build" -j"${JOBS:-$(nproc)}" \
    dlls/ntdll/ntdll.so \
    dlls/win32u/win32u.so \
    dlls/winevulkan/winevulkan.so \
    dlls/winevulkan/vulkan.o \
    dlls/winevulkan/vulkan_thunks.o \
    dlls/winevulkan/x86_64-windows/winevulkan.dll \
    dlls/vulkan-1/x86_64-windows/vulkan-1.dll \
    dlls/win32u/vulkan.o
test -f "$build/dlls/winevulkan/winevulkan.so"
echo "winevulkan build tree: $build"
find "$build/dlls" -path '*winevulkan*' -o -path '*vulkan-1*' -o -name 'ntdll.so' -o -name 'win32u.so' | head -40
