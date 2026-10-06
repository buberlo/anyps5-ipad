# Runtime implementation and verification

The source baseline is main `20bb9809e5f19624c7ae27fd129c32ebfad055e8` and
its pinned upstream commits. Selected build repairs from PR #2 are included;
the pins themselves have not moved. This document distinguishes source work,
build artifacts, host execution and physical-device acceptance.

## Implemented paths

- A real iOS `winevulkan` Unix archive, named call table and Madeira binder
  entry connect the ARM64EC PE DLL to `win32u` and MoltenVK. Missing symbols
  fail the build. The static loader uses valid dispatch instead of calling
  `dlsym` on its sentinel; direct symbol references prevent dead stripping.
  The host dispatch restores the portability-subset extension that Wine's
  extension filter drops. Vulkan surfaces retain the existing game Metal
  layer in game mode and the HWND's desktop layer in desktop mode.
- Lazy GuestArena scans host allocations and reserves chunks transactionally.
  A failed multichunk reservation rolls back both newly reserved pages and
  allocator bookkeeping. Fixed-address failures are reported.
- The iPhoneOS app has an isolated ID and an AnyPS5 runtime profile: AVX on,
  lazy guest arena, touch/XInput enabled. It preserves the JIT helper and
  builds real Wine, pairing, font and media archives. Its private URL scheme
  is `anyps5ipad://`, including JIT callbacks. Initial configuration uses the
  qualified M2 candidate 464–468 GiB arena; existing user settings are retained.
- The original guest demo imports public `sce*` APIs through NIDs, feeds RDNA
  instructions to AGC, checks the GPU result and presents through VideoOut.
  Madeira touch → XInput → SDL → `scePad` remains the input path.

## Build and test

Prerequisites: pinned submodules, Python 3, CMake, Ninja, glslangValidator,
spirv-val, MinGW GCC for Windows fixtures, and Xcode 27 for the actual helper.
The full iOS build also needs LLVM-MinGW 20260421, Rust with
`aarch64-apple-ios`, bison 3, flex and pkg-config. CI pins the LLVM archive hash
and Rust version. Set `DEVELOPER_DIR` per process; do not change global Xcode
selection. Build artifacts and toolchains stay in ignored `build/` directories.

```sh
git submodule update --init --depth 1
scripts/apply-patches.sh
scripts/link-madeira-siblings.sh
python3 tools/checks/check_patch_stack.py
scripts/check-vulkan-integration.sh
scripts/build-host-relinker.sh
scripts/run-relinker-tests.sh build/host-tools/build/relinker/relinker
scripts/build-runtime-probes.sh
scripts/build-demo.sh --profile smoke
scripts/build-runtime-guest.sh
scripts/build-runtime-exceptions.sh
```

`check_patch_stack.py` reconstructs only touched files from each pinned HEAD,
applies every patch in order and reverses the full stack in a temporary tree.
It never resets a developer's checkout. Complete-stack detection also makes
`apply-patches.sh` safe to repeat when a later patch changes earlier context.

Run the Windows CPU, mapping and allocator programs on Windows for the
reference and in the intended Madeira runtime for the device result. Building
a PE and running the scalar host reference do not prove FEX correctness.
The allocator probe includes the actual patched upstream implementation.
The separate relinked CPU fixture exercises real HLE callbacks, two threads,
ELF TLS templates, pthread key destructors, stack arguments and atomic counters.
The exception fixture retains actual ELF DWARF unwind tables and public libc
NID imports for heap allocation, typed catch, nested rethrow and destructors.
An ARM64 host reference for its C++ logic is not proof of guest unwinding.

```sh
build/runtime-probes/cpu-probe.exe
build/runtime-probes/memory-probe.exe
build/runtime-probes/allocator-probe.exe
```

Build pinned MoltenVK with `fetchDependencies --ios --macos`, then `make ios`
and `make macos`. Supply the resulting libraries explicitly:

