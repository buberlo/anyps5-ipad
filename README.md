# anyps5-ipad

Research prototype for running AnyPS5-relinked PS5 titles on an iPad.

The chain is AnyPS5 (`--windows --to-intel`) producing an x86-64 Windows PE,
then [Madeira](https://github.com/willfaust/Madeira) on iPadOS: Wine 11
ARM64EC plus a FEX fork, JIT via StikDebug. AnyPS5's GPU path is Vulkan.
The pinned Madeira upstream uses DXMT and Metal; this tree adds a Vulkan-only
build connecting `winevulkan` to MoltenVK and tests guest memory placement.

Owner: Konrad Kern. This is a long-term research project. The current tree
has a complete signed iPad runtime build and a native GPU device proof.
Standalone x64 AVX2 and GPU readbacks now pass through Wine/FEX on iPad;
a short Win32 swapchain test has visible output. The 464–468 GiB candidate
passes mapping/allocator probes. Real relinked PS5 HLE CPU/exception tests and
the original RDNA scene now pass on the device; full-game acceptance remains open.

## Runtime implementation

The implementation branch adds the iOS `winevulkan` Unix dispatch archive,
static MoltenVK surface-loader fixes, transactional lazy GuestArena, executable
CPU/memory/GPU probes, and an original relinkable 2D guest. The complete iPhoneOS
build keeps the real JIT helper and pairing/FFmpeg libraries. The private app's
bundle ID is `com.konradkern.anyps5ipad`.

On the physical M2 iPad, the separate native probe passed device creation,
buffer-device-address, 8-bit and 64-bit compute readbacks, and BC1 sampling.
The full app is signed and installed; a standalone Windows AVX2 probe passes
through Wine/FEX with built-in StikJIT. The original 320×192 RDNA scene has
visible output and a clean 900-frame GPU/readback run. Build 9 shows Xbox touch
controls and exports complete session logs directly. These bounded tests do not
prove touch input, presented FPS or the 1280×720 ten-minute acceptance.
A Build 10 diagnostic also completes 120 GPU comparisons with a 1280×720
source buffer and visible output, scaled into a 768×432 swapchain. Its guest
loop averages 23.130 iterations/s; full-resolution performance acceptance
remains open. See the [720p evidence](docs/evidence/ipad-m2-demo-720-diagnostic.json).
A follow-up game profile creates a true 1280×720 swapchain through the existing
SDL/Wine path; all 180 GPU comparisons pass and Wine exits 0. Its short guest
loop averages 26.379/s. See [full surface proof](docs/evidence/ipad-m2-demo-720-full-surface.json).
With global `env.MADEIRA_PAD_EARLY_SLOT = 1`, actual UI touch taps now reach
`scePad` through XInput/SDL: left/right move the paddle and A serves twice.
See [bounded input proof](docs/evidence/ipad-m2-touch-input-early-slot.json).

See [implementation and verification](docs/IMPLEMENTATION.md) for build commands
and the remaining device gates. The tables below are the **foundation's
historical evidence**, not a statement that a commercial game now works.
In particular, main's ARM64 CI subsequently executed the synthetic PE with
exit 42; the old `kernel32.dll` failure is resolved for that fixture.

The original demo needs no game data. It exercises public PS5 HLE imports and
a hand-written RDNA compute program before presenting frames. Linker-only
import stubs are kept separate from the real runtime PRX files.
Dreaming Sarah requires a privately supplied decrypted dump. The tested private
version reaches the main menu on Windows. On iPad its scaling/swapchain issue
is repaired. Build 10 reaches Vulkan with the shader-fixed graphics library and
no longer reports the vertex subgroup error. The optional native interpolation path now executes on iPad without the previous
`PerVertexKHR` errors. The user observes an initial logo, followed by audible
sound and a black screen. A further shader is conservatively rejected at pc 84,
and repeated native write faults later terminate the process. Gameplay remains
unverified. See [interpolation device diagnostic](docs/evidence/ipad-m2-dreaming-sarah-interpolation-device.json).
Thirty production-guard cases and six Apple Metal shader compilations pass.
Opt-in shader capture now occurs before source validation so newly rejected
draws can be diagnosed; that capture change awaits a new build/device run.
Game assets, shader requests and complete game logs stay outside this repository.

## Historical foundation status

| Piece | State |
| --- | --- |
| Upstream pins (AnyPS5, Madeira, FEX, Wine, MoltenVK) | recorded as submodules |
| Architecture, milestones M0–M5, legal note | written |
| Vulkan capability tool | lavapipe (Mesa 25.2.8, LLVM 20.1.2): `hard_fail=0`, exit 0 |
| M4 patch drafts (Vulkan, AVX, GuestArena, entitlement) | in `patches/`; see [docs/PATCHES.md](docs/PATCHES.md) for what was compiled |
| iOS build or device run | not done; this environment is a Linux VM |
| M0 AnyPS5 build | Linux relinker, `libc`, `libkernel`, `libSceAgcDriver` built. Synthetic PE exits 42 under Wine (lavapipe selected, Vulkan unused). MinGW `relinker.exe` also produced that PE under Wine. Ubuntu GCC 13 does not link `libc.prx`. WinLibs GCC 15.2 on `windows-latest` (`4b6d5a4`) linked `libc.prx` (875965 bytes) and `libSceAgcDriver.prx` (7068172 bytes), `APS5_SLIM=ON`. The PRX files were not executed |
| M1 Wine+FEX on ARM64 Linux | On `ubuntu-24.04-arm` (`2e74b7d`), native FEX ran a nostdlib x86-64 guest to exit 42. The host prefix was visible to the guest. `WINEDLLPATH` pointed at Ubuntu's `kernel32.dll`, and Wine still exited 53: `could not load kernel32.dll, status c0000135` |
| Linux winevulkan | `ntdll.so`, `win32u.so`, and `winevulkan.so` linked on this VM and on GitHub `ubuntu-24.04`. Not executed. On `macos-15` (`4b6d5a4`) `winevulkan.so` linked as Mach-O arm64, and the iOS SDK compiled `moltenvk_static_loader.o`, `vulkan_metal_ios.o`, and `vulkan_ios.o`. Those objects were not executed |
| MoltenVK on macOS | `macos-15` runner, portability instance: `hard_fail=0` on Apple Paravirtual device (API 1.1.357). Stock instance sees 0 devices. Not an iPad GPU |
| CI | On `2e74b7d`: `linux`, `winevulkan`, `winevulkan-ubuntu-22`, `moltenvk` (`macos-15`), `winevulkan-ios`, `synthetic-pe`, and `winlibs` passed. `linux-ubuntu-22` passed the capability tool (`hard_fail=0`) and failed compiling MinGW `GuestArena.cpp`. `fex-wine` and `madeira-simulator` failed as recorded in [docs/PATCHES.md](docs/PATCHES.md) |
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
