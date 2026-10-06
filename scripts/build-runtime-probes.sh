#!/usr/bin/env bash
# Build Windows x64 probes; compiling them is not a device runtime pass.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${RUNTIME_PROBES_OUT:-$root/build/runtime-probes}"
host_cxx="${HOST_CXX:-clang++}"
win_cxx="${WINDOWS_CXX:-x86_64-w64-mingw32-g++}"
mkdir -p "$out"
src="$root/tools/runtime-probes"
"$host_cxx" -std=c++17 -Wall -Wextra -Werror -O2 -DCPU_REFERENCE_ONLY "$src/cpu_probe.cpp" -o "$out/cpu-reference"
"$out/cpu-reference" 0x1234 > "$out/cpu-reference.jsonl"
"$win_cxx" -std=c++17 -Wall -Wextra -Werror -O2 -mno-avx -fno-tree-vectorize -c "$src/cpu_probe.cpp" -o "$out/cpu-probe.o"
"$win_cxx" -std=c++17 -Wall -Wextra -Werror -O2 -mavx2 -c "$src/cpu_avx2.cpp" -o "$out/cpu-avx2.o"
"$win_cxx" -static -static-libgcc -static-libstdc++ "$out/cpu-probe.o" "$out/cpu-avx2.o" -o "$out/cpu-probe.exe"
"$win_cxx" -std=c++17 -Wall -Wextra -Werror -Wno-cast-function-type -O2 -static -static-libgcc -static-libstdc++ \
    "$src/memory_probe.cpp" -o "$out/memory-probe.exe"
"$win_cxx" -std=c++20 -Wall -Wextra -Werror -Wno-cast-function-type -O2 -static -static-libgcc -static-libstdc++ \
    -I"$root/upstreams/AnyPS5/core/libs" "$src/allocator_probe.cpp" -o "$out/allocator-probe.exe"
printf 'Built Windows x64 probes and scalar reference in %s\n' "$out"
printf 'Run cpu-probe.exe [seed], then memory-probe.exe [exact-base] [size] on the target.\n'
printf 'A compiled probe or scalar reference does not establish AVX or memory behavior on iPad.\n'
