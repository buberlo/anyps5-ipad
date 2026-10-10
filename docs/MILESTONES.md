# Milestones

## MiniGolf MRT color-export candidate qualified, 2026-10-10

Precise MRT0–7 source-read footprints cover the seventh captured request,
without changing EXEC, CFG, whole-quad, interpolation or shader translation.
The actual graphics PRX passes 302 public cases and all seven complete shader
replays. Six retained SPIR-V/layout pairs remain byte-identical; the 3,692-symbol
ABI, native imports and 3,186 guest NIDs still close. Native optimized and
sanitized checks pass 239 distinct namespace cases and detect three unsafe
mutations. Both seventh-request MSL variants create macOS Metal source
libraries; no pipeline or GPU execution is qualified. The repairs are saved
in canonical source and patch0004. Execution evidence belongs to the isolated
driver; its native Windows game comparison, fresh production rebuild and
iPad execution remain pending.
[MRT qualification](evidence/minigolf-mrt-export-candidate-20261010.json),
[MiniGolf checkpoint](MINIGOLF.md).

## MiniGolf scalar-pair and array-load candidate qualified, 2026-10-10

Three exact register-effect forms cover the sixth request: scalar-buffer
`DWORDx2`, plain 2D-array image load and plain MSAA-array image load. The existing
translator and EXEC/CFG/whole-quad/interpolation proof remain unchanged.
The actual loaded PRX passes 234 public cases and six complete shader replays.
All five previous SPIR-V/layout pairs remain byte-identical, and the export ABI,
native imports and 3,186 guest NIDs still close. Native decoder regressions and
seven independent CPU memory/alias/mask witnesses also pass with ASan/UBSan.
The sixth request translates in both MSL argument-buffer modes and creates
Metal 2.4 source libraries on Apple M3 Pro. No GPU pipeline or iPad execution
is qualified by these checks. Native Windows passes all eight prerequisites,
including the 234 actual-PRX cases and 553 AVX-verified exception deliveries.
MiniGolf then exits after 7.180 seconds at the next interpolation-guard failure;
the swapchain is 837 × 471 with three images, with no captured game frames.
Promotion, game rendering and iPad execution remain pending.
[MiniGolf checkpoint](MINIGOLF.md),
[qualified candidate](evidence/minigolf-array-load-candidate-20261010.json).

## MiniGolf lane-math driver qualified, next shader isolated, 2026-10-10

Four exact register-effect footprints restore the unused-input proof for the
fifth captured shader without changing existing arithmetic or EXEC/WQM guards.
The new actual PRX passes 178 public cases, five complete shader replays,
regressions and prior-runtime negative controls. A reviewed internal auto-export
exclusion restores the exact 3,692-symbol ABI; all 3,186 guest NIDs remain resolved.
The five SPIR-V modules translate to Apple MSL; both fifth-request variants
compile as macOS Metal source libraries. Offline AIR and iPad GPU behavior remain
unqualified. Native Windows passes all eight prerequisites but MiniGolf
exits after 6.312 seconds at a sixth distinct shader request. No game rendering,
iPad execution or gameplay acceptance is claimed. The candidate remains isolated.
[MiniGolf checkpoint](MINIGOLF.md),
[sanitized qualification record](evidence/minigolf-lane-math-candidate-20261010.json).

## MiniGolf volume-sampler Windows comparison, 2026-10-10

The isolated 3D extension passes all eight native Windows prerequisites,
including 144 actual-PRX shader cases, native AVX/exception checks and Radeon
780M offscreen readbacks. MiniGolf then exits after 6.455 seconds at a different
2,975-byte shader request. Local actual-PRX replay reproduces that rejection
while retaining all 144 public cases. The decoder finds 76 instructions and
twelve CFG blocks; four lane-local instruction forms need exact footprint
rules in the unused-input proof. The shader has no implicit sample or WQM
instruction, so those guards remain unchanged. No game frames or iPad execution
are qualified. [Comparison evidence](evidence/minigolf-volume-windows-next-shader-20261010.json),
[MiniGolf checkpoint](MINIGOLF.md).

## MiniGolf 3D-sampler failure isolated, 2026-10-10