```sh
MOLTENVK_LIB=/absolute/path/libMoltenVK.dylib scripts/build-gpu-probe.sh macos
build/gpu-probe/macos/gpu-probe build/gpu-probe/macos
MOLTENVK_IOS_LIB=/absolute/path/libMoltenVK.a scripts/build-ipad-probe.sh
APS5_MOLTENVK_LIBRARY=/absolute/path/libMoltenVK.a scripts/m3-madeira-ios.sh
```

The native probe creates a device, dispatches a buffer-device-address shader
that checks 64-bit carry/multiplication and individual byte writes with untouched
guard bytes, then samples a known BC1 texture and compares the returned color.
CPU Vulkan implementations cannot pass the hardware gate. Its JSON Lines report and
artifact manifest are retained. This is an **offscreen native GPU test**,
not a Wine presentation or game test. The separate iPad probe ID is
`com.konradkern.anyps5ipad.probe`; its report is in Documents/gpu-probe.jsonl.
The probe uses a single `UIWindowScene`; active start and completion are recorded,
and any intervening loss of foreground invalidates device acceptance. Metadata
includes source hashes, model/OS, memory, thermal state and Low Power Mode.
The sealed bundle manifest labels executable hashes as **before codesigning**;
`artifact-manifest.json` outside the bundle records the final signed bytes.

The old `m3-madeira-simulator.sh` name remains a compatibility wrapper for
the actual iPhoneOS build. The full app uses the real JIT helper. The CI
`xcode-27` image supports its Swift version; compile failures are never converted
to success. Signing is opt-in through the explicit signing variables printed
by the build scripts; none of the build scripts installs or launches an app.
The three PE runtime modules (`xtajit64`, `winevulkan`, `vulkan-1`) get a separate
source/build manifest. Final app verification rejects missing, stale or changed
embedded modules.

## Windows reference and runtime package

`scripts/windows-runtime-build.sh` uses the pinned WinLibs 15.2 posix-SEH
toolchain, whose archive has an explicit SHA-256. It builds the 21 real HLE
targets, the relinker/NID patcher, the smoke demo and CPU/exception guests.
`scripts/package-runtime.py` accepts unpatched HLE outputs, patches a fresh copy,
walks native imports/forwarders recursively and resolves every guest NID before
writing the package. Test-only ELF linker stubs cannot be packaged as PRX files.
The original demo includes `app0/sce_sys/param.json`, validated against the
actual libkernel parser used by VideoOut. It calls normal libc `exit(0)` so
registered VideoOut/AGC shutdown callbacks run before Windows DLL teardown.
The reference still requires exit code 0; completed frame checks cannot hide
a teardown timeout.

The package contains `game.exe`, guest probes, `libs/`, `app0/`, hashes and
`game-profile.cfg`. Import that config explicitly in Madeira's game settings;
placing a config beside the executable does not apply it automatically.

Windows CI separates building from execution. The reference runner verifies
hashes, imposes process timeouts and requires individual JSON result stages.
Absent hardware Vulkan is an explicit graphics `not_run`; once a GPU is present,
a failed demo is a failure. These logs prove only native Windows execution.

## Local evidence, 2026-10-06

- The native host relinker and NID patcher build; all 17 existing relinker tests
  pass. Platform-dependent guest execution remains excluded on macOS.
- Both demo profiles and CPU/exception ELF fixtures compile and relink. The
  exception PE preserves its actual unwind metadata and RTTI import relocation.
- Seven compiler-produced PE/parser/NID packaging checks pass. Missing real HLE
  libraries correctly prevent creation of a runtime package.
- All four patch stacks apply and reverse on pristine pinned source. Static
  dispatch, Binder ABI, extension forwarding, surface lifetime and accepted
  presentation-count tests pass with controlled test drivers.
- The full iPhoneOS app now links successfully with real FEX,
  `ntdll`, `win32u`, `winevulkan`, wineserver, Rust pairing, font, crypto and
  media archives plus pinned MoltenVK and the real JIT helper. Its three
  embedded ARM64EC DLLs pass SHA-256 and CHPE code-map verification. The pinned
  LLVM-MinGW toolchain needs ThinLTO disabled for the ARM64EC FEX module;
  a small reproducer confirmed that link incompatibility.
