#!/usr/bin/env bash
# Build the iOS static archives the Madeira app links, from the pinned
# Wine fork and Madeira's own build scripts.
#
# Madeira v0.1.3 publishes Madeira-0.1.3.ipa and a sha256 file. It does
# not publish these archives, and the pin has no GitHub Actions, no
# git-lfs filter, and no deps submodule. BUILDING.md says
# build/ntdll-unix, build/win32u-unix, build/ffmpeg, and build/gnutls-ios
# produce them. build/wineserver/build.sh only swaps objects into a
# prebuilt libwineserver.a that is not in the clone, so this script
# compiles every server/*.c (with the iOS replacements) instead.
#
# libmadeira_rppairing.a is on-device remote pairing for Built-in StikJIT
# (docs/JIT.md), not remote play. The slim app gets scripts/slim-rppairing.c.
#
# Usage: scripts/m3-madeira-unix-libs.sh iphoneos|iphonesimulator
# The Madeira/FEX and Madeira/wine symlinks are the caller's.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "the iOS SDK is on macOS" >&2
    exit 1
fi

sdkname="${1:-iphoneos}"
case "$sdkname" in
    iphoneos) minflag="-miphoneos-version-min=17.0" ;;
    iphonesimulator) minflag="-mios-simulator-version-min=17.0" ;;
    *)
        echo "usage: $0 iphoneos|iphonesimulator" >&2
        exit 2
        ;;
esac

"$root/scripts/macos-select-xcode.sh"
madeira="$root/upstreams/Madeira"
wine="$root/upstreams/wine"
app_dir="$madeira/app/Madeira"
sdk="$(xcrun --sdk "$sdkname" --show-sdk-path)"
echo "unix libs for $sdkname ($sdk)"

if [ ! -f "$madeira/wine/dlls/ntdll/unix/loader.c" ]; then
    echo "Madeira/wine is not the pinned Wine checkout" >&2
    exit 1
fi

# Madeira's scripts read wine/build-macos/include/config.h. The macOS
# Wine build that already links winevulkan.so writes that header.
if [ ! -f "$root/build/wine-macos/include/config.h" ]; then
    "$root/scripts/m3-winevulkan-macos.sh"
fi
ln -sfn "$root/build/wine-macos" "$wine/build-macos"
mkdir -p "$wine/build-arm64ec"
ln -sfn "$root/build/wine-macos/include" "$wine/build-arm64ec/include"

# dwrite and winegstreamer include widl headers that a unix ntdll.so
# build does not generate. On 3a947d5 dwrite.h existed and the compile
# stopped at dcommon.h; winegstreamer stopped at strmif.h.
widl_headers=(
    include/unknwn.h
    include/wtypes.h
    include/objidl.h
    include/oaidl.h
    include/dxgiformat.h
    include/dcommon.h
    include/d2d1.h
    include/dwrite.h
    include/dwrite_1.h
    include/dwrite_2.h
    include/dwrite_3.h
    include/devenum.h
    include/axcore.h
    include/axextend.h
    include/dyngraph.h
    include/vmrender.h
    include/dvdif.h
    include/strmif.h
    include/amvideo.h
    include/control.h
    include/mfobjects.h
)
missing_widl=0
for header in "${widl_headers[@]}"; do
    if [ ! -f "$root/build/wine-macos/$header" ]; then
        missing_widl=1
        break
    fi
done
if [ "$missing_widl" -eq 1 ]; then
    echo "=== generating widl headers ==="
    set +e
    make -C "$root/build/wine-macos" -k -j"$(sysctl -n hw.ncpu)" "${widl_headers[@]}"
    set -e
    for header in "${widl_headers[@]}"; do
        if [ ! -f "$root/build/wine-macos/$header" ]; then
            echo "missing $header after widl" >&2
            exit 1
        fi
    done
fi

ft_src="$madeira/research/freetype"
if [ ! -f "$ft_src/include/ft2build.h" ]; then
    rm -rf "$ft_src"
    git clone --depth 1 --branch VER-2-13-3 \
        https://github.com/freetype/freetype.git "$ft_src"
fi

# Simulator builds reuse the same scripts, which hardcode the iphoneos
# SDK. Rewrite those copies for this process and put the originals back
# on the way out. iphoneos leaves the scripts untouched.
script_files=(
    build/ntdll-unix/build.sh
    build/win32u-unix/build.sh
    build/ffmpeg/build.sh
    build/gnutls-ios/build.sh
    build/freetype-ios/build.sh
)
restore_madeira_scripts() {
    if [ "$sdkname" = "iphonesimulator" ]; then
        git -C "$madeira" checkout -- "${script_files[@]}"
    fi
}
trap restore_madeira_scripts EXIT
if [ "$sdkname" = "iphonesimulator" ]; then
    python3 - "$madeira" "${script_files[@]}" << 'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
