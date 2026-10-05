#!/usr/bin/env bash
# Cross-build AnyPS5 for Windows with Ubuntu's MinGW-w64 GCC 13 (posix).
#
# This is not WinLibs GCC 15.2.0 posix-seh from AnyPS5 BUILD.md.
#
# What this compiler can and cannot do:
# - relinker.exe links. The relinker target does not use the MINGW
#   -fno-asynchronous-unwind-tables flag, so it emits SEH and links against
#   Ubuntu's libgcc.
# - llvm-mingw Clang (UCRT) gets past the headers but fails in
#   AllocatingStrings.cpp: AnyPS5's guest SysV varargs use
#   __builtin_sysv_va_list, which Clang's mingw target does not provide.
# - libc.prx compiles and does not link. core/libs adds
#   -fno-asynchronous-unwind-tables, and this GCC then emits SjLj
#   (__gxx_personality_sj0, _Unwind_SjLj_*). Ubuntu's libgcc_eh defines
#   only __gxx_personality_seh0; unwind-sjlj.o is an empty stub.
#   Dropping the flag makes gas reject other TUs
#   (Filesystem.cpp: ".seh_handlerdata used outside of .seh_proc block"
#   on rename_nid_postfix, which GCC emits with no .seh_proc).
#
# The script builds relinker.exe, then tries libc and returns libc's status.
# It does not download game dumps. FFmpeg's CMake may download the mingw
# prebuilt zip from AnyPS5's release URL when the full tree configures.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$root/scripts/apply-patches.sh"

if ! command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1; then
    echo "need g++-mingw-w64-x86-64 (posix alternative)" >&2
    exit 1
fi

echo "Windows compiler: $(x86_64-w64-mingw32-g++-posix --version | head -1)"

git -C "$root/upstreams/AnyPS5" submodule update --init --depth 1
build="$root/build/anyps5-mingw"
cmake -S "$root/upstreams/AnyPS5" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/scripts/cmake/mingw-w64-posix.cmake" \
    -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$root/scripts/cmake/mingw-objcopy.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "$build" --target relinker --parallel "${JOBS:-$(nproc)}"
echo "relinker.exe: $build/core/relinker/relinker.exe"
set +e
cmake --build "$build" --target libc --parallel "${JOBS:-$(nproc)}"
libc_status=$?
set -e
if [ "$libc_status" -ne 0 ]; then
    echo "libc.prx did not link (SjLj personalities vs SEH-only libgcc on this GCC). relinker.exe above is the Windows PE this host produced." >&2
    exit "$libc_status"
fi
cmake --build "$build" --target libSceAgcDriver --parallel "${JOBS:-$(nproc)}"
echo "Windows build tree: $build"