- After developer-account setup, both the app and real JIT helper are signed.
  Strict recursive signature verification passes; the main app's signature and
  provisioning profile both include Extended Virtual Addressing and Increased
  Memory Limit. The full app is installed and its library has started on the
  physical iPad. The first external JIT attempt found no VPN route. After the
  user enabled LocalDevVPN, the route was reachable, but StikDebug 3.1.13 still
  initially did not establish a verified live debugger. After the approved
  pairing migration, both external StikDebug and the built-in helper completed
  a standalone x64 AVX2 probe. The built-in run logged its own helper PID,
  successful completion, six exact vector comparisons and guest exit code 0.
  See [Wine/FEX CPU evidence](evidence/ipad-m2-wine-fex-cpu.json). This exercises
  Windows PE CPU execution; relinked PS5 HLE and graphics remain separate gates.
- The separate native probe was built, signed, installed and run on the physical
  iPad Air 13-inch M2 (`iPad14,10`, iPadOS 27.0.1 / 24A446). Actual device
  creation, BDA/8-bit/int64 shader readback and BC1 texture sampling all passed.
  App and scene were active at both ends and no interruption was recorded.
  The first launch exposed a UIKit notification-order race; its inactive-start
  report is rejected by the verifier. The corrected run is preserved in
  [raw device evidence](evidence/ipad-m2-native-gpu.jsonl) and its
  [strict qualification](evidence/ipad-m2-native-gpu-qualification.json).
  This proves native offscreen GPU execution only; it does not prove Wine/FEX,
  a visible swapchain, or the game acceptance below.
- The matching Windows PE and SPIR-V bundle now passes the same GPU readbacks
  through the actual iOS Wine/FEX/winevulkan/MoltenVK path. An older staged
  shader initially failed the stricter reference; that mixed bundle is rejected.
  See [Wine/FEX GPU evidence](evidence/ipad-m2-wine-fex-gpu.json).
- The Win32 swapchain probe accepted 600 presents in 10.43 seconds and a
  foreground screenshot shows its coloured output. This is a short clear/present
  smoke test, not the SDL/RDNA scene or a gameplay FPS result.
  See [presentation evidence](evidence/ipad-m2-wine-fex-present.json).
- Exact placeholder reservation failed at both 8 GiB and an
  explicitly separate 64 GiB candidate. Corrected error sampling at 64 GiB
  reports Windows error 487 and native KERN_NO_SPACE; the original 87 was a
  stale error due to C++ argument evaluation order. Further mapping stages
  were not reached in those failed candidates.
  See [memory failures](evidence/ipad-m2-wine-fex-memory.json).
- The exact CPU and exception ELF/PE bytes from the successful Windows CI
  package now also pass on iPad with the real PRX closure: SysV stack/register
  calls, two threads, main/worker TLS isolation, atomics, pthread key destructors,
  initialized libc heap, typed nested rethrow and unwind destructors. The runs
  use the configured 464–468 GiB arena with 256 MiB lazy chunks and exit 0.
  See [relinked HLE evidence](evidence/ipad-m2-relinked-hle-cpu-exceptions.json).
  RDNA/SDL, scene interaction and full acceptance still need their own tests.
- The local Homebrew GCC 16 HLE cross-build fails at libc's SjLj unwind symbols.
  No complete HLE package or Windows guest execution has been claimed from it.
  Use the pinned Windows toolchain for that build.

Runtime diagnostics capture the process's real native Mach regions and resident,
virtual and compressed memory, with existing JIT/debugger flags. Hooks record
app lifecycle, running Wine and the first new Vulkan present. The native map
is a bounded, live, non-atomic observation; it cannot prove a hole safe for a
fixed guest allocation. On the actual device, all four pre-Wine walks found
the entire 8–12 GiB candidate window already covered by 102 native leaf
regions. This includes executable/readable host mappings as well as existing
no-access regions; none is an unmapped hole. The proposed default is therefore
**not a usable arena on this observed device launch**. Preserve that collision
as a failing diagnostic and qualify another range against both native mappings
and Wine's own reserved-area bookkeeping before changing the runtime profile.
See [app-start and address-map evidence](evidence/ipad-m2-runtime-start.json).
The corrected 64 GiB attempt also hits a native no-access host reservation
covering 64–448 GiB. It fails with KERN_NO_SPACE / Windows error 487 and never
overwrites that reservation. The bounded native diagnostic preserves both codes.