for rel in sys.argv[2:]:
    path = root / rel
    text = path.read_text()
    text = text.replace("-miphoneos-version-min", "-mios-simulator-version-min")
    text = text.replace("iphoneos", "iphonesimulator")
    path.write_text(text)
    print("simulator sdk:", rel)
PY
fi

echo "=== gnutls / nettle / gmp ($sdkname) ==="
rm -rf "$madeira/build/gnutls-ios/obj"
if ! bash "$madeira/build/gnutls-ios/build.sh"; then
    echo "gnutls-ios failed; logs:" >&2
    find "$madeira/build/gnutls-ios/obj" -name '*.log' -print -exec tail -n 80 {} \; >&2 || true
    exit 1
fi
gnutls_prefix="$madeira/toolchains/gnutls-ios"
for lib in libgmp.a libnettle.a libhogweed.a libgnutls.a; do
    cp "$gnutls_prefix/lib/$lib" "$app_dir/$lib"
    echo "installed $lib ($sdkname) $(wc -c < "$app_dir/$lib" | tr -d ' ') bytes"
done

echo "=== ffmpeg ($sdkname) ==="
rm -rf "$madeira/build/ffmpeg/obj" "$madeira/toolchains/ffmpeg-ios"
if ! bash "$madeira/build/ffmpeg/build.sh"; then
    echo "ffmpeg failed; logs:" >&2
    find "$madeira/build/ffmpeg/obj" -name '*.log' -print -exec tail -n 80 {} \; >&2 || true
    exit 1
fi

echo "=== freetype ($sdkname) ==="
rm -rf "$madeira/build/freetype-ios/build"
bash "$madeira/build/freetype-ios/build.sh"

echo "=== ntdll unix ($sdkname) ==="
rm -rf "$madeira/build/ntdll-unix/obj"
if ! bash "$madeira/build/ntdll-unix/build.sh"; then
    echo "ntdll-unix failed; compiler errors:" >&2
    find "$madeira/build/ntdll-unix/obj" -name '*.err' -print | while read -r err; do
        if grep -q 'error:' "$err"; then
            echo "$err" >&2
            cat "$err" >&2
        fi
    done
    exit 1
fi
test -f "$app_dir/libntdll_unix.a"

echo "=== win32u unix ($sdkname) ==="
rm -rf "$madeira/build/win32u-unix/obj"
if ! bash "$madeira/build/win32u-unix/build.sh"; then
    echo "win32u-unix failed; compiler errors:" >&2
    find "$madeira/build/win32u-unix/obj" -name '*.err' -size +0c -print -exec tail -n 40 {} \; >&2 || true
    exit 1
fi
test -f "$app_dir/libwin32u_unix.a"

echo "=== wineserver from server/*.c ($sdkname) ==="
objcopy=""
if objcopy="$(xcrun --find llvm-objcopy 2>/dev/null)" && [ -x "$objcopy" ]; then
    :
else
    objcopy=""
fi
if [ -z "$objcopy" ]; then
    brew install llvm
    objcopy="$(brew --prefix llvm)/bin/llvm-objcopy"
fi
test -x "$objcopy"
echo "llvm-objcopy: $objcopy"

ws_build="$madeira/build/wineserver"
ws_obj="$ws_build/obj-$sdkname"
rm -rf "$ws_obj"
mkdir -p "$ws_obj"
shims="$madeira/build/ntdll-unix/shims"
ws_flags=(
    -arch arm64 -isysroot "$sdk" "$minflag" -O2
    -I"$wine/include" -I"$wine/include/wine"
    -I"$wine/build-macos/include"
    -I"$ws_build" -I"$wine/server"
    -I"$shims"
    -I"$madeira/build/madsync" -DHAVE_LINUX_NTSYNC_H=1
    -include "$ws_build/config_ios.h"
    -include stdarg.h
    -include "$ws_build/unicode_fix.h"
    -include "$ws_build/wineserver_ios_kill.h"
    -DBINDIR=\"/usr/local/bin\" -DDATADIR=\"/usr/local/share\"
    -D__WINESRC__ -DWINE_IOS=1
    -Dmain=wineserver_main
    -Wno-implicit-function-declaration
    -Wno-int-conversion
)
compile_ws() {
    local src="$1"
    local name="$2"
    shift 2
    echo -n "  $name... "
    if xcrun -sdk "$sdkname" clang "${ws_flags[@]}" "$@" -c "$src" -o "$ws_obj/$name.o" 2>"$ws_obj/err-$name.txt"; then
        echo "OK"
    else
        echo "FAILED"
        cat "$ws_obj/err-$name.txt" >&2
        exit 1
    fi
}

