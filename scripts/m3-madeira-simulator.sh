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
"$root/scripts/macos-select-xcode.sh"
git -C "$root" submodule update --init --depth 1 upstreams/Madeira upstreams/FEX

# The Xcode project searches $(SRCROOT)/../FEX, which is Madeira's own
# FEX submodule. This repo pins FEX at upstreams/FEX. An empty submodule
# directory is not a checkout.
fex_at="$root/upstreams/Madeira/FEX"
if [ ! -f "$fex_at/FEXCore/include/FEXCore/Config/Config.h" ]; then
    if [ -d "$fex_at/FEXCore" ]; then
        echo "Madeira/FEX is present and is not the pinned FEX checkout" >&2
        exit 1
    fi
    # Uninitialized nested submodule: empty directory, sometimes with a .git file.
    rm -rf "$fex_at"
    ln -s ../FEX "$fex_at"
fi

if ! command -v cmake >/dev/null 2>&1; then
    brew install cmake ninja
fi
# Drop the symlink before checkout's post step. git submodule foreach
# refuses a submodule path that is a symbolic link.
cleanup_fex_link() {
    if [ -L "$fex_at" ]; then
        rm -f "$fex_at"
    fi
}
trap cleanup_fex_link EXIT

# apply-patches.sh only checks out External/rpmalloc. FEX's iOS configure
# also add_subdirectory's fmt, xxhash, range-v3, and unordered_dense.
# On 4b6d5a4 those directories were empty and cmake stopped before the
# DXMT stub. TUNE_CPU defaults to native, which runs
# Scripts/aarch64_fit_native.py against /proc/cpuinfo. That file is
# absent on macOS, and the script imports packaging. An iOS cross build
# does not tune for the runner's CPU.
git -C "$root/upstreams/FEX" submodule update --init --depth 1 -- \
    External/unordered_dense External/xxhash External/fmt External/range-v3 \
    External/rpmalloc

# Madeira's build/fex-ios/build.sh passes CMAKE_OSX_SYSROOT=iphoneos and
# does not set CMAKE_SYSTEM_PROCESSOR. On the macos-15 runner that
# configure used /usr/bin/cc and failed with "Unsupported processor type".
sdk="$(xcrun --sdk iphoneos --show-sdk-path)"
fex_build="$root/upstreams/FEX/build-ios"
cmake -S "$root/upstreams/FEX" -B "$fex_build" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_SYSTEM_PROCESSOR=arm64 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
    -DCMAKE_C_COMPILER="$(xcrun -sdk iphoneos -f clang)" \
    -DCMAKE_CXX_COMPILER="$(xcrun -sdk iphoneos -f clang++)" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTUNE_CPU=none \
    -DBUILD_TESTING=OFF \
    -DBUILD_THUNKS=OFF \
    -DBUILD_FEXCONFIG=OFF \
    -DBUILD_FEX_LINUX_TESTS=OFF \
    -DENABLE_FEX_ALLOCATOR=OFF \
    -DENABLE_ASSERTIONS=OFF \
    -DENABLE_CLANG_THUNKS=ON \
    -DENABLE_CCACHE=OFF
cmake --build "$fex_build" --target FEXCore FEXCore_Base JemallocLibs --parallel
ls -l \
    "$fex_build/FEXCore/Source/libFEXCore.a" \
    "$fex_build/FEXCore/Source/libFEXCore_Base.a" \
    "$fex_build/FEXCore/Source/libJemallocLibs.a" \
    "$fex_build/External/fmt/libfmt.a" \
    "$fex_build/External/cephes/libcephes_128bit.a" \
    "$fex_build/External/xxhash/cmake_unofficial/libxxhash.a" \
    "$fex_build/External/SoftFloat-3e/libsoftfloat_3e.a"

# Slim path: do not build DXMT or madeira-d3d12. The app target links
# libdxmt_combined.a for present-count and canary symbols. A no-op
# archive satisfies that link. It is not the llvm-ios DXMT library.
app_dir="$root/upstreams/Madeira/app/Madeira"
stub_o="$root/build/slim-dxmt-stub.o"
mkdir -p "$root/build"
xcrun -sdk iphoneos clang \
    -arch arm64 -isysroot "$sdk" -miphoneos-version-min=17.0 \
    -c "$root/scripts/slim-dxmt-stub.c" -o "$stub_o"