A separate candidate at **464–468 GiB** now passes a complete 4 GiB reservation,
16 KiB private/shared replacements, write watch, aliases, protection changes,
coalescing and reuse through Wine/FEX. This reserves virtual addresses and does
not allocate 4 GiB of RAM. The real GuestArena implementation also passes its
controlled 1 MiB / 64 KiB-chunk startup collision, late collision, transaction
rollback, reuse and overflow tests. See [mapping evidence](evidence/ipad-m2-464gib-memory.json)
and [allocator evidence](evidence/ipad-m2-464gib-allocator.json). The initial
runtime profile now uses this candidate with its existing 4 GiB size and
256 MiB lazy chunks; existing user configuration is not silently replaced.
This is qualified for the observed M2 runs only. A live map and successful
probe do not guarantee that another launch, iPad or a game's fixed addresses
will fit; allocation collisions must still fail safely.

Additional local evidence lives under ignored `build/logs/`,
`build/ios-runtime/logs/` and the fixture directories. The signed probe,
installation/launch receipts, final executable hashes and screenshot are in
`build/ipad-probe/`. Its process was closed and the shared-device reservation
released after the test. The standalone CPU/GPU probes and relinked CPU/exception HLE tests pass; the
RDNA scene and full iPad acceptance below remain open. Draft PR #3 runs the prepared CI.
The corrected Windows CI at `a7a9950` builds the full real HLE closure and
passes relinked CPU callbacks/threads/TLS, initialized libc heap and nested
exceptions/destructors, plus standalone AVX2/memory/allocator probes. See
[Windows reference](evidence/windows-reference-a7a9950.json). Graphics is
explicitly `not_run` because the hosted Windows runner lacks a Vulkan driver.
The physical iPad graphics demo still needs that reference and its own run.
A private UM790 Pro run with the AMD Radeon 780M passed the CPU, exception,
AVX2, mapping and allocator checks. After adding the missing original demo
metadata, RDNA readback and VideoOut acknowledged all 120 frames, but the
process timed out during exit. That run remains a **failure**; the normal libc
exit fix passed a fresh reference run with exit code 0 and no timeout.
All 120 readback and presentation acknowledgments passed again. Input and
foreground visibility were not verified. See the [passing short reference](evidence/windows-um790-demo-exit-fixed.json). See [UM790 diagnostic](evidence/windows-um790-demo-exit-timeout.json).
Linux checks, ARM64 Wine/FEX and macOS GPU execution passed. Initial iOS jobs
exposed missing modern Bison and the separate Xcode Metal compiler component;
the workflow now explicitly installs both.

The initial Xcode **"No Accounts"** signing failure was resolved by signing in
to the developer account and generating the app's explicit capability profile.
The signed export is `build/ios-runtime/AnyPS5-iPad-SIGNED.ipa`, with a separate
archive/component hash manifest and `signing-verification.json`. The earlier
unsigned export remains separately labelled. iOS signing does not embed the
macOS `allow-jit` entitlement; a working debugger and actual JIT execution are
still required and cannot be inferred from successful signing.

## Physical iPad RDNA smoke and private-game reference

The M2 ran the corrected Windows demo PE and its unchanged real PRX closure
with built-in JIT. The scene is visible in iPad screenshots, all 120 per-frame
GPU XOR comparisons and flip acknowledgments completed, and Wine reported
exit code 0. This is a short 320×192 diagnostic, not full acceptance.

