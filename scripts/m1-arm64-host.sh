#!/usr/bin/env bash
# On a real aarch64 Linux host: build FEX natively, unpack an x86-64
# ubuntu-base rootfs, and apt-install wine64 inside it. qemu-user-static
# is only for that chroot install. The nostdlib guest and, when
# SAMPLE_EXE is set, the PE run under FEX, not qemu.
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
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y qemu-user-static binfmt-support ca-certificates curl
if [ ! -x "$rootfs/bin/bash" ]; then
    # debootstrap of noble amd64 failed on this runner while configuring
    # required packages (the log named passwd). ubuntu-base is a packed
    # rootfs, so that second-stage configure does not run.
    base_url="https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release/ubuntu-base-24.04.5-base-amd64.tar.gz"
    tarball="$root/build/ubuntu-base-24.04.5-base-amd64.tar.gz"
    mkdir -p "$root/build"
    curl -L --fail --retry 3 -o "$tarball" "$base_url"
    sudo mkdir -p "$rootfs"
    sudo tar -C "$rootfs" -xzf "$tarball"
fi
sudo mkdir -p "$rootfs/proc" "$rootfs/sys" "$rootfs/dev" "$rootfs/tmp" "$rootfs/etc" \
    "$rootfs/usr/bin" "$rootfs/usr/sbin" "$rootfs/etc/apt/apt.conf.d" "$rootfs/etc/apt/sources.list.d"
if [ -x /usr/bin/qemu-x86_64-static ]; then
    sudo cp /usr/bin/qemu-x86_64-static "$rootfs/usr/bin/qemu-x86_64-static"
fi
if ! mountpoint -q "$rootfs/proc"; then
    sudo mount -t proc proc "$rootfs/proc"
fi
if ! mountpoint -q "$rootfs/sys"; then
    sudo mount -t sysfs sys "$rootfs/sys"
fi
if ! mountpoint -q "$rootfs/dev"; then
    sudo mount --bind /dev "$rootfs/dev"
fi
sudo cp /etc/resolv.conf "$rootfs/etc/resolv.conf"
# ubuntu-base's trusted.gpg.d keyrings are a filetype apt ignores, so a
# sources.list without Signed-By fails with NO_PUBKEY. The host keyring
# already verified archive.ubuntu.com (this script's own apt-get).
if [ ! -s /usr/share/keyrings/ubuntu-archive-keyring.gpg ]; then
    echo "host ubuntu archive keyring is missing" >&2
    exit 1
fi
sudo mkdir -p "$rootfs/usr/share/keyrings"
sudo cp /usr/share/keyrings/ubuntu-archive-keyring.gpg "$rootfs/usr/share/keyrings/ubuntu-archive-keyring.gpg"
sudo rm -f "$rootfs/etc/apt/sources.list" "$rootfs/etc/apt/sources.list.d/"*.sources "$rootfs/etc/apt/sources.list.d/"*.list
sudo tee "$rootfs/etc/apt/sources.list.d/ubuntu.sources" >/dev/null << 'EOF'
Types: deb
URIs: http://archive.ubuntu.com/ubuntu
Suites: noble noble-updates
Components: main universe
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: http://security.ubuntu.com/ubuntu
Suites: noble-security
Components: main universe
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
printf '%s\n' 'APT::Sandbox::User "root";' | sudo tee "$rootfs/etc/apt/apt.conf.d/99sandbox" >/dev/null
printf '%s\n' '#!/bin/sh' 'exit 101' | sudo tee "$rootfs/usr/sbin/policy-rc.d" >/dev/null
sudo chmod 755 "$rootfs/usr/sbin/policy-rc.d"
sudo chmod 1777 "$rootfs/tmp"
if ! sudo chroot "$rootfs" dpkg -s wine64 >/dev/null 2>&1; then
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
# Wine's setup_config_dir calls chdir(WINEPREFIX) and mkdir. This FEX
# fork passes both syscalls to the host kernel (Passthrough.cpp), so a
# directory that exists only inside the rootfs is still ENOENT. On
# 4b6d5a4, mkdir of $rootfs/home/fex/prefix still produced
# "wine: chdir to /home/fex/prefix : No such file or directory".
# The prefix therefore lives on the host, owned by the runner. /tmp is
# root-owned, which Wine refuses ("is not owned by you").
prefix="$HOME/fex-prefix"
mkdir -p "$prefix"
cp "$sample" "$HOME/sample.exe"
chmod 755 "$HOME/sample.exe"
# A read-only open tries the rootfs first and falls back to the host.
# Copy the PE into the rootfs at the same absolute path.
sudo mkdir -p "$rootfs$HOME"
sudo cp "$sample" "$rootfs$HOME/sample.exe"
sudo chmod 755 "$rootfs$HOME/sample.exe"
echo "host prefix $(ls -ld "$prefix" "$HOME/sample.exe")"
set +e
"$fex" /usr/bin/ls -ld "$prefix" "$HOME/sample.exe"
guest_ls=$?
set -e
echo "guest ls exit=$guest_ls"

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
# Ubuntu's wine64 keeps kernel32.dll beside ntdll.so under
# /usr/lib/x86_64-linux-gnu/wine, not next to /usr/lib/wine/wine64.
# On dba67bd Wine got past chdir and then exited 53:
# "could not load kernel32.dll, status c0000135". WINEDLLPATH is the
# directory that contains x86_64-windows/.
k32="$(sudo find "$rootfs/usr" -name kernel32.dll -path '*x86_64-windows*' -print -quit)"
echo "kernel32.dll ${k32:-missing}"
if [ -n "$k32" ]; then
    export WINEDLLPATH="$(dirname "$(dirname "${k32#"$rootfs"}")")"
    echo "WINEDLLPATH=$WINEDLLPATH"
fi
export WINEPREFIX="$prefix"
export WINEDEBUG="${WINEDEBUG:--all}"
set +e
"$fex" "$wine_guest" "$HOME/sample.exe"
pe_status=$?
set -e
echo "sample.exe exit=$pe_status"
if [ "$pe_status" -ne 42 ]; then
    echo "expected sample.exe exit 42 under FEX+wine" >&2
    exit 1
fi
echo "FEX + wine + synthetic PE exited 42"
