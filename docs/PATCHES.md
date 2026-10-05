# Patch status

Apply with `scripts/apply-patches.sh`. Reverse with
`scripts/apply-patches.sh --reverse`. The submodules in git stay at the
upstream commits; the patches are the delta.

## Verified on this Linux VM

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
  on Linux. `ios_srcwatch_arm` in `dibdrv/bitblt.c` is a weak symbol so
  the Linux `win32u.so` link does not require Madeira's
  `signal_arm64_ios.c`. The `#ifdef __APPLE__` side was not compiled
  here. `vulkan_ios.c` and `vulkan_metal_ios.c` were not compiled.
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
  PRX files.
- Wine under aarch64 FEX on this qemu-user. The nostdlib guest is the run
  that returned 42. Host Wine segfaults in qemu before the PE prints
  anything. `.github/workflows/arm64.yml` is the real `ubuntu-24.04-arm`
  run (native FEX, x86-64 wine from a debootstrap rootfs). Its result is
  not in this note until that job's log is recorded.

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

## Hosted runners, not yet a recorded result

These workflows are in the tree. A green or red log from them is not
copied into this file until that run has been read.

- `.github/workflows/macos.yml` on `macos-14` and `macos-15`: MoltenVK
  `make macos`, then `vk-requirements` (exit 1 is kept as a hard-miss
  log). `macos-14` also links `winevulkan.so` and compiles
  `vulkan_metal_ios.c`, `vulkan_ios.c`, and `moltenvk_static_loader.c`
  with the iPhoneOS SDK, and tries Madeira's app for the iOS Simulator
  with signing turned off.
- `.github/workflows/arm64.yml` on `ubuntu-24.04-arm`: native FEX, an
  amd64 debootstrap rootfs (qemu-user only for that install), then FEX
  plus that rootfs's wine on the synthetic PE.
- `.github/workflows/windows.yml`: WinLibs GCC 15.2.0 posix-seh,
  `libc.prx` and `libSceAgcDriver.prx` with `APS5_SLIM=ON`.

## Left for Konrad

Sign a build, install it with StikDebug on a real iPad, and boot a tiny
AnyPS5 PE (`MADEIRA_FEX_AVX=1`, `APS5_GUEST_ARENA_LAZY=1`). Read
`EntitlementChecker`'s `address-map` line (512 GB with the extended
virtual-addressing entitlement, 63 GB without). The plist key in
`patches/madeira/0002` does not grant the entitlement by itself. No
game dump.
