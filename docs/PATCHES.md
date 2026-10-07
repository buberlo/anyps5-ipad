# Patch status

The implementation follow-ups are documented in
[IMPLEMENTATION.md](IMPLEMENTATION.md): transactional GuestArena (now folded into AnyPS5 0001),
accurate unavailable SDK telemetry (now upstream), the complete static Vulkan
bridge (0006), and the isolated runtime profile/build integration (0007).
The native protection-repair follow-up (0017) retains the fault terminal and
recognizes only a checked writable repair on the faulting thread/range. Build 12
passes the same original 5000-store probe that terminated at 2000 on Build 11;
an intentionally unrepaired handler still terminates. Vulkan lifecycle admission
and drain (0018) parks GPU calls while inactive, preserves native results and
tracks live devices. Host dispatch/quiescence tests and actual iPhoneOS compilation
pass; device validation is recorded separately in IMPLEMENTATION.md.
The foundation measurements below remain historical evidence.

Apply with `scripts/apply-patches.sh`. Reverse with
`scripts/apply-patches.sh --reverse`. The submodules in git stay at the
upstream commits; the patches are the delta.

## Current local refresh: 2026-10-07

The [recorded pins](UPSTREAMS.md) are built in isolated local checkouts. All
22 AnyPS5, 18 Madeira, two FEX and two Wine patches apply to pristine pinned
sources, match the built source and reverse successfully. The four optional
MoltenVK patches pass apply/reverse checks but were not used in Build 18.

The native iPhoneOS runtime and ARM64EC ntdll/FEX/Vulkan components were rebuilt,
linked into the development-signed menu app and verified against embedded hashes.
Build 18 is installed on the M2 iPad. All 11 HLE host contracts pass using the
qualified WinLibs compiler under local macOS Wine; the portable JSON and offline
API contracts also pass ASan/UBSan. This is not a native-Windows GPU reference.

The hardware audio-period regression reproduces the old callback starvation and
passes the new sizing at 44.1, 48 and 96 kHz. The iPad run reports 21.333 ms;
physical audio behavior and route changes remain unqualified. Solitaire is
installed with a complete dependency closure, but its bounded launch reached
Unity initialization without a game image. See the
[Build 18 record](evidence/ipad-m2-menu-build18-solitaire-install.json) and
[runtime measurement details](RUNTIME-PERFORMANCE.md).

## Menu lifecycle and serialized shader headers: 2026-10-08

Madeira 0028 restores explicit activation and resignation of the Vulkan gate
from the menu frontend, including its initial state when SwiftUI appears after
UIKit's first activation notification. Build 19 creates the device and three
swapchain images on the iPad; the native gate's quiescence/dispatch tests pass.
The bounded Solitaire launch then exposes an AGC shader-header alignment failure,
also reproduced under local macOS Wine. See the [Build 19 record](evidence/ipad-m2-menu-build19-solitaire-startup.json).

AnyPS5 0022 uses the dword alignment of serialized AGC shader headers and nested
user-data pointer tables, preserves the 96-byte header and field offsets, and
relocates packed pointer fields with byte copies. Code alignment and mapped-range
checks remain enforced. Its opt-in unreadable-range trace now reports the rejected
address, range, alignment and caller. The production creation/relocation test
checks headers at an address congruent to four modulo eight, null relative fields,
program-address register patching, and rejection of misaligned shader code.

The header/relocation change passes four portable ASan/UBSan contract groups
and all 12 qualified WinLibs Windows contracts under local macOS Wine. On the
iPad, the updated package's 138 files were hash-verified, and shader registration
now passes. Startup next aborts at `sceKernelRaiseException` with signal 30:
Unity installs a handler and needs real target-thread/context delivery for GC.
This call is not replaced by a successful no-op.

AnyPS5 0023 admits PA_SC_MODE_CNTL_1 bit 17, which partitions primitives across
AMD shader engines; Vulkan handles hardware distribution of the full draw.
Logical raster controls such as pixel killing, sample iteration and out-of-order
rasterization remain rejected. The focused `agc_driver_graphics_tests --state-only`
run passes; the full graphics suite still fails a separate fixed-function
interpolation shader-recompiler case. The focused result does not qualify the
whole renderer. See the [runtime repair record](evidence/ipad-m2-solitaire-header-state.json).