The isolated quad-mask graphics driver passes 121 public actual-PRX cases,
three complete retained game-shader requests and all eight native Windows
prerequisites. The physical Radeon 780M probe passes Vulkan device creation,
BDA/8-bit buffer readback and BC1 sampling readback. MiniGolf's verified
170-file package then exits after 6.28 seconds with a black window, before any
owned window capture. This is an execution/diagnostic result, not gameplay.

Its complete 2,111-byte request roundtrips exactly and reproduces the failure
through the actual PRX on local Wine. The decoder isolates plain implicit-LOD
3D sampling with three coordinates; the current unused-input proof models only
the two-coordinate 2D form. All 121 public cases still pass. The isolated 3D
extension now passes 144 actual-PRX cases and four complete retained requests,
with the previous PRX as a rejecting negative control. The original sole cleanup
failure remains recorded; a separate terminal-state reconciliation verifies both
owned prefixes and reuses only successful results with exact input/log hashes.
The extension is not promoted or physically GPU qualified. iPad game rendering
remains unqualified. [Diagnostic evidence](evidence/minigolf-3d-sampler-diagnostic-20261010.json),
[MiniGolf checkpoint](MINIGOLF.md).

The preceding quad-mask production source has also completed a fresh full build:
47 PRXs, 53 passing host cases, 121 actual-PRX cases, three complete requests
and a sealed runtime with a new source-derived shader-cache identity. The local
Wine AVX-context subcase is explicitly unavailable. This is separate from the
isolated 3D extension and physical GPU/gameplay acceptance.
[Fresh production evidence](evidence/minigolf-quad-mask-full-production-20261010.json).

## Combined AnyPS5 runtime prepared, 2026-10-10

The recorded six-patch baseline qualifies together on upstream `d70b8998`:
47 real HLE PRXs, 42 selected and ten additional test programs, 53 distinct
passing cases. There are no failures or CTest skips; one internal asynchronous
AVX subcase remains unavailable on local Wine. The actual source snapshot and
630 newly compiled project objects are recorded. Final export sealing preserves
an immutable qualification and verifies all 50 payload files plus acyclic
provenance. [Combined evidence](evidence/anyps5-combined-runtime-20261010.json).
This historical artifact predates the latest cumulative shader-proof expansion.

That sealed runtime's shader proof passes 45 public staged cases and the exact private
MiniGolf request; its independent CPU model checks 921,600 raw-input comparisons
in each optimized/sanitized run and detects three unsafe mutations. The new
MiniGolf package relinks twelve ELFs, resolves 3,186 guest NIDs and verifies
170 files against the sealed HLE runtime. **Prepared does not mean imported,
device executed or playable.** That package has no qualified Windows or
iPad game run. Historical Native55 `nullDescriptor` and Windows interpolation
failures remain recorded. [MiniGolf checkpoint](MINIGOLF.md),
[compiler proof](evidence/anyps5-unused-barycentric-proof-20261010.json),
[preparation](evidence/minigolf-combined-main-preparation-20261010.json).

## Solitaire gameplay milestone, 2026-10-09

**15in1 Solitaire's PS5 build now runs locally on the M2 iPad and accepts real
card actions.** A 12-minute device run verifies Golf's full board, stock draws,
a legal 2♣ onto A♥ move exposing 9♣, and Undo. The same game process survives
the seven reviewed inputs and the complete run. This is the main compatibility
achievement, beyond startup or a visible menu.

Getting there required CPU-state preservation across exceptions and protected
writes, retained guest-module initialization, validated shader/descriptor
handling, eight logical MSAA samples mapped to two native four-sample layers,
color/stencil transfers and resolve, and explicit per-draw transitions. The
[Solitaire technical account](SOLITAIRE.md) explains the blockers, implemented
repairs, tested configuration and remaining work. [Gameplay proof](evidence/ipad-solitaire-zero-stencil-invariant-controls-20261009.json).

## Current installed milestone, 2026-10-09

Native55 is now installed in place on the M2 iPad using the existing bundle ID.
Its game execution is not yet qualified. The previous component milestone was
Native54 with the Solitaire Source65 package. That run's selection menu renders
correctly, with all 18 touch-control labels and an
FPS-only overlay. The same native main remains alive at 15, 30, 60 and 120
seconds. The 512 MiB JIT pool, functional game profile and installation
manifest are retained. The observations below refer to that Native54 run.