for src in "$wine/server/"*.c; do
    name="$(basename "$src" .c)"
    case "$name" in
        request|main|mach|unicode|fd|window|mapping|queue) continue ;;
    esac
    compile_ws "$src" "$name"
done
compile_ws "$ws_build/request_ios.c" request
compile_ws "$ws_build/main_ios.c" main
compile_ws "$ws_build/mach_ios.c" mach
compile_ws "$ws_build/unicode_ios.c" unicode
compile_ws "$ws_build/fd_ios.c" fd
compile_ws "$ws_build/window_ios.c" window
compile_ws "$ws_build/mapping_ios.c" mapping
compile_ws "$ws_build/queue_ios.c" queue
compile_ws "$ws_build/wine_log_ios.c" wine_log_ios
compile_ws "$ws_build/hidpad_ios.c" hidpad_ios
compile_ws "$madeira/build/hidpad/hidparse_ios.c" hidparse_ios
# The kill wrapper must not see its own header, or the macro recurses.
echo -n "  wineserver_ios_kill... "
if xcrun -sdk "$sdkname" clang \
    -arch arm64 -isysroot "$sdk" "$minflag" -O2 \
    -I"$ws_build" -DWINE_IOS=1 -Wno-implicit-function-declaration \
    -c "$ws_build/wineserver_ios_kill.c" -o "$ws_obj/wineserver_ios_kill.o" \
    2>"$ws_obj/err-kill.txt"; then
    echo "OK"
else
    echo "FAILED"
    cat "$ws_obj/err-kill.txt" >&2
    exit 1
fi

ar rcs "$ws_obj/libwineserver.a" "$ws_obj"/*.o
# Same renames as build/wineserver/build.sh. Skip an object that does
# not reference the symbol; llvm-objcopy errors when the old name is
# absent.
collisions=(
    alloc_user_handle free_user_handle get_virtual_screen_rect
    destroy_thread_windows get_window_thread is_desktop_class
    is_message_class is_window_visible mirror_region send_notify_message
    shared_session user_shared_data
)
rename_dir="$ws_obj/rename"
rm -rf "$rename_dir"
mkdir -p "$rename_dir"
(cd "$rename_dir" && ar x "$ws_obj/libwineserver.a")
for obj in "$rename_dir"/*.o; do
    args=()
    defined="$(nm "$obj" 2>/dev/null || true)"
    for sym in "${collisions[@]}"; do
        if printf '%s\n' "$defined" | grep -q " _${sym}\$"; then
            args+=(--redefine-sym "_${sym}=_ws_${sym}")
        fi
    done
    if [ "${#args[@]}" -gt 0 ]; then
        "$objcopy" "${args[@]}" "$obj"
    fi
done
rm -f "$ws_obj/libwineserver.a"
ar rcs "$ws_obj/libwineserver.a" "$rename_dir"/*.o
cp "$ws_obj/libwineserver.a" "$app_dir/libwineserver.a"
echo "libwineserver.a ($sdkname): $(wc -c < "$app_dir/libwineserver.a" | tr -d ' ') bytes"
nm -gU "$app_dir/libwineserver.a" | grep -m 1 wineserver_main || {
    echo "libwineserver.a does not export wineserver_main" >&2
    exit 1
}

echo "=== slim rppairing ($sdkname) ==="
rpp_o="$root/build/slim-rppairing-$sdkname.o"
mkdir -p "$root/build"
xcrun -sdk "$sdkname" clang \
    -arch arm64 -isysroot "$sdk" "$minflag" \
    -c "$root/scripts/slim-rppairing.c" -o "$rpp_o"
xcrun -sdk "$sdkname" libtool -static -o "$app_dir/libmadeira_rppairing.a" "$rpp_o"
echo "slim libmadeira_rppairing.a (pairing stub, not idevice)"

echo "archives for $sdkname:"
ls -l \
    "$app_dir/libntdll_unix.a" \
    "$app_dir/libwin32u_unix.a" \
    "$app_dir/libwineserver.a" \
    "$app_dir/libmadeira_rppairing.a" \
    "$app_dir/libavformat.a" \
    "$app_dir/libavcodec.a" \
    "$app_dir/libswresample.a" \
    "$app_dir/libavutil.a" \
    "$app_dir/libgnutls.a" \
    "$app_dir/libgmp.a"