## ARM64 atomic access direction: 2026-10-08

Madeira 0029 classifies CAS/CASP and LSE read-modify-write instructions as write
faults before Wine/FEX exception delivery, including acquire forms that the old
bit-22 load/store shortcut classified as reads. It leaves ordinary loads,
load-exclusive instructions and LDAPR outside this override. It changes access
classification only: mapping ownership, permissions, dirty tracking and the
fault terminal remain enforced by the existing handlers.

Build 21 passes an independent x86-64 protection probe on the M2 iPad through
Wine/FEX: successful and failed CMPXCHG16B comparisons, XADD, XCHG and CMPXCHG
produce write faults on read-only pages, while a plain load from PAGE_NOACCESS
produces a read fault. The production instruction classifier also passes
ASan/UBSan host tests. The temporarily added probe entry and 512-MB JIT-pool
configuration are removed and the originals restored byte-for-byte.
[Build 21 record](evidence/ipad-m2-menu-build21-atomic-direction.json).

A private cooperative-SIGUSR1 experiment advances Solitaire past Unity GC
initialization but does not qualify arbitrary asynchronous guest signals. With
the atomic correction, a later null-vtable dereference and MoltenVK PerVertexKHR
compilation errors remain. The experimental signal sources are not part of the
production patch series; visible Solitaire gameplay is still unqualified.

## Historical foundation evidence

The following Linux and hosted-runner records describe earlier pins. Historical
patch numbers and unavailable components are not the status of the current
2026-10-07 runtime. GitHub Actions remain disabled; all current validation and
builds run locally.

### Verified on the original Linux VM

- `tools/vk-requirements` rebuilt from the current source and run against
  lavapipe (`/usr/share/vulkan/icd.d/lvp_icd.json`, Mesa 25.2.8, LLVM
  20.1.2). Exit 0. `summary hard_fail=0 optional_missing=3
  selected=llvmpipe (LLVM 20.1.2, 256 bits)`. The three optional misses
  are fragment shader barycentric, `VK_EXT_image_view_min_lod`, and
  `depthBounds`. `VK_KHR_swapchain` is reported as the extension only; no
  surface was created. `VK_EXT_metal_surface` is absent on this host.
  Stock and portability instance enumerations both see one device.
  `VK_KHR_portability_subset` is not advertised, so the portability-subset
  device path was not exercised.
- `patches/madeira/0003-winevulkan-moltenvk.patch` contains
  `moltenvk_static_loader.c`. `tools/checks/test_moltenvk_loader` compiles
  that file and checks the static-link fallback: a missing
  `libMoltenVK.dylib` resolves `vkGetInstanceProcAddr` and
  `vkGetDeviceProcAddr` from the process, and does not resolve other
  symbols. That is the loader policy. It is not a MoltenVK build and not
  an iOS link.
- AnyPS5 Linux Release build (`scripts/m0-build-anyps5.sh`) finished and
  linked the relinker. The 17 synthetic tests in
  `core/relinker/relinker/tests/` all passed against that binary. No title
  ELF was used.
- Synthetic PE under Ubuntu Wine 9 (`scripts/m0-synthetic-pe-wine.sh`).
  The Linux relinker turned `test_optional_plt.py`'s fixture into
  `sample.exe` and `sample-intel.exe` (`--windows` and
  `--windows --to-intel`). Both are `IMAGE_SUBSYSTEM_WINDOWS_CUI` and both
  exited 42. Lavapipe was selected (`VK_DRIVER_FILES` =
  `lvp_icd.json`). The PE does not call Vulkan, so this does not exercise
  the GPU path. Wine's prefix init printed a harmless
  `rundll32.exe` `c0000135`.
- Windows `relinker.exe` from Ubuntu MinGW-w64 GCC 13 posix
  (`build/anyps5-mingw/core/relinker/relinker.exe`, PE32+ console, 3.8 MiB).
  Under the same Wine it relinked the same fixture to a PE (subsystem 3).
  That PE printed `Transferring control to ELF entry point` and exited 42.
  `libc.prx` and `libSceAgcDriver.prx` did not link. See the MinGW block
  below.
