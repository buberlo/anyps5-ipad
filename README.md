# anyps5-ipad

Research prototype for running AnyPS5-relinked PS5 titles on an iPad.

The chain is AnyPS5 (`--windows --to-intel`) producing an x86-64 Windows PE,
then [Madeira](https://github.com/willfaust/Madeira) on iOS 26: Wine 11
ARM64EC plus a FEX fork, JIT via StikDebug. AnyPS5's GPU path is Vulkan.
Madeira's GPU path today is DXMT and Metal, and its Wine build is
configured without Vulkan. The work in this tree is to put MoltenVK under
`winevulkan` and to make the guest address space fit an iPad.

Owner: Konrad Kern. This is a long-term research project. The current tree
is a foundation and the first patch drafts, not a title running on a device.

## Status

| Piece | State |
| --- | --- |
| Upstream pins (AnyPS5, Madeira, FEX, Wine, MoltenVK) | recorded as submodules |
| Architecture, milestones M0–M5, legal note | written |
| Vulkan capability tool | lavapipe (Mesa 25.2.8, LLVM 20.1.2): `hard_fail=0`, exit 0 |
| M4 patch drafts (Vulkan, AVX, GuestArena, entitlement) | in `patches/`; see [docs/PATCHES.md](docs/PATCHES.md) for what was compiled |
| iOS build or device run | not done; this environment is a Linux VM |
| M0 AnyPS5 build | Linux relinker, `libc`, `libkernel`, `libSceAgcDriver` built. Synthetic PE exits 42 under Wine (lavapipe selected, Vulkan unused). MinGW `relinker.exe` also produced that PE under Wine. Ubuntu GCC 13 does not link `libc.prx`. WinLibs GCC 15.2 on `windows-latest` (`4b6d5a4`) linked `libc.prx` (875965 bytes) and `libSceAgcDriver.prx` (7068172 bytes), `APS5_SLIM=ON`. The PRX files were not executed |
| M1 Wine+FEX on ARM64 Linux | On `ubuntu-24.04-arm` (`15978d4`), native FEX ran a nostdlib x86-64 guest to exit 42, and the synthetic PE exited 42 under FEX plus Ubuntu `wine64`. The fixture does not call Vulkan |
| Linux winevulkan | `ntdll.so`, `win32u.so`, and `winevulkan.so` linked on this VM and on GitHub `ubuntu-24.04`. Not executed. On `macos-15` (`4b6d5a4`) `winevulkan.so` linked as Mach-O arm64, and the iOS SDK compiled `moltenvk_static_loader.o`, `vulkan_metal_ios.o`, and `vulkan_ios.o`. Those objects were not executed |
| MoltenVK on macOS | `macos-15` runner, portability instance: `hard_fail=0` on Apple Paravirtual device (API 1.1.357). Stock instance sees 0 devices. Not an iPad GPU |
| CI | On `3a947d5`: `linux`, `linux-ubuntu-22` (g++ 12 linked `libSceAgcDriver.prx`), `winevulkan`, `winevulkan-ubuntu-22`, `moltenvk`, `winevulkan-ios`, `synthetic-pe`, `winlibs`, and `fex-wine` passed. `madeira-simulator` built GnuTLS, FFmpeg, and FreeType, then `ntdll-unix` failed three files (`ri_page_wait_time_mach`, `dcommon.h`, `strmif.h`). The app did not link. The slim unix build now omits `dwrite`, `winegstreamer`, and FFmpeg. That link is not a hosted-runner result yet. Details are in [docs/PATCHES.md](docs/PATCHES.md) |
| Slim Vulkan path | [docs/SLIM.md](docs/SLIM.md). `APS5_SLIM=1` drops SDL from `libSceAgcDriver` (8.87 MB → 6.74 MB on Linux). DXMT and D3D are not in this build |

What to leave out of a Vulkan-only iPad build: [docs/SLIM.md](docs/SLIM.md).

Details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/MILESTONES.md](docs/MILESTONES.md),
[docs/PATCHES.md](docs/PATCHES.md),
[docs/LEGAL.md](docs/LEGAL.md).

## Layout

```
upstreams/     pinned submodules, unmodified until you apply patches
patches/       one series per upstream
scripts/       apply patches, Linux checks, M0/M1 entry points
tools/vk-requirements/   AnyPS5 hard-requirement probe
docs/
```

## First commands

```sh
git submodule update --init
scripts/apply-patches.sh
scripts/check-linux.sh
```

`check-linux.sh` needs a C compiler, MinGW (`x86_64-w64-mingw32-g++`), and a
Vulkan loader. It selects lavapipe when
`/usr/share/vulkan/icd.d/lvp_icd.json` is present. Exit 0 from the tool
means that ICD meets AnyPS5's hard checks. Exit 1 means the tool ran and the
ICD failed one or more; that is still a successful run of the tool.

M0, when you want the full AnyPS5 build:

```sh
scripts/m0-build-anyps5.sh          # Linux relinker and HLE libraries
scripts/m0-synthetic-pe-wine.sh     # fixture PE under Wine; expect exit 42
scripts/m0-build-anyps5-windows.sh  # MinGW relinker.exe; libc.prx link fails on Ubuntu GCC 13
scripts/m1-build-fex-aarch64.sh     # aarch64 FEX + FEXServer
scripts/m1-wine-fex-arm64.sh        # qemu-user smoke; nostdlib guest exits 42
scripts/build-wine-vulkan-linux.sh  # ntdll.so, win32u.so, winevulkan.so, PE ICD
scripts/check-fexbridge-avx.sh
```

That script needs the X11 and GL development packages AnyPS5's SDL2 check
looks for (`libx11-dev`, `libxext-dev`, `libxrandr-dev`, `libxcursor-dev`,
`libxi-dev`, `libxfixes-dev`, `libxss-dev`, `libxrender-dev`,
`libxxf86vm-dev`, `libdrm-dev`, `libudev-dev`, `libdbus-1-dev`,
`libgl1-mesa-dev`, `libegl1-mesa-dev`). On this VM the first configure
failed on a missing `Xext.h` until those were installed. The script does
not install them.

## iPad configuration the patches expect

These are switches, not measurements:

```sh
MADEIRA_WITH_VULKAN=1          # Madeira build: winevulkan + MoltenVK unix side
MADEIRA_FEX_AVX=1              # xtajit64's FEXCore; the in-process bridge defaults on
APS5_GUEST_ARENA_LAZY=1        # reserve guest VA in 256 MiB chunks
APS5_GUEST_ARENA_SIZE=0x100000000   # 4 GiB example; default remains 448 GiB
```

`Madeira.entitlements` gains `com.apple.developer.kernel.extended-virtual-addressing`
only after the patch is applied. The provisioning profile still has to grant it.

## Legal

No game dumps, keys, or firmware belong in this repo. AnyPS5 is GPL-2.0-only,
Madeira is GPL-3.0-or-later, and they are kept as separate submodules.
[docs/LEGAL.md](docs/LEGAL.md).