- **Audio cadence:** optional patch0065 paces the legacy AudioOut producer.
  A controlled comparison changes the captured four-packet repetition from
  1,855/1,870 matching fingerprints to 0/1,870, and consecutive identical
  native blocks from 463/467 to zero. These are digital-content observations,
  not a listening-quality or simulation-speed acceptance.
- **Native diagnostics:** Madeira0051 skips the complete statistical region,
  hole and malloc-zone census when the existing Diagnostics setting is off.
  The installed host still reports pool warming, footprint sampling and
  passing heap checks. FEX0002 separately gates two routine messages; this
  source change is not in the installed FEX DLL.
- **Remaining qualification:** the latest XCTest inspector times out while
  enabling automation mode and sends no touches. Card gameplay with Source65,
  audible quality, save/load, lifecycle, sustained memory and performance
  remain open. Earlier legal moves belong to their recorded profiles. A
  screenshot's FPS reading is not a matched benchmark or constant-60 result.
  The component-check log reports a serious-to-critical thermal transition;
  its temperature state was not matched for a performance comparison.

The [AudioOut record](AUDIOOUT-FRAME-PACING.md) separates the restored capture
pair from the subsequent installation. The [native component evidence](evidence/quiet-runtime-diagnostics-20261009.json)
records the installed host and preserved checks. Detailed switches are in
[runtime performance](RUNTIME-PERFORMANCE.md).

## Earlier foundation and gameplay milestones

The earlier gameplay result is in the [README](../README.md): the PS5 build of
Dreaming Sarah boots on an M2 iPad and is playable in a basic sense. Details
and the open gates are in [IMPLEMENTATION.md](IMPLEMENTATION.md). The foundation
log below is the 2026-10-05 run. Its notes that M1 was not closed, and that
the iOS build was not run from that Linux VM, describe that run.

Main's ARM64 run 37386696225 executed `sample.exe` with exit 42 after the Wine
tree bind-mount fix. That closes the synthetic CPU startup check only; it does
not execute AnyPS5 PRX, Vulkan or a title. The former M1 exit-53 report below
belongs to the earlier commits explicitly named there.

Acceptance is observational. A milestone is done when the check below has
been run and the result recorded, including a failure with a log. Guessing
that a later stage will pass does not close an earlier one.

This 2026-10-05 foundation run closed none of M0–M5 end to end. M5's later
device result is recorded in that section. The run added the repo, the
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

On GitHub `ubuntu-24.04-arm` (`a54b497`, `c161948`, `4b6d5a4`,
`dba67bd`, and `2e74b7d`) native FEX linked, ubuntu-base 24.04.5
supplied `wine64` 9.0, and a nostdlib x86-64 guest exited 42 under
FEX. On `2e74b7d` the host prefix was visible inside the guest,
`kernel32.dll` was on disk, `WINEDLLPATH` was
`/usr/lib/x86_64-linux-gnu/wine`, and the PE still exited 53:
`wine: could not load kernel32.dll, status c0000135`. M1 is not
closed.

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

## M5 — First menu on an iPad

**Acceptance**

- An M-series iPad, ideally 16 GB of RAM, sideloaded Madeira with the M4
  patches, StikDebug providing JIT.
- One legally obtained title that AnyPS5 already runs (the only title named
  in AnyPS5's `docs/user/COMPATIBILITY.md` at the pinned commit is Dreaming
  Sarah) reaches its menu.
- Frame time, missing Vulkan features, and FEX faults are written down.
  The milestone bar is one menu reached on device.

**Recorded.** Build 10 reached the New game / Continue / Options menu on the
M2 iPad. On 2026-10-06 a maintainer screen recording went further: library
launch, the title and Options screens, New game, and about 60 seconds of
touch-controlled gameplay with the character visible. Displayed frame time
was not measured on that recording. The open gates (displayed FPS, long
sessions, full background recovery, save/load, formal audio acceptance,
other iPads, no public IPA) are in the README status table. Meeting the menu
bar, and the later basic-gameplay observation, leave those gates open.