- Pinned FEX `ios-port-2607` cross-built for aarch64 Linux
  (`scripts/m1-build-fex-aarch64.sh`, Clang `--target=aarch64-linux-gnu`,
  `-DTUNE_CPU=cortex-a78`, jemalloc glibc hook off). `Bin/FEX` and
  `Bin/FEXServer` are aarch64 PIE executables.
  `scripts/m1-wine-fex-arm64.sh` with no arguments ran a nostdlib x86-64
  guest (`mov $42, %rdi; mov $60, %rax; syscall`) under
  `qemu-aarch64-static` and exited 42. `APS5_GUEST_ARENA_LAZY=1` and
  `APS5_GUEST_ARENA_SIZE=0x100000000` were set; that guest does not read
  them. A glibc static hello died with `Fatal glibc error: Cannot allocate
  TLS block` (exit 127). `qemu-aarch64-static` plus host `wine` plus the
  synthetic PE segfaulted inside qemu (`uncaught target signal 11`) before
  any guest print. binfmt_misc is not mounted here; the script copies FEX
  and wraps `FEXServer` in a shell that re-enters qemu. This is a double
  emulator and not a timing result. The aarch64 binary includes
  `patches/fex/0001` through `0004` (rpmalloc POSIX log,
  `InitializeAllocator` declaration, iOS compile-block logs kept under
  `FEX_IOS_HOST`, and `VirtualQuery` compiled only on Windows).
- `scripts/check-fexbridge-avx.sh`: host exits were default=0, `MADEIRA_FEX_AVX=0` → 1,
  `MADEIRA_FEX_AVX=1` → 0. The same TU cross-compiled and under qemu
  defaulted to AVX on. `FEXBridge.mm` itself was not compiled.

## Compiled inside the Linux AnyPS5 build

- `patches/anyps5/0002-vulkan-portability.patch` is in
  `VulkanDevice.cpp.o` and in `libSceAgcDriver.prx`. The object file
  contains the strings `VK_KHR_portability_enumeration` and
  `VK_KHR_portability_subset`. `libSceAgcDriver` is `EXCLUDE_FROM_ALL`;
  the default `m0` build does not emit it, and a follow-up
  `cmake --build build/anyps5 --target libSceAgcDriver` did. Nothing
  created a `VkDevice` from that library on this run. The patch enables
  the two extensions only when the ICD advertises them.
- `patches/anyps5/0001-guest-arena-lazy.patch`: `GuestArena.cpp` compiled
  into `libc.prx` on Linux. The Windows constructor, including
  `ReadArenaLimits` and the lazy `Reserve` loop, is under `#ifdef _WIN32`,
  so the Release Linux object does not contain the `APS5_GUEST_ARENA_*`
  strings (the unused static parser is optimized out). The Linux
  constructor still compiles and leaves the arena unavailable.

## Compiled on a Linux host, not an iOS or Windows library link

- Madeira's Wine fork (`scripts/build-wine-vulkan-linux.sh`), configured
  `--enable-archs=x86_64` out of tree. `SONAME_LIBVULKAN` is
  `libvulkan.so.1`. Linked on this VM: `dlls/ntdll/ntdll.so` (4,130,936
  bytes), `dlls/win32u/win32u.so` (12,068,824 bytes), and
  `dlls/winevulkan/winevulkan.so` (6,997,864 bytes). `winevulkan.so`
  NEEDs `ntdll.so`, `win32u.so`, and `libc.so.6`. `win32u/vulkan.o`
  contains the string `libvulkan.so.1` (dlopen, not a DT_NEEDED). The PE
  `winevulkan.dll` and `vulkan-1.dll` still link. The unix libraries were
  not executed and did not create a `VkDevice`.
  `patches/wine/0001-linux-winevulkan-guards.patch` keeps the Apple
  `clock_gettime_nsec_np` / `mach_absolute_time` / QoS path under
  `#ifdef __APPLE__` and uses `CLOCK_MONOTONIC` plus a no-op eco apply
  on Linux. `ios_srcwatch_arm`, `ios_srcwatch_arm_geom`, and
  `winios_dump_srcbits` are weak definitions in `dibdrv/bitblt.c`
  (`nm` shows `W` on `dlls/win32u/dibdrv/bitblt.o`). A missing weak
  reference is not enough for Apple ld. The Apple link is what
  `winevulkan-ios` runs; the weak definitions are not measured there
  yet. `vulkan_ios.c` and `vulkan_metal_ios.c` were not compiled.
