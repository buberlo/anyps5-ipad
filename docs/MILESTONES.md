# Milestones

Acceptance is observational. A milestone is done when the check below has
been run and the result recorded, including a failure with a log. Guessing
that a later stage will pass does not close an earlier one.

This foundation run closed none of M0–M5 end to end. It added the repo, the
capability tool (run on lavapipe, `hard_fail=0`), a Linux AnyPS5 build, and
the M4 patch drafts. Per-item status is in [PATCHES.md](PATCHES.md).

## M0 — AnyPS5 on x86-64

Build AnyPS5 on x86-64 and produce a Windows PE with `--windows --to-intel`.
Run that PE natively on Windows, or run the project's own tests on Linux.

**Acceptance**

- `scripts/m0-build-anyps5.sh` finishes, or the log shows the first failing
  target.
- `cmake --build build/anyps5 --target libs` produces the HLE libraries.
- A relink of a user-supplied ELF is not required to close M0. If no ELF is
  available, an upstream test binary under `core/` is enough.
- No title, firmware, or key is downloaded to make the build pass.

**This run.** `scripts/m0-build-anyps5.sh` finished on this x86-64 Linux VM
(gcc/g++ 13.3, CMake 3.28, Ninja, Release). It produced:

- `build/anyps5/core/relinker/relinker`, a 3.5 MiB statically linked x86-64 ELF
- `libc.prx` (includes `GuestArena.cpp` with patch 0001 applied)
- `libkernel.prx`
- `libSceAgcDriver.prx` (includes `VulkanDevice.cpp` with patch 0002 applied)

`libSceAgcDriver` is `EXCLUDE_FROM_ALL`, so the default build did not emit
it. A follow-up `cmake --build build/anyps5 --target libSceAgcDriver` did.
The 17 synthetic Python tests under
`core/relinker/relinker/tests/` were run against that relinker and all
printed `PASS` (`failed=0`). They do not load a title. No user ELF was
relinked.

The Linux relinker also wrote a Windows PE from the `test_optional_plt.py`
fixture (`scripts/m0-synthetic-pe-wine.sh`). Under Ubuntu Wine 9, with
lavapipe selected, both the `--windows` PE and the `--to-intel` PE exited
42. The fixture does not call Vulkan.

`scripts/m0-build-anyps5-windows.sh` uses Ubuntu MinGW GCC 13 posix, not
WinLibs GCC 15.2. It linked `build/anyps5-mingw/core/relinker/relinker.exe`.
That PE, run under Wine, relinked the same fixture, and the result exited
42. `libc.prx` did not link: the MINGW unwind flag selects SjLj and this
libgcc only has SEH. llvm-mingw Clang stops on `__builtin_sysv_va_list`.

On GitHub `windows-latest` (`4b6d5a4`) WinLibs GCC 15.2.0 posix-seh
linked `build/anyps5-winlibs/core/libs/libs/unpatched/libc.prx`
(875965 bytes) and `libSceAgcDriver.prx` (7068172 bytes) with
`APS5_SLIM=ON`. The PRX files were not executed.

## M1 — The same PE under Wine + FEX on ARM64 Linux

**Acceptance**

- An ARM64 Linux machine, FEX, and Wine.
- The PE from M0 starts under `FEXInterpreter wine64` far enough to reach
  guest entry without a Vulkan ICD.
- `APS5_GUEST_ARENA_LAZY=1` is set so startup does not reserve 448 GiB.
- A missing game is an acceptable stop. A crash inside GuestArena or FEX
  before any guest instruction is not.

**This run.** The pinned FEX fork cross-compiled to aarch64
(`Bin/FEX`, `Bin/FEXServer`). On this x86-64 VM,
`scripts/m1-wine-fex-arm64.sh` ran that FEX under `qemu-aarch64-static`
(no binfmt; a shell wrapper re-execs `FEXServer` via qemu). A nostdlib
x86-64 guest exited 42. A glibc static hello failed with
`Cannot allocate TLS block`. Host Wine plus the synthetic PE segfaulted
in qemu before the PE printed anything. This does not close M1: there is
no ARM64 machine here, and the PE never reached guest entry under FEX.

On GitHub `ubuntu-24.04-arm` (`15978d4`) native FEX linked,
ubuntu-base 24.04.5 supplied `wine64` 9.0, and a nostdlib x86-64
guest exited 42 under FEX. After the Wine library directory was
bind-mounted onto the host, the synthetic PE exited 42 under
`FEX` plus `wine64`. The module trace mapped `kernel32.dll` from
`Z:\usr\lib\x86_64-linux-gnu\wine\x86_64-windows\kernel32.dll`.
`APS5_GUEST_ARENA_LAZY=1` was set. The fixture does not call Vulkan
and does not exercise GuestArena. A title was not run.