The loop measured 18.569 seconds for 120 frames (6.462 per second), including
CPU draw, full GPU readback and presentation acknowledgment. These are guest
loop timings, not display timestamps. Input was not observed. The neutral
scene checksum at frame 0 matches Windows; frame 60 differs (Windows
3237446373, iPad 2612983613). Per-frame GPU/input comparisons alone do not
validate the CPU-rendered scene. A new neutral-input checksum guard and
position samples reproduce this divergence as a failure on M2. Paddle and
ball simulation coordinates match the neutral scene. Before/after-flip checks
and host analysis of both pixel snapshots confirm the difference already
exists in the CPU source buffer: the ball remains drawn at its first-frame
position, differing in exactly 72 pixels from the frame-60 model. GPU output
still matches every source pixel XOR 1. This baseline failed correct scene
execution. Failure paths now
use normal libc shutdown too, and retain original-demo pixel snapshots.
See [M2 RDNA diagnostic](evidence/ipad-m2-rdna-smoke.json).

The first private Dreaming Sarah Windows diagnostic loaded its libraries and
created an AMD hardware Vulkan swapchain, but displayed black and reported
an uncaught parse error. The preparer had incorrectly excluded the dumped
`~INDEX` VFS asset. Packaging now retains it with an immutable-source hash
check and a synthetic regression test. The game binaries and libraries are
unchanged in the repaired package. The repaired native Windows run reaches the animated title and readable
main menu without an unhandled exception. Gameplay, audio and save/load are
still unverified. See [Windows menu evidence](evidence/windows-um790-dreaming-sarah-menu.json).

The same repaired package was verified on iPad and launched with successful
built-in JIT. The foreground game surface stays black. Shared guest pages
initially mapped RWX consume all 4,096 anonymous JIT aliases and overflow the
pool ledger before their requested RW rights are applied. The initial change to SDK mapping rights was rejected after the real device probe
found that it prevented a later read-only → read-write transition. The SDK's
original maximum access rights are retained. Instead, the Madeira patch
removes only native EXEC from fully mapped non-native views inside the explicit
PS5 arena. Wine's logical rights, FEX notifications and write tracking remain;
ARM64EC code, images, system views, uncommitted placeholders, holes and JIT
pool ranges are excluded. Committed replacement views retain the placeholder
flag for later unmapping; they must not be mistaken for empty reservations.
A bounded on-device classifier confirmed this flag on actual SDK shared pages. Compiled tests exercise the actual selector with controlled mappings,
boundary/overflow cases, late profile export and native-code exclusions.
Builds 5/6 pass the actual SDK alias/tracking tests but still route shared pages
to the native JIT pool and reproduce the demo scene failure. Build 7 corrects
the committed-placeholder classification. Its physical-iPad SDK probe passes
RW, RO and NOACCESS mappings, later write permission, shared aliases, write
tracking and placeholder restoration, with clean guest exit and no arena JIT
alias. The byte-identical diagnostic demo now matches Windows at frames 0/60,
passes all 120 GPU comparisons and exits 0. Its short loop rate is 28.233/s;
the screenshots occurred after guest exit, so this run alone does not prove
foreground scene visibility or display FPS. See [SDK mapping proof](evidence/ipad-m2-sdk-shared-mapping.json)
and [corrected RDNA result](evidence/ipad-m2-rdna-shared-mapping-fixed.json).