- `patches/anyps5/0001-guest-arena-lazy.patch` inside the MinGW libc
  objects: `GuestArena.cpp.obj` contains `APS5_GUEST_ARENA_BASE`,
  `APS5_GUEST_ARENA_SIZE`, `APS5_GUEST_ARENA_CHUNK`, and
  `APS5_GUEST_ARENA_LAZY`. That object is SjLj and is not in a linked
  `libc.prx`. `patches/anyps5/0003-mingw11-getthreaddescription.patch`
  supplies the prototype mingw-w64 11 headers omit;
  `CrashReport.cpp` compiled after that. The import lib already had the
  symbol.

## Compile-checked only as a translation unit, not linked into a PE

- A separate `g++ -c` of `GuestArena.cpp` as a Linux TU
  (`-Wno-unused-function`, because the parser is only called from the
  Windows constructor) keeps the `APS5_GUEST_ARENA_*` strings.
- MinGW `x86_64-w64-mingw32-g++ -c` of the same file type-checks the
  Windows constructor, the chunk reserve loop, and the env parser.
  `WindowsMappings.hpp` is included. That object contains the
  `APS5_GUEST_ARENA_*` strings. No `VirtualAlloc` call was executed.
  Pre-existing `-Wcast-function-type` warnings in `WindowsMappings.hpp`
  (GetProcAddress casts) are upstream, not from this patch, and the
  compile is not `-Werror`.

## Reviewed against the pinned sources, not compiled

- The rest of `0003` (`vulkan_ios.c`, `vulkan_metal_ios.c`, the
  `build.sh` / `driver_ios.c` edits) includes Wine unix headers and is
  built by Madeira's iOS `xcrun` script. Those TUs were not compiled here.
- `patches/madeira/0001-fexbridge-avx.patch` edits `FEXBridge.mm`. That
  file includes Mach and FEX headers and is Objective-C++. It was not
  compiled. The behavior it encodes: `MADEIRA_FEX_AVX=0` disables AVX;
  any other value, including unset, enables it; `SupportsSVE128` and
  `SupportsSVE256` stay false. See [ARCHITECTURE.md](ARCHITECTURE.md) for
  why SVE is not required and why `xtajit64` is a second switch.
- `patches/madeira/0002-extended-virtual-addressing.patch` adds one plist
  key. It does not sign a profile and does not change the address map.

## Blocked on this Linux VM

- Windows HLE libraries (`libc.prx`, `libSceAgcDriver.prx`) with the
  compilers installed here. Ubuntu GCC 13 posix plus AnyPS5's
  `-fno-asynchronous-unwind-tables` emits SjLj, and this libgcc has no
  SjLj runtime. llvm-mingw Clang cannot compile `__builtin_sysv_va_list`.
  `scripts/m0-build-anyps5-winlibs.sh` downloads WinLibs GCC 15.2.0
  posix-seh (`15.2.0posix-14.0.0-ucrt-r7`) and is what
  `.github/workflows/windows.yml` runs. This VM has not produced those
  PRX files. The GitHub `winlibs` job on `f1d3f06` did; paths are in
  the hosted-runner section. Byte sizes were not printed on `f1d3f06`.
  They were printed on `4b6d5a4`.
- Wine under aarch64 FEX on this qemu-user. The nostdlib guest is the run
  that returned 42. Host Wine segfaults in qemu before the PE prints
  anything. On GitHub `ubuntu-24.04-arm` (`a54b497`) native FEX ran that
  same nostdlib guest to exit 42, and `wine64` 9.0 was installed from
  ubuntu-base. The synthetic PE did not start: Wine exited 1 because
  `/tmp` was not owned by the runner user.

## Untested, and not claimed

- iOS app link of MoltenVK into `libwin32u_unix.a`.
- `vkCreateMetalSurfaceEXT` against a real `CAMetalLayer`.
- Lazy GuestArena under Wine's `MEM_RESERVE_PLACEHOLDER` implementation.
  AnyPS5 already uses placeholder reservations; Wine on iOS may reject
  them. That would be an M4 failure to record, not something this patch
  papers over.
