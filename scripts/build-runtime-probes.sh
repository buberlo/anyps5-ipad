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
"$win_cxx" -std=c++20 -Wall -Wextra -Werror -Wno-cast-function-type -O2 -static -static-libgcc -static-libstdc++ \
    -I"$root/upstreams/AnyPS5/core/libs" "$src/memory_probe.cpp" -o "$out/memory-probe.exe"
"$win_cxx" -std=c++20 -Wall -Wextra -Werror -Wno-cast-function-type -O2 -static -static-libgcc -static-libstdc++ \
    -I"$root/upstreams/AnyPS5/core/libs" "$src/allocator_probe.cpp" -o "$out/allocator-probe.exe"
"$win_cxx" -std=c++17 -Wall -Wextra -Werror -O2 -mavx -ffreestanding \
    -fno-exceptions -fno-rtti -fno-stack-protector -nostdlib \
    -Wl,--entry=mainCRTStartup "$src/protected_write_probe.cpp" -lkernel32 \
    -o "$out/protected-write-probe.exe"
"$win_cxx" -x c -std=c11 -Wall -Wextra -Werror -O2 -mcx16 -static-libgcc \
    "$src/atomic_protection_probe.c" -o "$out/atomic-protection-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -O2 -mavx2 -static-libgcc \
    "$src/guest_argument_probe.c" -o "$out/guest-argument-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -O2 -mno-red-zone -static-libgcc \
    "$src/negative_arithmetic_probe.c" -o "$out/negative-arithmetic-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -Wno-cast-function-type -O2 -static-libgcc \
    "$src/native_write_fault_probe.c" -o "$out/native-write-fault-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -O2 -static-libgcc \
    "$src/redzone_fault_probe.c" -o "$out/redzone-fault-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -O2 -mavx2 -static-libgcc \
    "$src/write_watch_probe.c" -o "$out/write-watch-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -Wno-cast-function-type -O2 -static-libgcc \
    "$src/special_apc_probe.c" -o "$out/special-apc-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -O2 -static-libgcc \
    -I"$root/upstreams/Madeira/build/ntdll-unix" \
    "$src/protected_store_state_probe.c" -o "$out/protected-store-state-probe.exe"
"$win_cxx" -x c -std=gnu11 -Wall -Wextra -Werror -O2 -static-libgcc \
    "$src/self_read_probe.c" -o "$out/self-read-probe.exe"
printf 'Built Windows x64 probes and scalar reference in %s\n' "$out"
printf 'Run cpu-probe.exe [seed], then memory-probe.exe [exact-base] [size] on the target.\n'
printf 'A compiled probe or scalar reference does not establish AVX or memory behavior on iPad.\n'