Dreaming Sarah Build 7 also avoids the former JIT alias exhaustion, but
remains black and recreates 5,222 swapchains during its bounded startup.
See [post-memory-fix startup](evidence/ipad-m2-dreaming-sarah-shared-mapping-fixed.json).
The iOS Vulkan adapter now negotiates advertised surface/device maintenance,
queries and enables the maintenance feature, and requests supported stretch
scaling for the HWND inside the iPad layer. It preserves application chains
and propagates capability errors; it does not suppress SUBOPTIMAL/OUT_OF_DATE.
Compiled adapter tests cover unsupported features/scaling and host errors.
Build 8 is built, signed and installed. Its longer original 320×192 demo is
visible in a fresh foreground screenshot, passes all 900 GPU comparisons and
flip acknowledgments and exits 0. The recovered log measures 29.530 guest loops/s;
this includes GPU readback and is not presented-frame FPS or ten-minute acceptance.
The initial runner timed out because the two newest archived logs were hard-linked.
AFC refused opening those files; replacing only the redundant archive links with
byte-identical independent copies recovered both full logs without losing data.
The native archive patch now keeps the active log single-linked, snapshots each
session at completion and seals crash logs before the next launch rotates them.
Compiled Foundation tests verify byte preservation, an already-open writer,
crash/relaunch, rotation, independent links and unsafe-name rejection. The app
also loads the Xbox touch preset for enabled games with an empty profile and no
explicit layout; existing controls and named layouts retain their settings.
Signed installed Build 9 repeats 900 GPU comparisons and exits 0. Its active
log and completed named archive both export directly with one filesystem link.
A fresh foreground screenshot shows the scene and Xbox controls; the saved game
profile contains 18 controls. Actual touch input remains unverified. Two independent Build 9 demo restarts
pass; a third attempt fails JIT pool placement before Wine. Three-successful-
restart acceptance is still open. See [restart evidence](evidence/ipad-m2-build9-restarts.json). The original
runner failed parsing an interleaved diagnostic line; a separate bounded collection
verified the completion, archive and own-process cleanup. See [log and layout proof](evidence/ipad-m2-independent-logs-touch-layout.json).

Dreaming Sarah still remains black in fresh 10/30/60-second screenshots. The
recovered Build 8 log confirms JIT, enabled maintenance/stretch scaling and exactly
one swapchain creation. Draw validation rejects vertex GroupNonUniform capability
61 because M2/MoltenVK does not support subgroups in that stage. A private IR capture
shows fully set export masks retaining unnecessary LaneId queries. The new compiler
patch proves masked constant shifts invariant before folding them; dead-code removal
then removes those lane queries and their subgroup requirement. Compiled tests exercise
20 real IR/pass cases including genuinely varying masks, unbounded shifts and invalid
bounds. Native replay of six private on-device requests produces three vertex
shaders without subgroup capabilities; genuine fragment ballot requirements
remain. See [compiler proof](evidence/ipad-m2-vertex-mask-compiler.json).
A Build 10 comparison now tests the old HLE and a package changing only the
graphics runtime. Both start Wine/JIT and Vulkan. The updated driver reports no
vertex subgroup rejection, but fails fragment SPIR-V-to-MSL conversion on
`PerVertexKHR`; a later repeatedly redelivered native memory fault terminates
the process. Both runs show no game scene. Earlier Build 9 attempts stalled
inside the FEX allocator before GPU initialization, and an old-HLE control
failed JIT pool placement before Wine; their root causes remain open. Build
10 uses Foundation file copies instead of loading entire log archives into
heap buffers. Its build, signing and installation are confirmed; causation
between archive allocation and the earlier failures has not been established.
See [single-driver comparison](evidence/ipad-m2-dreaming-sarah-shader-mask-device.json).
Validation stays enabled.
See [Build 8 results and limits](evidence/ipad-m2-vulkan-scaling-visibility.json).

The optional `APS5_FIXED_FUNCTION_INTERPOLATION=1` selects the existing native
interpolated-attribute lowering instead of emitting unsupported per-vertex
fragment inputs. The production translator now verifies complete unmodified
center P1/P2 pairs and rejects partial results, raw barycentric arithmetic,
custom/sample/centroid interpolation and unsupported control flow. Thirty
compiled guard cases pass. All six captured shaders replay through this guard
and pinned SPIRV-Cross, and Apple's actual Metal compiler accepts all six MSL
outputs. See [interpolation compiler proof](evidence/ipad-m2-fixed-function-interpolation-compiler.json).
The repaired Windows CI driver now runs on the installed Build 10. The opt-in
is selected, and the prior PerVertexKHR error is absent. The user reports the
first logo and then sound over a black screen; inspected 10/30-second captures
show black game output. A new draw is rejected at pc 84, and repeated native
write faults still terminate the process. A short control with the original
barycentric path shows no logo and restores PerVertexKHR errors. The opt-in
is restored afterwards. See [device diagnostic](evidence/ipad-m2-dreaming-sarah-interpolation-device.json).
Patch 0008 extends the existing opt-in APS5_DUMP_SHADERS to draw requests
before source analysis. A fresh Build 10 run changes only the graphics driver,
records the first logo independently and captures the previously rejected draw.
All game assets, guest modules, PE and ~INDEX remain unchanged. The 40-second
observation still shows black output after the logo and a pc-84 guard rejection;
its shorter duration cannot establish that the later repeated memory fault is
fixed.

