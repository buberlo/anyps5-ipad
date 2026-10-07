#!/usr/bin/env bash
# Build the real AnyPS5 iPhoneOS runtime. This does not install or launch it.
# Keep the qualified Debug/JIT configuration; optimize only the native app host.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ "$(uname -s)" = Darwin ] || { echo "iPhoneOS builds require macOS" >&2; exit 1; }
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
export JOBS="${JOBS:-2}"
case "${APS5_BUILD_PROFILE:-performance}" in
    performance|diagnostic) ;;
    *) echo "APS5_BUILD_PROFILE must be performance or diagnostic" >&2; exit 2 ;;
esac
export CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-$JOBS}"
export APS5_VULKAN_ONLY=1
export MADEIRA_VK_STATIC_LINK=1
export MADEIRA_WITH_VULKAN=1
export WINE_SRC="$root/upstreams/wine"
export WINE_BUILD="$root/build/wine-macos"
for command in cmake ninja llvm-objcopy llvm-readobj cargo; do
    command -v "$command" >/dev/null || { echo "Missing build tool: $command" >&2; exit 1; }
done
if [ "${APS5_PATCHES_APPLIED:-0}" != 1 ]; then "$root/scripts/apply-patches.sh"; fi
"$root/scripts/link-madeira-siblings.sh"
# GitHub checkout cleanup traverses nested gitlinks and rejects our local links.
# Remove only links made by the build, after all child builds have completed.
cleanup_links() {
    local name link
    for name in FEX wine; do
        link="$root/upstreams/Madeira/$name"
        if [ -L "$link" ] && [ "$(readlink "$link")" = "../$name" ]; then rm "$link"; fi
    done
}
trap cleanup_links EXIT
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
cmake --build "$fex_build" --target FEXCore FEXCore_Base JemallocLibs --parallel "${JOBS:-2}"
ls -l \
    "$fex_build/FEXCore/Source/libFEXCore.a" \
    "$fex_build/FEXCore/Source/libFEXCore_Base.a" \
    "$fex_build/FEXCore/Source/libJemallocLibs.a" \
    "$fex_build/External/fmt/libfmt.a" \
    "$fex_build/External/cephes/libcephes_128bit.a" \
    "$fex_build/External/xxhash/cmake_unofficial/libxxhash.a" \
    "$fex_build/External/SoftFloat-3e/libsoftfloat_3e.a"


"$root/scripts/m3-madeira-unix-libs.sh" iphoneos
"$root/scripts/m3-madeira-pe.sh"
# Build the named winevulkan dispatch table and its real Vulkan present counter.
bash "$root/upstreams/Madeira/build/winevulkan-unix/build.sh"
app_dir="$root/upstreams/Madeira/app/Madeira"
moltenvk="${APS5_MOLTENVK_LIBRARY:-$root/upstreams/MoltenVK/Package/Release/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a}"
[ -f "$moltenvk" ] || { echo "Missing iPhoneOS MoltenVK archive: $moltenvk" >&2; exit 1; }
cp "$moltenvk" "$app_dir/libMoltenVK.a"
mkdir -p "$root/build/ios-runtime"
xcrun -sdk iphoneos clang -arch arm64 -isysroot "$sdk" -miphoneos-version-min=17.0 \
    -c "$root/scripts/aps5-vulkan-ui.c" -o "$root/build/ios-runtime/aps5-vulkan-ui.o"
xcrun -sdk iphoneos clang -arch arm64 -isysroot "$sdk" -miphoneos-version-min=17.0 \
    -c "$root/scripts/aps5-runtime-diagnostics.c" -o "$root/build/ios-runtime/aps5-runtime-diagnostics.o"
xcrun -sdk iphoneos libtool -static -o "$app_dir/libaps5_ui.a" \
    "$root/build/ios-runtime/aps5-vulkan-ui.o" "$root/build/ios-runtime/aps5-runtime-diagnostics.o"
bash "$root/upstreams/Madeira/build/stage-licenses.sh"
# No helper-removal, pairing stubs or required-library placeholders are used.
args=(
    -project "$root/upstreams/Madeira/app/Madeira.xcodeproj"
    -scheme Madeira -destination 'generic/platform=iOS' -configuration Debug
    -derivedDataPath "$root/build/ios-runtime/DerivedData"
    MADEIRA_BUNDLE_IDENTIFIER=com.buberlo.anyps5ipad
    'OTHER_SWIFT_FLAGS=$(inherited) -j'"$JOBS -driver-batch-count $JOBS"
)
if [ "${APS5_BUILD_PROFILE:-performance}" = performance ]; then
    args+=(GCC_OPTIMIZATION_LEVEL=2 SWIFT_OPTIMIZATION_LEVEL=-O DEBUG_INFORMATION_FORMAT=dwarf-with-dsym)
fi
if [ "${APS5_CODE_SIGNING:-NO}" = YES ]; then
    [ -n "${APS5_DEVELOPMENT_TEAM:-}" ] || { echo "Set APS5_DEVELOPMENT_TEAM for signing" >&2; exit 1; }
    args+=("DEVELOPMENT_TEAM=$APS5_DEVELOPMENT_TEAM" CODE_SIGNING_ALLOWED=YES)
    if [ "${APS5_ALLOW_PROVISIONING_UPDATES:-0}" = 1 ]; then
        args+=(-allowProvisioningUpdates)
    fi
else
    args+=(CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO)
fi
xcodebuild -jobs "$JOBS" "${args[@]}" build
app="$root/build/ios-runtime/DerivedData/Build/Products/Debug-iphoneos/Madeira.app"
test -x "$app/Madeira"
test -d "$app/PlugIns/MadeiraJITHelper.appex"
python3 "$root/scripts/check-ios-pe.py" verify --app "$app"
/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Info.plist"
echo "LINKED_APP=$app"
echo "CODE_SIGNING=${APS5_CODE_SIGNING:-NO}; install, JIT and gameplay remain separate device checks"
