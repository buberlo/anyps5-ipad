#!/usr/bin/env bash
# Run an x86-64 program under FEX on aarch64.
# On a real ARM64 host this is: FEXInterpreter wine64 program.exe
# On this x86-64 VM, if scripts/m1-build-fex-aarch64.sh has produced Bin/FEX
# and qemu-aarch64-static is installed, the same binary runs under qemu-user.
# qemu-user plus FEX is a double emulator. It is a smoke test, not a
# measurement of iPad performance.
#
# binfmt_misc is not assumed. FEX execs FEXServer from its own directory
# (/proc/self/exe). Under qemu-user that exec hits the host kernel, which
# cannot run an aarch64 ELF without binfmt. The script copies FEX into
# build/fex-aarch64/qemu-run and places a shell FEXServer beside it. The
# shell re-enters qemu-aarch64-static. A copied FEX (not a symlink) is
# required: the kernel's /proc/self/exe target is the realpath.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
guest="${1:-}"
fex_bin="${FEX:-$root/build/fex-aarch64/Bin/FEX}"

run_fex() {
    local interpreter="$1"
    shift
    export APS5_GUEST_ARENA_LAZY="${APS5_GUEST_ARENA_LAZY:-1}"
    export APS5_GUEST_ARENA_SIZE="${APS5_GUEST_ARENA_SIZE:-0x100000000}"
    echo "APS5_GUEST_ARENA_LAZY=$APS5_GUEST_ARENA_LAZY APS5_GUEST_ARENA_SIZE=$APS5_GUEST_ARENA_SIZE"
    echo "command: $interpreter $*"
    exec "$interpreter" "$@"
}

if [ "$(uname -m)" = "aarch64" ]; then
    if [ -z "$guest" ]; then
        echo "usage: $0 /path/to/program" >&2
        exit 1
    fi
    if ! command -v FEXInterpreter >/dev/null 2>&1 && [ ! -x "$fex_bin" ]; then
        echo "FEXInterpreter is not on PATH and $fex_bin is missing" >&2
        exit 1
    fi
    interpreter="${FEXInterpreter:-FEXInterpreter}"
    if [ -x "$fex_bin" ]; then interpreter="$fex_bin"; fi
    if [[ "$guest" == *.exe ]]; then
        if ! command -v wine64 >/dev/null 2>&1 && ! command -v wine >/dev/null 2>&1; then
            echo "wine64 is not on PATH" >&2
            exit 1
        fi
        wine_bin="$(command -v wine64 || command -v wine)"
        run_fex "$interpreter" "$wine_bin" "$guest"
    fi
    run_fex "$interpreter" "$guest"
fi

if [ ! -x "$fex_bin" ]; then
    cat <<EOF
M1 was not run. This machine is $(uname -m) and $fex_bin does not exist.
Build it with scripts/m1-build-fex-aarch64.sh, then re-run this script.
On a real ARM64 host the command is:

  FEXInterpreter wine64 ${guest:-/path/to/program.exe}

  APS5_GUEST_ARENA_LAZY=1
  APS5_GUEST_ARENA_SIZE=0x100000000
EOF
    exit 0
fi

if ! command -v qemu-aarch64-static >/dev/null 2>&1; then
    echo "$fex_bin exists but qemu-aarch64-static is not installed" >&2
    exit 1
fi

bin_dir="$(cd "$(dirname "$fex_bin")" && pwd)"
server_elf="$bin_dir/FEXServer"
if [ -x "$bin_dir/FEXServer.aarch64" ]; then
    server_elf="$bin_dir/FEXServer.aarch64"
fi
if ! file "$server_elf" | grep -q 'ARM aarch64'; then
    echo "need an aarch64 FEXServer next to $fex_bin (cmake --build ... --target FEXServer)" >&2
    exit 1
fi

run_dir="$root/build/fex-aarch64/qemu-run"
mkdir -p "$run_dir"
cp -f "$fex_bin" "$run_dir/FEX"
cat > "$run_dir/FEXServer" << EOF
#!/bin/bash
exec qemu-aarch64-static -L /usr/aarch64-linux-gnu $(printf '%q' "$server_elf") "\$@"
EOF
chmod +x "$run_dir/FEXServer"

qemu=(qemu-aarch64-static -L /usr/aarch64-linux-gnu "$run_dir/FEX")
if [ -z "$guest" ]; then
    guest="$root/build/m1-hello-x86"
    if [ ! -x "$guest" ]; then
        mkdir -p "$root/build"
        # nostdlib: a glibc static hello dies under this qemu-user with
        # "Fatal glibc error: Cannot allocate TLS block" (exit 127).
        cat > "$root/build/m1-hello-x86.s" << 'EOF'
.global _start
_start:
    mov $42, %rdi
    mov $60, %rax
    syscall
EOF
        gcc -static -nostdlib -nostartfiles -o "$guest" "$root/build/m1-hello-x86.s"
    fi
    echo "no guest given; using nostdlib x86-64 program (expect exit 42)"
fi

echo "qemu-user is emulating aarch64 FEX, which emulates the x86-64 guest."
echo "This is not an iPad and not a timing result. binfmt_misc is not used."
if [[ "$guest" == *.exe ]]; then
    if ! command -v wine >/dev/null 2>&1 && ! command -v wine64 >/dev/null 2>&1; then
        echo "wine is not installed; cannot run $guest under FEX" >&2
        exit 1
    fi
    wine_bin="$(command -v wine64 || command -v wine)"
    run_fex "${qemu[@]}" "$wine_bin" "$guest"
fi
run_fex "${qemu[@]}" "$guest"