The newly captured fragment consumes the center I register in place after other
P1 instructions and schedules independent vector ALU before matching P2s.
The guard now permits that sequence only while J stays live and no intervening
ALU reads or writes a partial result, changes EXEC, or reads raw I/J. Memory,
compare, unknown and control-flow instructions between pairs still reject.
Forty-three compiled production-guard cases pass. Rebuilding the production
recompiler locally changes this capture from rejection to success; all twelve
local requests pass pinned SPIRV-Cross and Apple's Metal compiler. The extended guard has not yet run on
the iPad. See [scheduled-pair diagnosis](evidence/ipad-m2-interpolation-scheduled-pairs.json).

The exception-delivery terminal also contains a separate accumulation defect:
its PC/address hash counts recurring visits even when that thread has delivered
other faults in between. The baseline log shows intervening fault pairs and
successful changed stores before its 2000-count termination. Patch 0016 counts
consecutive identical deliveries per exact Mach-thread identity instead, retaining
the 256 warning, 2000 terminal and all memory protection/write tracking. The actual
counter passes host checks and the iPhoneOS signal handler compiles. Device
validation is pending; this does not establish that every observed fault is valid.
See [fault-counter diagnosis](evidence/ipad-m2-consecutive-fault-counter.json).

Private shader requests, assets and complete game logs remain outside Git and CI.

## Acceptance and current limits

A bounded Build 10 run uses a 1280×720 original guest buffer and completes
120 full-pixel GPU comparisons with Wine exit 0. Fresh foreground screenshots
show the paddle scene and controls. The existing SDL window scales presentation
into a 768×432 swapchain. The guest loop averages 23.130 iterations/s; one Metal
HUD screenshot reports 26.73 FPS, 37.42 ms frame interval and 0.86 ms GPU time.
That isolated HUD sample is not an average over ten minutes. Touch input is not
observed. Independent Windows scene checks at this resolution, full-resolution
presentation and the complete acceptance run remain open. See the
[720p diagnostic](evidence/ipad-m2-demo-720-diagnostic.json).
A follow-up profile uses a 2134×1200 synthetic desktop so the existing
60-percent SDL window creates an actual 1280×720 swapchain. The same SDL/Wine/
MoltenVK path remains in use. All 180 GPU comparisons pass and Wine exits 0;
a fresh foreground screenshot and Metal HUD confirm the full swapchain size.
The short guest loop averages 26.379/s, with 5.702 of 6.823 seconds attributed
to flip submission/acknowledgment. This does not establish a ten-minute displayed
FPS average. See [full 720p surface proof](evidence/ipad-m2-demo-720-full-surface.json).

A subsequent run completes 600.002 guest seconds on the same actual 1280×720
surface with 32,252 full-pixel comparisons, zero guest-reported failures and Wine
exit 0. Scripted real UI taps reach scePad; further input changes occur while
the user watches the device. Nine minute-spaced foreground screenshots show
stable 28.20 MB Metal and 1.65 GB app memory at HUD precision. The guest loop
averages 53.753/s; this is not displayed FPS. A/B controls obscure numerical
frame metrics and no continuous Metal HUD intervals are collected. The initial
runner cleanup check fails because an own helper remains; a separate fresh
path-verified cleanup confirms no own processes. Background/resume and the
sustained displayed-rate gate remain open. See
[ten-minute execution evidence](evidence/ipad-m2-demo-720-ten-minute.json).