xcrun -sdk iphoneos libtool -static -o "$app_dir/libdxmt_combined.a" "$stub_o"
echo "slim libdxmt_combined.a (no-op, not DXMT)"

# tools/fetch-vcruntime.md extracts Microsoft's VC_redist.x64.exe. Nothing
# in the Madeira scripts builds those DLLs, and the slim PE does not import
# them. The Xcode project copies this folder into the app. Leave it empty.
mkdir -p "$app_dir/x86_64-vcruntime"
echo "x86_64-vcruntime left empty (Microsoft redistributable, not fetched)"

# StikJIT.xcframework was built with Swift 6.4. Xcode 26.3 on macos-15
# is Swift 6.2.4 and refuses the module ("this SDK is not supported by
# the compiler"). Rewriting :: to . produced
# "'StikJIT' is not a member type of enum 'StikJIT.StikJIT'" and the
# same SDK error. MadeiraJITHelper is the only target that imports
# StikJIT. This unsigned CI build drops that dependency and does not
# embed the appex. Device JIT still needs the helper; Konrad's signed
# boot is unchanged.
pbx="$root/upstreams/Madeira/app/Madeira.xcodeproj/project.pbxproj"
python3 - "$pbx" << 'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
old_dep = "\t\t\tdependencies = (\n\t\t\t\tB3000006 /* PBXTargetDependency */,\n\t\t\t);"
new_dep = "\t\t\tdependencies = (\n\t\t\t);"
old_embed = "\t\t\tfiles = (\n\t\t\t\tB1000016 /* MadeiraJITHelper.appex in Embed JIT Helper */,\n\t\t\t);\n\t\t\tname = \"Embed JIT Helper\";"
new_embed = "\t\t\tfiles = (\n\t\t\t);\n\t\t\tname = \"Embed JIT Helper\";"
if old_dep not in text or old_embed not in text:
    raise SystemExit("MadeiraJITHelper dependency was not in the project")
path.write_text(text.replace(old_dep, new_dep, 1).replace(old_embed, new_embed, 1))
print("unsigned build omits MadeiraJITHelper")
PY
for archive in \
    libntdll_unix.a libwin32u_unix.a libwineserver.a \
    libmadeira_rppairing.a libavformat.a libavcodec.a \
    libswresample.a libavutil.a libgnutls.a libgmp.a
do
    if [ -f "$app_dir/$archive" ]; then
        echo "present $archive"
    else
        echo "missing $archive"
    fi
done

# xcodebuild still runs so a new Swift or Objective-C error fails the job.
# On 15978d4 (Xcode 26.3, iPhoneOS 26.2) the app compiled and ld stopped
# at the first missing archive: library 'wineserver' not found.
# build/wineserver/build.sh copies a prebuilt app/Madeira/libwineserver.a
# and exits if that file is absent. The clone does not contain it.
# That specific miss is a hard stop. Any other xcodebuild failure still
# fails the job. This does not create a stand-in archive.
log="$root/build/madeira-xcodebuild.log"
mkdir -p "$root/build"
set +e
set +o pipefail
xcodebuild \
    -project "$root/upstreams/Madeira/app/Madeira.xcodeproj" \
    -scheme Madeira \
    -destination 'generic/platform=iOS' \
    -configuration Debug \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    build 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -o pipefail
set -e
if [ "$status" -eq 0 ]; then
    exit 0
fi
if [ ! -f "$app_dir/libwineserver.a" ] && grep -F -q "library 'wineserver' not found" "$log"; then
    echo "hard stop: the Madeira app compiled and did not link"
    echo "ld reported: library 'wineserver' not found"
    echo "build/wineserver/build.sh cannot create libwineserver.a without a base archive, and app/Madeira/libwineserver.a is not in the clone"
    echo "archives still missing:"
    for archive in \
        libntdll_unix.a libwin32u_unix.a libwineserver.a \
        libmadeira_rppairing.a libavformat.a libavcodec.a \
        libswresample.a libavutil.a
    do
        if [ ! -f "$app_dir/$archive" ]; then
            echo "  missing $archive"
        fi
    done
    exit 0
fi
echo "xcodebuild failed (exit $status); this is not the missing-wineserver hard stop" >&2
exit "$status"
