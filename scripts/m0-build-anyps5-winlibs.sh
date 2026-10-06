#!/usr/bin/env bash
# Build the demo and Dreaming Sarah's AnyPS5 PRX closure with WinLibs GCC 15.2.0
# posix-seh (15.2.0posix-14.0.0-ucrt-r7), the compiler AnyPS5's BUILD.md
# requires. Ubuntu's GCC 13 posix emits SjLj and does not link these
# libraries. This script is for a Windows host (GitHub windows-latest or
# Git Bash). It downloads the toolchain into build/toolchains, which is
# gitignored.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
libraries=(libc libkernel libSceAgc libSceAgcDriver libSceVideoOut libScePad
    libSceAudioOut libSceCommonDialog libSceIme libSceImeBackend libSceImeDialog
    libSceLibcInternal libSceNpGameIntent libSceNpTrophy2 libSceNpUniversalDataSystem
    libSceSaveData.native libSceSaveDataDialog.native libSceSysmodule
    libSceSystemService libSceUlt libSceUserService)
# A private dependency audit can request additional real upstream libraries.
# Never interpret the target file as shell code or fabricate missing modules.
if [ -n "${APS5_HLE_TARGETS_FILE:-}" ]; then
    [ -f "$APS5_HLE_TARGETS_FILE" ] || { echo "Missing HLE target file" >&2; exit 1; }
    while IFS= read -r library || [ -n "$library" ]; do
        library="${library%$'\r'}"
        [ -n "$library" ] || continue
        if [[ ! "$library" =~ ^lib[A-Za-z0-9_.]+$ ]] ||
            [ ! -d "$root/upstreams/AnyPS5/core/libs/prx/$library" ]; then
            echo "Unknown or unsafe HLE target: $library" >&2
            exit 1
        fi
        case " ${libraries[*]} " in
            *" $library "*) ;;
            *) libraries+=("$library") ;;
        esac
    done < "$APS5_HLE_TARGETS_FILE"
fi
if [ "${1:-}" = --print-hle-targets ] && [ "$#" = 1 ]; then
    printf '%s\n' "${libraries[@]}"
    exit 0
fi
[ "$#" = 0 ] || { echo "Usage: $0 [--print-hle-targets]" >&2; exit 1; }
"$root/scripts/apply-patches.sh"

url="https://github.com/brechtsanders/winlibs_mingw/releases/download/15.2.0posix-14.0.0-ucrt-r7/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-14.0.0-r7.7z"
archive_sha256="a914feafd7462126637d4b8196a31f3fb856ad929768ceb092c99969b43675b3"
dest="$root/build/toolchains/winlibs"
gcc=""
if [ -x "$dest/mingw64/bin/g++.exe" ]; then
    gcc="$dest/mingw64/bin/g++.exe"
elif [ -x "$dest/mingw64/bin/g++" ]; then
    gcc="$dest/mingw64/bin/g++"
fi
if [ -z "$gcc" ]; then
    mkdir -p "$root/build/toolchains"
    archive="$root/build/toolchains/winlibs-15.2.0posix-seh.7z"
    echo "downloading $url"
    curl -L --fail --retry 3 -o "$archive" "$url"
    printf '%s  %s\n' "$archive_sha256" "$archive" | sha256sum --check --strict -
    if [ -x "/c/Program Files/7-Zip/7z.exe" ]; then
        seven="/c/Program Files/7-Zip/7z.exe"
    elif command -v 7z >/dev/null 2>&1; then
        seven="$(command -v 7z)"
    else
        echo "7z is required to extract WinLibs" >&2
        exit 1
    fi
    mkdir -p "$dest"
    "$seven" x -y "-o$dest" "$archive"
    if [ -x "$dest/mingw64/bin/g++.exe" ]; then
        gcc="$dest/mingw64/bin/g++.exe"
    else
        echo "WinLibs extract did not produce mingw64/bin/g++.exe" >&2
        find "$dest" -name 'g++*' | head
        exit 1
    fi
fi

export PATH="$(dirname "$gcc"):$PATH"
echo "Windows compiler: $($gcc --version | head -1)"
"$gcc" --version | head -1 | grep -q '15\.2\.0' || {
    echo "expected GCC 15.2.0" >&2
    exit 1
}

# WinLibs ships a cmake whose curl CA store fails TLS (error 60) on the
# FFmpeg download inside AnyPS5's configure. GitHub's CMake is ahead of
# that directory when we call it by path. gcc, objcopy, and windres stay
# on PATH from mingw64/bin.
cmake_bin="${APS5_CMAKE:-cmake}"
if [ -z "${APS5_CMAKE:-}" ] && [ -x "/c/Program Files/CMake/bin/cmake.exe" ]; then
    cmake_bin="/c/Program Files/CMake/bin/cmake.exe"
fi
echo "cmake: $cmake_bin"
"$cmake_bin" --version | head -1

git -C "$root/upstreams/AnyPS5" submodule update --init --depth 1
build="$root/build/anyps5-winlibs"
"$cmake_bin" -S "$root/upstreams/AnyPS5" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$root/scripts/cmake/mingw-objcopy.cmake" \
    -DBUILD_TESTING=OFF \
    -DAPS5_SLIM=ON
jobs="${JOBS:-}"
if [ -z "$jobs" ]; then
    if command -v nproc >/dev/null 2>&1; then
        jobs="$(nproc)"
    else
        jobs=4
    fi
fi
"$cmake_bin" --build "$build" --target relinker nid_patcher "${libraries[@]}" --parallel "$jobs"

for library in "${libraries[@]}"; do
    prx="$build/core/libs/libs/unpatched/$library.prx"
    test -s "$prx" || { echo "Missing built PRX: $prx" >&2; exit 1; }
    wc -c "$prx"
done
echo "WinLibs build tree: $build"
