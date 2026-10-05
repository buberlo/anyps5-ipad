#!/usr/bin/env bash
# On a real aarch64 Linux host: build FEX natively, install an x86-64
# Ubuntu rootfs (debootstrap uses qemu-user only for that install), then
# run a nostdlib x86-64 guest and, when SAMPLE_EXE is set, that PE under
# the rootfs wine via FEX. The PE run is FEX, not qemu.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$(uname -m)" != "aarch64" ]; then
    echo "this script is for a real aarch64 host (this is $(uname -m))" >&2
    exit 1
fi

echo "cpu: $(grep -m1 'model name' /proc/cpuinfo || true)"
"$root/scripts/m1-build-fex-aarch64.sh"
fex="$root/build/fex-aarch64/Bin/FEX"
test -x "$fex"

rootfs="${FEX_ROOTFS:-/opt/x86-root}"
if [ ! -d "$rootfs/usr" ]; then
    sudo apt-get update
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y debootstrap qemu-user-static
    sudo debootstrap --arch=amd64 noble "$rootfs" http://archive.ubuntu.com/ubuntu
fi
if ! sudo chroot "$rootfs" dpkg -s wine64 >/dev/null 2>&1; then
    sudo tee "$rootfs/etc/apt/sources.list" >/dev/null << 'EOF'
deb http://archive.ubuntu.com/ubuntu noble main universe
deb http://archive.ubuntu.com/ubuntu noble-updates main universe
deb http://security.ubuntu.com/ubuntu noble-security main universe
EOF
    sudo chroot "$rootfs" apt-get update
    sudo chroot "$rootfs" env DEBIAN_FRONTEND=noninteractive apt-get install -y wine64
fi

mkdir -p "$HOME/.fex-emu"
cat > "$HOME/.fex-emu/Config.json" << EOF
{
  "Config": {
    "RootFS": "$rootfs"
  }
}
EOF
echo "FEX RootFS=$rootfs"

sudo apt-get install -y gcc-x86-64-linux-gnu file
hello_src="$root/build/m1-hello-x86.s"
mkdir -p "$root/build"
cat > "$hello_src" << 'EOF'
.global _start
_start:
    mov $42, %rdi
    mov $60, %rax
    syscall
EOF
x86_64-linux-gnu-gcc -static -nostdlib -nostartfiles -o "$root/build/m1-hello-x86" "$hello_src"
sudo cp "$root/build/m1-hello-x86" "$rootfs/tmp/hello-x86"
sudo chmod 755 "$rootfs/tmp/hello-x86"

export APS5_GUEST_ARENA_LAZY="${APS5_GUEST_ARENA_LAZY:-1}"
export APS5_GUEST_ARENA_SIZE="${APS5_GUEST_ARENA_SIZE:-0x100000000}"
echo "nostdlib guest under FEX"
set +e
"$fex" /tmp/hello-x86
hello_status=$?
set -e
echo "hello exit=$hello_status"
if [ "$hello_status" -ne 42 ]; then
    echo "expected nostdlib guest exit 42" >&2
    exit 1
fi

sample="${SAMPLE_EXE:-}"
if [ -z "$sample" ]; then
    echo "SAMPLE_EXE unset; skipped the Wine PE"
    exit 0
fi
test -f "$sample"
sudo cp "$sample" "$rootfs/tmp/sample.exe"
sudo mkdir -p "$rootfs/tmp/prefix"
sudo chmod 777 "$rootfs/tmp" "$rootfs/tmp/prefix"

wine_guest=""
while IFS= read -r cand; do
    if file "$cand" | grep -q 'ELF 64-bit LSB.*x86-64'; then
        wine_guest="${cand#"$rootfs"}"
        break
    fi
done < <(sudo find "$rootfs/usr" -type f \( -name wine64 -o -name wine -o -name wine-preloader \) 2>/dev/null)
if [ -z "$wine_guest" ]; then
    echo "no x86-64 wine ELF in $rootfs" >&2
    sudo find "$rootfs/usr" -iname '*wine*' -type f | head
    exit 1
fi
echo "wine ELF guest path: $wine_guest"
export WINEPREFIX=/tmp/prefix
export WINEDEBUG="${WINEDEBUG:--all}"
set +e
"$fex" "$wine_guest" /tmp/sample.exe
pe_status=$?
set -e
echo "sample.exe exit=$pe_status"
if [ "$pe_status" -ne 42 ]; then
    echo "expected sample.exe exit 42 under FEX+wine" >&2
    exit 1
fi
echo "FEX + wine + synthetic PE exited 42"