- AVX2 correctness for PS5 code on 128-bit FEX IR.
- Whether a 512 GB iOS map can hold the default 448 GiB arena even with
  the entitlement. Prefer lazy mode and a smaller
  `APS5_GUEST_ARENA_SIZE` until a device log says otherwise.
- AnyPS5's Windows PE actually loading `vulkan-1.dll` from Madeira's
  arm64ec farm. `build/wine-pe/build-modules.sh` grows that farm when
  `MADEIRA_WITH_VULKAN=1`. The script was not run (it needs llvm-mingw
  and a macOS or cross setup Madeira documents).

## Slim configuration

`docs/SLIM.md`. `APS5_SLIM=1` on `scripts/m0-build-anyps5.sh` passes
`-DAPS5_SLIM=ON`: `libSceAgcDriver` loads `libvulkan.so.1` with `dlopen`
and does not link SDL. The Linux PRX went from 8,874,056 bytes (3,396
`SDL_` symbols) to 6,741,112 bytes (no `SDL_` symbols). Portability
extension strings are still in the slim binary. No `VkDevice` was
created. `APS5_SLIM=1 scripts/build-wine-vulkan-linux.sh` configures a
separate tree with OpenGL, Wayland, FreeType, GnuTLS, and the other
host libraries in that script turned off. `SONAME_LIBVULKAN` stays
`libvulkan.so.1`. `winevulkan.dll` is the same size as the default tree.
The slim tree was not rebuilt after `winevulkan.so` started linking;
the `.so` measured above is the default tree.
SDL remains in `libSceVideoOut` and FMOD. DXMT and `madeira-d3d12` are
not built.

## Hosted runners

On `54e5815` the Linux job passed: patches, lavapipe `hard_fail=0`, the
Linux AnyPS5 build including `libSceAgcDriver`, the 17 relinker tests,
and the synthetic PE under Wine. The `synthetic-pe` job passed again on
`2d7c2b8` and uploaded `sample.exe`.

On `2d7c2b8` the `winevulkan` job passed. A fresh configure (FreeType
and fontconfig off) found `-lvulkan` as `libvulkan.so.1` and linked
`ntdll.so`, `win32u.so`, and `winevulkan.so`, each an x86-64 ELF.
`win32u.so` NEEDs `ntdll.so`, `libm.so.6`, and `libc.so.6`.
`win32u/vulkan.o` contains `libvulkan.so.1`. The libraries were not
executed. Sizes were not printed on that runner; the byte sizes above
are still the local VM link.

On `a54b497` the `winevulkan` job passed again. `synthetic-pe` passed
again.

`moltenvk` on `macos-15` (`a54b497`) is the capability log. Stock
`vkCreateInstance` returned `VK_ERROR_INCOMPATIBLE_DRIVER` (-9).
`stock-anyps5-device-count` is 0. With
`VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` the count is 1.
`VK_EXT_metal_surface` is present. The device is `Apple Paravirtual
device`, integrated, API 1.1.357, graphics+compute. This is the
runner's virtual GPU, not an iPad. Every `HARD` line passed, including
`VK_KHR_8bit_storage`, `storageBuffer8BitAccess`,
`VK_KHR_buffer_device_address`, `bufferDeviceAddress`, `shaderInt64`,
`VK_KHR_shader_float_controls`,
`shaderSignedZeroInfNanPreserveFloat32`,
`vertexPipelineStoresAndAtomics`, `fragmentStoresAndAtomics`,
`samplerAnisotropy`, and `textureCompressionBC`. `VK_KHR_swapchain` is
present; no surface was created. The device advertises
`VK_KHR_portability_subset`. Summary: `hard_fail=0`,
`optional_missing=7`, exit 0. The optional misses are mesh shader,
shader clock, `VK_EXT_depth_range_unrestricted`,
`VK_EXT_primitive_topology_list_restart`, `VK_EXT_image_view_min_lod`,
`shaderFloat64`, and `depthBounds`. No `VkDevice` was created by AnyPS5.