The original guest now reports aggregate timings for drawing/input, dispatch
and GPU waiting, full readback/checkpoints, flip acknowledgment and post-flip
pacing, plus poll sleep counts. These measurements retain all pixel comparisons
and existing waits; they diagnose the bottleneck without claiming display FPS.

`scripts/build-demo.sh --profile interactive` builds a separate 320×192 input
diagnostic lasting up to 600 seconds (or Circle to exit), with the same real
RDNA dispatch and full-pixel comparison. It emits no acceptance-pass events;
it gives a person time to test controls before the short smoke stops. All three
profiles compile and relink locally. The interactive package is installed and
has a bounded physical iPad input run; it is not a full acceptance run.

In a manual test the user reports a moving ball but no response to left/right
or A. This establishes visible ongoing execution, not working input. The earlier
device log enables touch/XInput but contains no SDL controller-open message.
One diagnostic is `env.MADEIRA_PAD_EARLY_SLOT = 1` in **All settings** followed by
a full app restart: it reserves the existing virtual player before SDL initializes.
The native reader uses global config; this switch in a game's config is currently
too late for the reservation. After reconnection, the global switch is enabled
and the device log confirms both an early player reservation and an opened SDL
Xbox 360 controller. The existing signed XCUITest helper injects four right,
four left and two A taps into the actual touch controls. The guest sees the
corresponding `scePad` button states, moves the paddle and serves twice. Zero
button states follow the taps. At least 1,861 full-pixel GPU comparisons succeed
before the bounded runner stops the app. This is physical input proof; sticks,
background/resume and ten-minute performance need separate checks. A second
fresh launch repeats the ten taps, then taps B/Circle: the guest records 21
input changes, two serves and 310 successful GPU comparisons, closes VideoOut
and exits Wine with code 0. Its timing probe attributes 9.562 of 10.817 seconds
to flip submission/acknowledgment, compared with 0.731 seconds for compute
submission/wait and 0.024 seconds for readback/checkpoints. This narrows the
performance investigation to the flip path without establishing display FPS.
See [touch input proof](evidence/ipad-m2-touch-input-early-slot.json).
Three fresh Build 10 launches of the same interactive package now initialize
JIT, open SDL's virtual controller and deliver touch input. The first is stopped
externally after its bounded observation; the next two stop through B/Circle
with Wine exit 0. The third verifies 315 full-pixel GPU comparisons and 33 input
changes. These three startup successes supersede the earlier Build 9 restart
limit for this configuration; full-resolution duration and background recovery
remain unverified.

The rejected initial guest-arena candidate was base `0x200000000`, size `0x100000000`,
with lazy 256-MiB reservations. A 512-GiB entitlement does not make the entire
space usable: Madeira has GPU, JIT and runtime reservations. Confirm the exact
map and placeholder/alias semantics on the device before relying on it.

The acceptance demo is 1280×720 for 600 seconds, targeting 60 FPS and at least
30 FPS on average. A short low-resolution smoke profile is a different run.
Record real presented frame times, device/OS, app foreground state, thermal
state, memory and loaded runtime hashes. Require three app launches, touch
press/release, background/resume and correct RDNA readback. A build, fake ICD
test, headless result or cumulative present counter is not visible gameplay.

Before installation or launch, claim the shared iPad through its existing
coordination record. Use the isolated app IDs and preserve other apps and data.
For bounded command phases, use the [fail-closed lease wrapper](IPAD-LEASE.md),
which starts no child process after a rejected claim and preserves newer leases.
Verify built/signed, installed, launched, JIT-enabled and benchmarked states
separately. No physical-iPad gameplay result is asserted by this source change.

Dreaming Sarah can now be prepared from a local decrypted dump with the
[private game preparer](PRIVATE-GAME-PACKAGING.md). It inventories the exact
ELFs and assets, retains bundled modules, builds and validates the real HLE
closure, and never uploads game data. Establish the same version on Windows
before comparing menu/gameplay/audio/saves on iPad. Packaging is not execution;
the original demo is not commercial-title compatibility evidence.
