#!/usr/bin/env bash
# Build AnyPS5 libc.prx and libSceAgcDriver.prx with WinLibs GCC 15.2.0
# posix-seh (15.2.0posix-14.0.0-ucrt-r7), the compiler AnyPS5's BUILD.md
# requires. Ubuntu's GCC 13 posix emits SjLj and does not link these
# libraries. This script is for a Windows host (GitHub windows-latest or
# Git Bash). It downloads the toolchain into build/toolchains, which is
# gitignored.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$root/scripts/apply-patches.sh"

url="https://github.com/brechtsanders/winlibs_mingw/releases/download/15.2.0posix-14.0.0-ucrt-r7/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-14.0.0-r7.7z"
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

git -C "$root/upstreams/AnyPS5" submodule update --init --depth 1
build="$root/build/anyps5-winlibs"
cmake -S "$root/upstreams/AnyPS5" -B "$build" -G Ninja \
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
cmake --build "$build" --target libc --target libSceAgcDriver --parallel "$jobs"

found=0
while IFS= read -r prx; do
    echo "PRX $prx"
    found=1
done < <(find "$build" -name 'libc.prx' -o -name 'libSceAgcDriver.prx')
if [ "$found" -ne 1 ]; then
    echo "libc.prx and libSceAgcDriver.prx were not both produced" >&2
    exit 1
fi
test -n "$(find "$build" -name 'libc.prx' -print -quit)"
test -n "$(find "$build" -name 'libSceAgcDriver.prx' -print -quit)"
echo "WinLibs build tree: $build"