The same commit's `macos-14` MoltenVK package built, then the
portability `vkCreateInstance` aborted (exit 134) inside
`MVKPhysicalDevice::initMetalFeatures`: `AppleParavirtDevice` does not
implement `newArgumentEncoderWithLayout:`. No `HARD` lines. That runner
is dropped from the matrix. It is not the measurement above.

`fex-wine` on `a54b497` linked native FEX, installed `wine64` 9.0 from
ubuntu-base, and ran the nostdlib guest to exit 42. `/usr/lib/wine/wine64`
then exited 1 with `wine: '/tmp' is not owned by you, refusing to
create a configuration directory there`. The PE did not start. The
prefix is now `/home/fex/prefix`, owned by the runner user. That run is
not recorded yet.

`winevulkan-ios` on `macos-14` stopped in Wine configure: `aarch64 PE
cross-compiler not found`. No iOS translation unit was compiled. The
script now downloads llvm-mingw `20260421` (`aarch64-w64-mingw32-clang`)
and the job runs on `macos-15`.

`madeira-simulator` on `macos-14` reached `xcodebuild` with Xcode 15.4
and failed in Swift: `BGContinuedProcessingTask` is not in that SDK
(`SteamDownloadBackground.swift`, `JITPairing.swift`). The job now
selects Xcode 26 or later on `macos-15`. That build is not recorded yet.

`linux` and `winlibs` on `2d7c2b8` and `a54b497` were cancelled before
a runner was assigned. The check annotation is `The job was not acquired
by Runner of type hosted even after multiple attempts`, at 15 minutes,
and the next push had not happened yet. Each workflow had one
`pull_request` run. `cancel-in-progress` did not fire those two
cancels. The concurrency group was still `*-${{ github.ref }}`, which is
`refs/pull/1/merge` for every push to this PR, so a newer push cancels
a workflow that is still pending in that group. The group is now the
commit SHA, and `cancel-in-progress` is false. If `windows-latest` or
`ubuntu-24.04` is cancelled before it starts, the same steps run on
`windows-2022` or `ubuntu-22.04`.

On `f1d3f06` the primary `linux` and `winevulkan` jobs passed, and
`linux-fallback` was skipped. The `winlibs` job passed and
`winlibs-fallback` was skipped. GCC is `g++.exe (MinGW-W64
x86_64-ucrt-posix-seh, built by Brecht Sanders, r7) 15.2.0`. CMake is
`/c/Program Files/CMake/bin/cmake.exe`. `APS5_SLIM=ON`. The link lines
are `core/libs/libs/unpatched/libc.prx` and
`core/libs/libs/unpatched/libSceAgcDriver.prx` under
`build/anyps5-winlibs`. The script printed both paths and then exited
0. It did not print byte sizes. `libkernel.prx` was linked as a
dependency of those targets. Nothing ran the PRX files.

`synthetic-pe` on `f1d3f06` was cancelled with the same
`not acquired by Runner` annotation. `fex-wine` was skipped, so the
`/home/fex/prefix` PE run is still unrecorded. The PE job now retries
on `ubuntu-22.04` when the `ubuntu-24.04` job is cancelled.

`winevulkan-ios` on `f1d3f06` and again on `c161948` (`macos-15`,
Xcode 26.3, llvm-mingw `20260421`) configured Wine. Configure said
libvulkan and libMoltenVK development files were not found. `ntdll.so`
linked. `win32u.so` failed: Apple ld reported missing
`_ios_srcwatch_arm`, `_ios_srcwatch_arm_geom`, and
`_winios_dump_srcbits` from `dibdrv_PutImage`. `weak_import` on
`c161948` did not change that. The three symbols are now weak
definitions in `bitblt.c`. That link is not recorded yet. The iOS
`-c` steps have not run.

`madeira-simulator` on `f1d3f06` selected Xcode 26 and failed at
`FEXBridge.mm:13` because `FEXCore/Config/Config.h` was not on the
search path. On `c161948` the script symlinked Madeira's nested FEX
path at the pinned tree and ran CMake. CMake identified AppleClang
17 and `/usr/bin/cc`, then failed: `string no output variable
specified` and `Unsupported processor type` (empty
`CMAKE_SYSTEM_PROCESSOR`). The script now passes `arm64`, the
iphoneos SDK path, and `xcrun` clang. `libdxmt_combined.a` is not built from llvm-ios. The simulator script
compiles `scripts/slim-dxmt-stub.c` into that archive (no-op symbols).
`x86_64-vcruntime` is Microsoft's redistributable
(`tools/fetch-vcruntime.md`); no Madeira script builds it, and the
slim path leaves the folder empty.