## M2 — Vulkan under Wine + FEX on ARM64 Linux

**Acceptance**

- M1, plus a real ICD (`VK_DRIVER_FILES` pointing at lavapipe or hardware).
- `tools/vk-requirements/vk-requirements` inside that environment reports
  `hard_fail=0`, or the log names the first hard feature the ICD lacks.
- The PE creates a `VkDevice` (AnyPS5 log line `Vulkan device ready`) or
  fails on a feature this tool already reported.

## M3 — winevulkan + MoltenVK on macOS

**Acceptance**

- Apple Silicon Mac, MoltenVK, Wine built with Vulkan.
- The capability tool against MoltenVK. Record `stock-anyps5-device-count`
  versus `portability-device-count`, and every `HARD` line.
- Expected risks, not yet measured: `textureCompressionBC`,
  `shaderInt64`, buffer-device address, 8-bit storage, and the portability
  subset. A missing hard feature ends M3 with a written gap, not a silent
  skip.

On `macos-15` (`a54b497`) the capability tool against MoltenVK exited 0.
Stock `vkCreateInstance` returned `VK_ERROR_INCOMPATIBLE_DRIVER`.
`stock-anyps5-device-count` is 0 and `portability-device-count` is 1.
The device is the runner's `Apple Paravirtual device` (integrated, API
1.1.357), not an iPad GPU. Every hard check passed, including
`textureCompressionBC`, `shaderInt64`, buffer-device address, and
8-bit storage. The device requires `VK_KHR_portability_subset`.
`optional_missing=7` (mesh shader, shader clock, unrestricted depth
range, primitive topology list restart, image view min lod,
`shaderFloat64`, `depthBounds`). `macos-14` aborts inside MoltenVK
before any hard line, on that image's paravirtual Metal device. No
`VkDevice` was created by AnyPS5. Wine on that Mac was not linked.

## M4 — Madeira patches on iOS

**Acceptance**

- Wine's iOS unix build defines `SONAME_LIBVULKAN` and `dlsym` returns
  MoltenVK's `vkGetInstanceProcAddr` from inside the app process.
- `winios_pVulkanInit` creates a `VkSurfaceKHR` from the HWND's
  `CAMetalLayer` (`VK_EXT_metal_surface`).
- `MADEIRA_FEX_AVX=1` is set for the title, and a VEX instruction executes
  under FEX instead of raising SIGILL. `=0` still disables AVX.
- GuestArena comes up with `APS5_GUEST_ARENA_LAZY=1` and a size that
  `VirtualQuery`/`VirtualAlloc` accepts. The default 448 GiB eager reserve
  is not the iPad configuration.
- `Madeira.entitlements` contains
  `com.apple.developer.kernel.extended-virtual-addressing`, and a signed
  build's profile grants it (`EntitlementChecker` already prints
  `profile-extended-va`). The address map is the 512 GB one, not the 63 GB
  one.
- iOS builds are not run from the Linux VM. Device logs are the evidence.

**This run.** On GitHub `macos-15` (`15978d4`, Xcode 26.3, iPhoneOS 26.2)
the unsigned Madeira app compiled after `MadeiraJITHelper` was omitted.
`ld` stopped with `library 'wineserver' not found`. The app did not
link. MoltenVK was not loaded from the app process. Madeira does not
publish those static archives (the v0.1.3 release is an IPA). The
follow-up compiles `libntdll_unix.a`, `libwin32u_unix.a`, and
`libwineserver.a` from the pinned Wine fork and stubs on-device
pairing. On `3a947d5` GnuTLS, FFmpeg, and FreeType built, and
`ntdll-unix` failed three files (`ri_page_wait_time_mach`,
`dcommon.h`, `strmif.h`) before the link. The slim link now leaves
`dwrite`, `winegstreamer`, and FFmpeg out of that unix build. That
omission is not a hosted-runner result yet. `APS5_SLIM=0` still
builds the full variant, including the widl headers and FFmpeg.

## M5 — First menu on an iPad

**Acceptance**

- An M-series iPad, ideally 16 GB of RAM, sideloaded Madeira with the M4
  patches, StikDebug providing JIT.
- One legally obtained title that AnyPS5 already runs (the only title named
  in AnyPS5's `docs/user/COMPATIBILITY.md` at the pinned commit is Dreaming
  Sarah) reaches its menu.
- Frame time, missing Vulkan features, and FEX faults are written down.
  Reaching the menu once is the bar, not playability.