`fex-wine` on `c161948` (`ubuntu-24.04-arm`) linked native FEX, the
nostdlib guest exited 42, and `wine64` 9.0 was installed. The PE
exited 1: `wine: chdir to /home/fex/prefix : No such file or
directory`. The prefix directory is now created before that `chdir`.
That run is not recorded yet.

`linux` and `winevulkan` on `c161948` were cancelled before a runner
(`not acquired by Runner`, 15 minutes). `linux-fallback` was skipped
in the same second, so the `needs` condition never started
`ubuntu-22.04`. Those two jobs now also start on `ubuntu-22.04`
without waiting. `winlibs` on `c161948` got a runner and then failed:
`The hosted runner lost communication with the server`. That is not a
compile error. The `f1d3f06` PRX paths above still stand. A
`windows-2022` job runs when that result is not success.

On `4b6d5a4` these jobs passed: `linux` and `winevulkan` on
`ubuntu-24.04`, `winevulkan-ubuntu-22`, `moltenvk` on `macos-15`
(same portability `hard_fail=0` result), `synthetic-pe`, and
`winlibs`. The `winlibs` log printed `libc.prx` at 875965 bytes and
`libSceAgcDriver.prx` at 7068172 bytes, both under
`build/anyps5-winlibs/core/libs/libs/unpatched/`, `APS5_SLIM=ON`.
The PRX files were not executed. `winlibs-fallback` was skipped.

`winevulkan-ios` on `4b6d5a4` (`macos-15`, Xcode 26.3, Apple clang
17.0.0) linked `winevulkan.so` as a Mach-O 64-bit arm64 shared
library, and the iOS SDK `-c` steps produced
`build/ios-drafts/moltenvk_static_loader.o`, `vulkan_metal_ios.o`,
and `vulkan_ios.o`. Those objects were not executed. No `VkDevice`
was created.

`linux-ubuntu-22` failed in `make -C tools/vk-requirements`. Ubuntu
22.04's Vulkan headers do not declare
`VkPhysicalDeviceMeshShaderFeaturesEXT`,
`VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR`,
`VK_EXT_MESH_SHADER_EXTENSION_NAME`,
`VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME`,
`VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME`, or
`VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`. The `ubuntu-24.04`
job compiled the same file and passed. The tool now skips the two
optional feature structs when the header does not name them, and
supplies the portability names when they are missing. That compile
is not yet a hosted-runner result. On this VM, with current headers,
lavapipe still reports `hard_fail=0`.

`fex-wine` on `4b6d5a4` ran the nostdlib guest to exit 42 again.
`wine64` then exited 1: `wine: chdir to /home/fex/prefix : No such
file or directory`. The script had created that directory inside the
rootfs. FEX passes `chdir` and `mkdir` to the host, so the rootfs
directory is not the one Wine changes into. The prefix is now
`$HOME/fex-prefix` on the host, and the PE is copied to both
`$HOME/sample.exe` and the same path inside the rootfs. That run is
not recorded yet.

`madeira-simulator` on `4b6d5a4` configured FEX with the iphoneos SDK
and `xcrun` clang (the empty-processor failure is gone) and then
stopped. `External/unordered_dense`, `External/xxhash`,
`External/fmt`, and `External/range-v3` were not checked out, and
`Scripts/aarch64_fit_native.py` could not import `packaging`.
`NeedDisabledSVE.py` did not find `/proc/cpuinfo`; cmake continued
with "Platform has bugged SVE". The DXMT stub did not run. The script
now checks out those four FEX submodules and sets `TUNE_CPU=none` so
the native CPU probe is not used for an iOS cross build. That
configure is not recorded yet. The app target still lists
`libntdll_unix.a`, `libwin32u_unix.a`, `libwineserver.a`,
`libavformat.a`, and `libmadeira_rppairing.a`, which this script does
not build.

On `dba67bd` the Ubuntu 22.04 capability tool compiled and ran.
lavapipe (LLVM 15.0.7) API 1.3.255, `hard_fail=0`,
`optional_missing=5` (mesh shader, fragment shader barycentric,
`VK_EXT_image_view_min_lod`, `VK_KHR_maintenance8`, `depthBounds`).
`VK_KHR_portability_enumeration` was absent. The same step then
failed to compile `moltenvk_static_loader.c`: `RTLD_DEFAULT` is
undeclared in that glibc unless `_GNU_SOURCE` is set. The loader
now defines `RTLD_DEFAULT` as `((void *)0)` when the header does
not.

On `2e74b7d` that loader compiled. `linux-ubuntu-22` then failed in
the MinGW `GuestArena.cpp` step. Ubuntu 22.04's mingw-w64 reports
`std::mutex` does not name a type in `WindowsMappings.hpp`, and
`MEM_RESERVE_PLACEHOLDER`, `MEM_REPLACE_PLACEHOLDER`,
`MEM_PRESERVE_PLACEHOLDER`, and `MEM_COALESCE_PLACEHOLDERS` are
undeclared. The `ubuntu-24.04` job compiles the same TU and passes.
`scripts/check-linux.sh` now compiles a probe with `<windows.h>` and
`<mutex>` and skips the MinGW TU when that probe fails. That skip is
not a hosted-runner result yet. The rest of the 22.04 AnyPS5 build
has not been reached.

`fex-wine` on `dba67bd` ran the nostdlib guest to exit 42. The host
prefix `/home/runner/fex-prefix` existed, and a guest `ls` of it
exited 0. `wine64` then printed `wine: could not load kernel32.dll,
status c0000135` and exited 53. Ubuntu stores that DLL at
`/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/kernel32.dll`.

On `2e74b7d` the script found that file under the rootfs and set
`WINEDLLPATH=/usr/lib/x86_64-linux-gnu/wine`. The nostdlib guest
still exited 42, the guest `ls` of the host prefix still exited 0,
and Wine still printed `could not load kernel32.dll, status
c0000135` twice and exited 53. `WINEDEBUG=-all` hid the search.
wineserver opens that path from a second process. The script now
bind-mounts that Wine directory and `/usr/share/wine` onto the host
at the same paths, unregisters qemu's x86 binfmt handlers after the
chroot install, and runs Wine with `WINEDEBUG=+module,+file`. That
run is not recorded yet.

`madeira-simulator` on `dba67bd` configured and built the iOS FEX
archives (`libFEXCore.a` 4,867,464 bytes, `libFEXCore_Base.a`
238,360 bytes) and compiled the slim `libdxmt_combined.a`.
`xcodebuild` then failed in `MadeiraJITHelper`: StikJIT's
`arm64-apple-ios.private.swiftinterface` was emitted by Swift 6.4
(`Swift::Sendable`), and Xcode 26.3 reports `expected '{' in struct`
at that colon.

On `2e74b7d` the script rewrote those `::` qualifiers to `.`.
`xcodebuild` then reported `'StikJIT' is not a member type of enum
'StikJIT.StikJIT'` and `failed to build module 'StikJIT'`: the SDK
was built with Apple Swift 6.4 (swiftlang-6.4.0.34.1) and the
compiler is Apple Swift 6.2.4 (swiftlang-6.2.4.1.4). The unsigned
CI build now drops the Madeira target's dependency on
`MadeiraJITHelper` and leaves the appex out of the embed phase.
`StikJITHelper.swift` in the app does not import that module.
`libntdll_unix.a`, `libwin32u_unix.a`, `libwineserver.a`, the FFmpeg
archives, and `libmadeira_rppairing.a` are still absent.
`build/wineserver/build.sh` exits with `No base libwineserver.a
found` on a clean tree. That `xcodebuild` is not recorded yet.

## Device boot

The signed runtime has booted on an iPad Air 13-inch M2. The PS5 build of
Dreaming Sarah launches from the app library and is playable in a basic
sense. See the [README](../README.md) and
[IMPLEMENTATION.md](IMPLEMENTATION.md). The plist key in
`patches/madeira/0002` still does not grant extended virtual addressing by
itself; the provisioning profile has to include it. No game dump belongs in
this repository.
