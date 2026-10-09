# Runtime implementation and verification

The historical foundation baseline for this implementation log is main
`20bb9809e5f19624c7ae27fd129c32ebfad055e8` and its pinned upstream commits.
Selected build repairs from PR #2 were included; later upstream refreshes are
recorded in [UPSTREAM-REFRESH.md](UPSTREAM-REFRESH.md). PR #3 merged the runtime
work into main. This document distinguishes
source work, build artifacts, host execution and physical-device acceptance.

## Current device result

As of 2026-10-09, Native54 and Solitaire Source65 are installed. The game
selection menu renders with 18 touch controls and an FPS-only overlay. The
two-minute native component check retains JIT warming, footprint sampling and
passing heap checks while suppressing the statistical memory census. AudioOut
pacing changes the captured repeating-packet pattern; listening quality and
speed remain unaccepted. XCTest currently times out enabling automation mode,
so this profile has no new touch-gameplay qualification. See the [current
milestone](MILESTONES.md#current-installed-milestone-2026-10-09), [AudioOut checkpoint](AUDIOOUT-FRAME-PACING.md)
and [native device evidence](evidence/quiet-runtime-diagnostics-20261009.json).

## Earlier Dreaming Sarah gameplay observation

On 2026-10-06 at about 16:11 UTC (23:11 WIB), buberlo recorded the iPad
screen for about two minutes. The device was an iPad Air 13-inch M2 running
iPadOS 27.0.1. The AnyPS5 iPad app library entry
`Dreaming Sarah (PS5) 01.000.000, 64-bit, Vulkan, 5.26 GB` launched, JIT came
up through the StikDebug-style flow, and the session did not crash.

Observed, in order: the Ratalaika Games publisher logo with audio, a working
title screen and Options menu, New game, the Asteristic studio intro with
music, then about 60 seconds of gameplay. The player character was visible,
walked and jumped through the forest, music continued, and a dialogue box
rendered while she spoke with an NPC. Control was the on-screen touch
controller. Displayed FPS was not measured. The app build, model identifier,
OS build, and private-media hashes were not recorded for this observation.

See [gameplay recording](evidence/ipad-m2-dreaming-sarah-gameplay-recording.json).

This is a basic playability observation of the PS5 build's own relinked
binary. Displayed FPS, long sessions, full background recovery, save/load,
formal audio acceptance, other iPad models, and a public IPA remain open.
Build 12's six-minute forest traversal and Build 13's brief Vulkan inactivity
drain are the instrumented results later in this file.

Headings below this note are the chronological log. Paragraphs about a black
image or a missing character describe the build named in that paragraph.

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
`com.buberlo.anyps5ipad.probe`; its report is in Documents/gpu-probe.jsonl.
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

## Solitaire diagnostic checkpoint, 2026-10-08

Build 21 repairs ARM64 atomic read-modify-write fault classification and passes
an independent protection-fault probe on the iPad. The installed Solitaire
package still has no qualified visible scene. Production
`sceKernelRaiseException` reports unsupported asynchronous delivery explicitly.

A separate private cooperative SIGUSR1 experiment advances through Unity GC
initialization. Its standalone contract passes on the iPad through Wine/FEX:
target-thread identity, readable stack context, preserved nonvolatile register,
and condition waits that keep their original completion/deadline. Using the
original Wine ARM64EC APC context does not fix the subsequent graphics-worker
null dereference. This experiment is not part of the production patch stack;
its results do not prove complete GC roots or general asynchronous signal support.

The same experimental HLE package was hash-verified on a native Windows 11
UM790 Pro with the Radeon 780M selected by Vulkan. A 45-second fixed-interpolation
run and a 90-second normal-interpolation run stayed active without the observed
iPad CPU access violation. The inspected Windows game window was black.
Unsupported depth/stencil maintenance and multisampling/coverage operations
remain. The existing fixed-function interpolation proof also rejects a later
shader; the normal iPad path reports unsupported `PerVertexKHR` MSL translation.
No graphics validation was disabled. These are startup diagnostics, not gameplay
or display-FPS acceptance. Private shader requests and raw logs stay outside Git.

Two bounded graphics repairs now retain the normal Vulkan draw path. A
single-sample `STENCIL_CLEAR_ENABLE` is admitted only when its normal stencil
state already replaces every covered sample with `DB_STENCIL_CLEAR`: both active
faces must always pass, every operation must replace, write masks must be full,
depth/bounds tests must be disabled, the stencil plane must be writable, and
shader kill must be disabled. The initial guard admitted Z order 1,
`EARLY_Z_THEN_LATE_Z`; its earlier `LATE_Z` comment was incorrect. The follow-up
also admits actual `LATE_Z` (0), while retaining all no-test/no-kill and
coverage constraints. Re-Z modes stay rejected. This does not implement general
depth/stencil maintenance or MSAA.

The fixed-function interpolation proof also permits a plain read-only 2D
`IMAGE_SAMPLE` between P1 and P2 when its complete coordinate/result register
footprint is disjoint from every pending interpolation result. Raw I/J reads,
status returns, packed/NSA addressing, stores, atomics, incomplete pairs and
ambiguous effects remain rejected. Seventy guard cases pass, including
ASan/UBSan; all nine initially captured shader requests now replay successfully.
The later capture still rejects two different shaders at `pc=76` and `pc=108`.
Run the guard against an isolated patched checkout with
`python3 tools/checks/test_fixed_function_interpolation.py --source <AnyPS5>`.

A 45-second stencil-only Windows comparison and a 60-second comparison including
the interpolation repair both remained visually black. The original non-graphics
guest binaries were unchanged and each new package verified 137 guest files.
The latter run advanced to additional unsupported clear, MSAA and interpolation
states. On the iPad, the bounded private signal experiment reached JIT and the
ELF entry but again faulted in the graphics worker at `game.exe+0x1426827`.
The production kernel and updated graphics runtime were restored with 138 files
verified; the library, game assets and global configuration were preserved.
The restored package was not launched again. See the
[bounded comparison record](evidence/solitaire-runtime-checkpoint-build21.json).

The next bounded repair proves a specific three-block alpha-discard diamond:
entry, a surviving continuation, and an exit that only clears EXEC, emits an
empty final export and ends. Original center I/J registers must remain unchanged
through the branch; complete P1/P2 pairs cannot cross it. Disjoint read-only
scalar descriptor loads, wait counters and NOPs may occur between pairs. Scalar
write ranges exclude M0 and EXEC, and VCC writes may not exceed its two words.
Other merges, loops, stores, barriers and ambiguous effects remain rejected.
Ninety-eight portable guard cases pass, also with ASan/UBSan. The two earlier
interpolation rejections disappear; 14 of 15 old captures compile, with the
remaining capture failing resource-descriptor evaluation rather than the guard.

The Z-order correction follows Mesa's
[GFX10.3 register enumeration](https://chromium.googlesource.com/chromiumos/third_party/mesa/+/a11d1bd247df18057ecb9945e01bf39ff85d8d9d/src/amd/registers/gfx103.json):
0 is late, 1 is early-then-late, and 2/3 are re-Z modes.
Depth/stencil state contracts and all 12 HLE host contracts pass. All 26 patches
apply to the pinned source, match the isolated build checkout, and reverse to
the original bytes. These checks do not establish GPU correctness.

A driver-only native Windows comparison now visibly reaches the Solitaire
start image and the Yukon/Canfield/Golf selection menu. Unsupported MSAA draws
remain in its error log; neither full rendering fidelity nor gameplay is
qualified. The same private signal/renderer comparison on iPad reaches JIT and
the ELF entry, then faults in `UnityGfxDeviceWorker` at `game.exe+0x1461222`.
The production kernel and new renderer were restored, all 138 files verified,
and global settings, library and game assets preserved. The signal experiment
remains private and absent from the installed production package.
See the [follow-up checkpoint](evidence/solitaire-discard-continuation-checkpoint.json).

Build 22 adds an opt-in, default-quiet FEX diagnostic:
`MADEIRA_TRACE_FEX_CONFIG=1` records the resolved scalar/vector/REP and half-barrier
TSO options after configuration layers have loaded. Requested environment values
alone are not evidence of the resolved mode. The iOS hardware-TSO hook returns
false, retaining software emulation; no ordering defaults are changed.

A stronger independent signal test binds distinct values to R12–R15 while the
actual target thread waits in native alertable `SleepEx`. Both callbacks match
all four values in the interrupted PS5 context on the host and iPad (eight
checks). Original wait completion/deadline contracts also pass. This excludes
a generic mismatch of these nonvolatile registers in that tested path, but does
not prove every context field, Unity GC root completeness or arbitrary async
delivery. The signal implementation remains a private diagnostic prototype.

Two Build 22 game runs use the same diagnostic package and resolved scalar TSO
and half-barrier settings. The baseline resolves vector/REP ordering to false;
the strict run resolves both to true. Both reach JIT and the ELF entry, then
fault at `game.exe+0x1426827` reading address `0x20` in `UnityGfxDeviceWorker`.
Stricter ordering did not fix that observed failure. The regular production
package is restored with all 138 files hash-verified. Library and global config
remain byte-identical, and the device lease is released. Solitaire is installed;
visible iPad gameplay remains unqualified. See the
[context and ordering checkpoint](evidence/solitaire-build22-context-and-ordering.json).

A follow-up read-only private trace identifies the actual GC callback inside
`Il2cppUserAssemblies.prx.guest.prx`. Instruction analysis shows that it copies
only the context prefix `[0,0xd8)` and records the separately supplied interrupted
RSP; this callback does not load the FPU area or later context fields. All
observed interrupted stack pointers lie within the registered thread stacks.
No signal delivery to the graphics worker appears in this trace. These facts
narrow the callback hypothesis, rather than qualifying full GC correctness.

The traced game run later reads address `0x2` at `game.exe+0x1426ac4`. The stack
argument array used by its virtual method calls contains small integer values
`2` and `3` where pointers are consumed. The producer of those values is not yet
identified. FEX captures the earlier virtual-call target in frontend and
post-pass IR, but these dumps do not establish machine-code correctness.
A separate native x86-64 Wine run on the M3 Pro loses the Metal device to a
command-buffer out-of-memory error before isolating the iPad null fault; it is
not a matching native Windows reference. Both runs are bounded and stopped.
The production package and source are restored; 138 installed files are verified.
See the [GC and pointer checkpoint](evidence/solitaire-build22-gc-pointer-checkpoint.json).

Build 23 adds a default-off live argument tracer, separate from compile-time
IR listings. Set `MADEIRA_TRACE_GUEST_ARGS=1` with an explicit
`MADEIRA_IRCAP_MODULE` and nonzero `MADEIRA_IRCAP_RVA`. It records RIP, RDI,
RSI, RDX, RCX, RBX and RSP at that instruction. Optional
`MADEIRA_TRACE_GUEST_ARGS_WORDS` reads up to eight words from each of RSI, RDX
and RCX; its default is zero. Only enable these reads for known valid guest
arrays. Runtime logging is capped at 4,096 records. Trace insertion changes
register allocation and execution timing; it must not be treated as a fix or
used during performance acceptance.

The reproducible Windows fixture in `tools/runtime-probes/guest_argument_probe.c`
uses an actual unaligned AVX pointer copy, a SysV call and native alertable wait.
On the iPad, ten traced calls preserve 210 pointers, the result checksum, and
R12–R15 sentinels. Obtain the `TraceAnchor` RVA from the compiled PE symbols;
do not assume a fixed compiler layout. Validate its complete raw log with
`python3 tools/runtime-probes/check_guest_argument_trace.py <local-log>`.
The verifier ignores IR string listings and rejects incomplete runtime records.
These are synthetic tracer checks, not relinked-game or GC acceptance.

One traced game call now contains null slots and high-address resource pointers
before the virtual dispatch, without the previously observed small integer
values. The run proceeds further and faults at `game.exe+0x1461222`, reading
`0x30`. The untraced control still reads `0x2` at
`game.exe+0x1426ac4`. Its app-file service timed out; a temporary, private native
recovery path retrieved that prior fault through the launch console, restored
the regular package, and checked all 138 file hashes inside the app. It restored
the original global configuration and preserved the current library, including
launch metadata. The recovery source and payload are removed afterward. The
normal app is reinstalled without the private recovery code or payload. External
readback now verifies all 138 production files, the restored global configuration
and the preserved library. The complete untraced log is recovered as well. This
single traced/untraced pair does not distinguish register allocation effects
from timing or other causes. Tracing is not a runtime fix. No gameplay is qualified.
See the [live argument checkpoint](evidence/solitaire-build23-live-arguments.json).

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
  Windows PE CPU execution. Relinked PS5 HLE and graphics were still separate
  gates at this stage.
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
  At this stage RDNA/SDL, scene interaction and full acceptance still needed
  their own tests.
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
released after the test. At this point in the log the standalone CPU/GPU probes and relinked
CPU/exception HLE tests had passed, and the RDNA scene was still open. PR #3
was still a draft running the prepared CI; it has since merged. Device results
are in the current device result above and in the sections below.
The corrected Windows CI at `a7a9950` builds the full real HLE closure and
passes relinked CPU callbacks/threads/TLS, initialized libc heap and nested
exceptions/destructors, plus standalone AVX2/memory/allocator probes. See
[Windows reference](evidence/windows-reference-a7a9950.json). Graphics is
explicitly `not_run` because the hosted Windows runner lacks a Vulkan driver.
At this point in the log the physical iPad graphics demo had not yet had its own run.
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
main menu without an unhandled exception. On that Windows menu run, gameplay,
audio and save/load were not verified. See [Windows menu evidence](evidence/windows-um790-dreaming-sarah-menu.json).

The same repaired package was verified on iPad and launched with successful
built-in JIT. The foreground game surface stayed black on that launch. Shared guest pages
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

Dreaming Sarah Build 7 also avoided the former JIT alias exhaustion, but
remained black and recreated 5,222 swapchains during its bounded startup.
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
profile contains 18 controls. Actual touch input was still unverified on that Build 9 run. Two independent Build 9 demo restarts
pass; a third attempt fails JIT pool placement before Wine. Three-successful-
restart acceptance is still open. See [restart evidence](evidence/ipad-m2-build9-restarts.json). The original
runner failed parsing an interleaved diagnostic line; a separate bounded collection
verified the completion, archive and own-process cleanup. See [log and layout proof](evidence/ipad-m2-independent-logs-touch-layout.json).

At that stage Dreaming Sarah remained black in fresh 10/30/60-second screenshots. The
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
The repaired Windows CI driver was installed on Build 10. The opt-in
was selected, and the prior PerVertexKHR error was absent. In that diagnostic
the user reported the first logo and then sound over a black screen; inspected
10/30-second captures showed black game output. A new draw was rejected at
pc 84, and repeated native
write faults still terminated the process. A short control with the original
barycentric path showed no logo and restored PerVertexKHR errors. The opt-in
was restored afterwards. See [device diagnostic](evidence/ipad-m2-dreaming-sarah-interpolation-device.json).
Patch 0008 extends the existing opt-in APS5_DUMP_SHADERS to draw requests
before source analysis. A fresh Build 10 run changes only the graphics driver,
records the first logo independently and captures the previously rejected draw.
All game assets, guest modules, PE and ~INDEX remain unchanged. The 40-second
observation still showed black output after the logo and a pc-84 guard rejection;
its shorter duration cannot establish that the later repeated memory fault is
fixed.

The newly captured fragment consumes the center I register in place after other
P1 instructions and schedules independent vector ALU before matching P2s.
The guard now permits that sequence only while J stays live and no intervening
ALU reads or writes a partial result, changes EXEC, or reads raw I/J. Memory,
compare, unknown and control-flow instructions between pairs still reject.
Forty-three compiled production-guard cases pass. Rebuilding the production
recompiler locally changes this capture from rejection to success; all twelve
local requests pass pinned SPIRV-Cross and Apple's Metal compiler. The extended
guard now executes on the iPad: an isolated driver update on
Build 10 reaches the visible New game / Continue / Options menu. The 91-second
observation has zero shader skips and zero PerVertexKHR errors, with successful
own-process cleanup. Old native fault-counter warnings remained on that menu
run; gameplay and save/load still needed their own tests. See
[main menu proof](evidence/ipad-m2-dreaming-sarah-main-menu.json) and
[scheduled-pair diagnosis](evidence/ipad-m2-interpolation-scheduled-pairs.json).

The exception-delivery terminal also contains a separate accumulation defect:
its PC/address hash counts recurring visits even when that thread has delivered
other faults in between. The baseline log shows intervening fault pairs and
successful changed stores before its 2000-count termination. Patch 0016 counts
consecutive identical deliveries per exact Mach-thread identity instead, retaining
the 256 warning, 2000 terminal and all memory protection/write tracking. The actual
counter passes host checks and the iPhoneOS signal handler compiles. Device
validation now confirms Build 11 is installed with byte-preserved embedded PE
farms and checked game/settings/library data. Touch starts New game and the
credits plus forest appear. A remaining consecutive native fault still reaches
the retained terminal, so this first counter repair alone does not resolve the memory issue.
See [fault-counter diagnosis](evidence/ipad-m2-consecutive-fault-counter.json).

The entry-scene capture adds one rejected fragment with three basic blocks:
its complete center interpolation occurs in the unconditional entry prefix,
and a texture result overwrites raw I/J before the first branch. The guard now
permits this precise shape, requires a valid entry starting at instruction zero
with no incoming edges, and rejects surviving raw inputs, partial pairs or later
interpolation. Fifty-three production cases pass. The earlier Build 11 scene lacked the
character. It predates the Build 12 rendering below and the 2026-10-06
gameplay recording. See
[entry scene diagnostic](evidence/ipad-m2-dreaming-sarah-entry-scene.json).

Build 12 completes the remaining two targeted repairs. The entry-prefix guard
is built in Windows CI run 37483428646 and executes on the iPad. All 21 private
captures replay through the production recompiler, pinned SPIRV-Cross and Apple's
iOS Metal compiler. A five-minute device observation renders Sarah and the forest
with zero skipped draws. A separate six-minute run disables shader-file capture
and uses actual held UI touches: D-pad right leaves the starting platform, analog
right reaches the pool, and analog left moves/faces left. Every UI action succeeds
and independent screenshots confirm movement. The HUD's Frame 0/nominal 30 FPS
is rejected as performance evidence. See
[player rendering](evidence/ipad-m2-dreaming-sarah-player.json) and
[held input](evidence/ipad-m2-dreaming-sarah-held-input.json).

Patch 0017 fixes legitimate repeated protection-handler repairs being counted
as one stuck loop. NtProtectVirtualMemory reports only a successful read-only to
writable transition; the signal handler checks that the previous fault belongs
to the calling Mach thread and repaired range and that its actual backing page
is writable. Only then does it retire that sequence. Memory tracking, warning 256
and terminal 2000 remain enabled. The same original AVX-store executable stops
at 2000 validated repairs on Build 11, then completes 5000 and Wine exit 0 on
Build 12. A deliberately unrepaired handler still reaches terminal 2000 without
a completed store. Host checks cover thread/range isolation and overflow bounds.
See [verified protection repair](evidence/ipad-m2-verified-write-repair.json).
The standalone source is `tools/runtime-probes/protected_write_probe.cpp`;
`scripts/build-runtime-probes.sh` builds its normal mode, and the Windows reference
runner executes it when present. The intentionally unrepaired diagnostic is not
part of the normal reference invocation.

The actual iPhoneOS signal and virtual objects are rebuilt; the other 35 native
archive members and three embedded PE farms are byte-preserved. Build 12 passes
strict recursive signing and is independently confirmed installed. After an
initial source-fingerprint mismatch, the actual pinned ARM64EC FEX and Wine PE
build targets run and record a fresh manifest. The three output hashes are
unchanged, and strict source/CHPE verification now passes. The historical
manifest and initial failed check are retained rather than rewritten.

Patch 0018 adds a native Vulkan lifecycle gate. UIKit closes admission at
willResignActive, waits for admitted calls to finish and drains each live device
before background GPU execution becomes prohibited. Queued submit/present/image
acquisition and wait calls park until didBecomeActive; forwarded Vulkan results
are preserved. Create/destroy maintain the native device registry; the ordinary
non-Vulkan app path has a weak no-op fallback. Host tests exercise actual dispatch,
parked calls, running-call quiescence, native errors and device cleanup. The actual
iPhoneOS object compiles with implicit declarations treated as errors.

Signed Build 13 changes only win32u's Vulkan archive member plus the app lifecycle
bridge. All three PE farms are byte-preserved and strict PE source/CHPE checks
pass after the actual build targets run. Installation readback confirms game PE,
manifest, index, settings and library preservation. Build 12 demonstrates the
background permission/device-loss failure. Build 13 drains two devices with
VK_SUCCESS and returns from a brief cycle without that error. The long-cycle UI
helper crashes and its state assertion disagrees with UIKit's lifecycle records;
that automation has not passed. Full background/performance acceptance remains
open. See [lifecycle checkpoint](evidence/ipad-m2-vulkan-lifecycle-build13.json),
[baseline failure](evidence/ipad-m2-vulkan-background-failure.json) and
[brief cycle](evidence/ipad-m2-vulkan-background-short.json).

The same driver/package manifest now runs on UM790 in a fresh, hash-verified
folder. The menu is independently visible and no skipped draw is logged. The
95-second observation's final capture is a white transition; this is not Windows
gameplay acceptance. Original manifests are not repaired in place. See
[matching-driver comparison](evidence/windows-um790-entry-prefix-menu.json).

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
separately. Bounded physical-iPad gameplay is recorded above, including the
2026-10-06 maintainer screen recording of library launch, menus, and about
60 seconds of touch-controlled play. Displayed FPS, long sessions, full
background recovery, save/load, formal audio acceptance, other iPads, and a
public IPA remain open.

Dreaming Sarah can now be prepared from a local decrypted dump with the
[private game preparer](PRIVATE-GAME-PACKAGING.md). It inventories the exact
ELFs and assets, retains bundled modules, builds and validates the real HLE
closure, and never uploads game data. Establish the same version on Windows
before comparing menu/gameplay/audio/saves on iPad. Packaging is not execution;
the original demo is not commercial-title compatibility evidence.


### FEX block and arithmetic checkpoint, Build 25

Build 24 added resolved `MULTIBLOCK` and `MAXINST` values to the existing
default-off `MADEIRA_TRACE_FEX_CONFIG=1` report. With the same private runtime
package and `MAXINST=5000`, disabling multiblock moves the graphics worker's
failure from an invalid small resource pointer to a later null resource. This
is an observed change in the failure, not proof that multiblock is its cause.

Disabling flag elimination initially fails before the game starts. The fault
report's saved guest RIP points to an earlier loader function. Reading the JIT
block tail identifies the actual block as `libwinpthread-1.dll+0xb302`, containing
`add rsp, -128`. Targeted frontend, post-RA and emitted-code capture proves that
an unused `Add` receives the logical immediate `-128` and emits the invalid
ARM64 word `0xfffe02e7`. `ALUOp` previously built a logical placeholder and
retagged it as arithmetic, although `CalculateFlags_ADD/SUB` already supplied
the result. The repair omits that unused placeholder for Add/Sub; it preserves
the result and flag calculation and leaves default optimizer settings intact.

For investigations beyond the first marker, `MADEIRA_IRCAP_FULL_BLOCK=1` extends
the existing explicit module/RVA capture. Output is bounded to 4096 IR lines
per stage and 64 KiB of host code per block, within the existing four compilation
sessions. It only reports compilation data and does not insert operations into
the guest IR. Keep it disabled during performance measurements. Raw guest IR,
JIT dumps and game logs remain private.

`negative_arithmetic_probe.c` checks ten real x86 operations against an
independent integer oracle, using sixteen boundary inputs each. It compares
32/64-bit results and CF, PF, AF, ZF, SF and OF, including positive/negative 128
and a large immediate that cannot fit the ARM immediate encoding. The host
reference completes 160 checks without mismatch. Before the repair, the iPad
run with flag elimination disabled faults at the malformed instruction. Build
25 completes all 160 checks with flag elimination enabled and disabled. Both
then fault in native cleanup and exit with code 1, so the arithmetic checks
pass but full process lifecycle remains unqualified.

The repaired disabled-pass path now reaches Solitaire's ELF entry point, then
fails at `game.exe+0x1461222` reading `0x30`. With normal flag elimination enabled,
the controlled prototype fails at `game.exe+0x1426827` reading `0x20`. These
experiments still use the private cooperative signal kernel; neither is a
production gameplay pass. The regular 138-file package is restored and verified
by device readback afterward, along with the original configuration. The native
app's menus, existing library and assets are preserved.

See the [Build 25 checkpoint](evidence/solitaire-build25-arithmetic-checkpoint.json).


### Native write faults and CRT detach, Build 26

The independent `native_write_fault_probe.c` calls the actual `ucrtbase.dll`
`memcpy` and `memset` exports through a SysV x86-64 caller. A Windows vectored
handler opens only the faulting 16-KiB page. Nine sizes cross page boundaries
in both private memory and two views of one shared section. The test checks
returned pointers, every copied/filled byte in both aliases, R12–R15 and the
expected number of faults. It uses no game code or assets.

The basic Build 25 fixture passes 16 checks and exits with code 0. The extended
fixture passes all 36 checks and 68 faults, then faults during process cleanup;
the arithmetic fixture also fails only after its checks. Disassembly locates
the cleanup caller in `InvalidationTracker::InvalidateAlignedInterval`, calling
the context's virtual `GetCodeInvalidationMutex` during DLL/CRT destruction.
Checking an engaged global `optional` cannot establish object lifetime once
global destruction has begun. Heap frees during that destruction still call
Wine's allocation/free/protection notification hooks.

For the iOS ARM64EC build, an early `.CRT$XLB` PE TLS callback closes those three
notification hooks on `DLL_PROCESS_DETACH`, before MinGW TLS callbacks and global
destructors. The callback uses a static atomic flag, allocates nothing and does
not read TEB/TLS. The final DLL's TLS array is checked for callback order. This
terminal gate leaves all memory protection and code invalidation active during
guest execution; it does not suppress a game exception or alter optimizer flags.

With Build 26 installed on the M2 iPad, the extended write fixture passes all
36 checks/68 faults and the arithmetic fixture passes 160 checks. Both exit
with code 0 and return to the library. Temporary profiles and configuration are
restored byte-for-byte. Recompiling the final write-probe source reproduces the
tested PE section contents; the local host reference also exits with code 0.

A controlled Solitaire run with the same private cooperative signal kernel and
GC diagnostics still reaches the ELF entry point and faults at
`game.exe+0x1426827`, reading `0x20`, matching Build 25's default-pass control.
The detach repair is therefore a separate runtime fix. These independent
fixtures do not qualify concurrent AnyPS5 write tracking, game rendering or
performance. All 138 regular game files are restored and hash-verified by
closed-app readback afterward; the private kernel is removed and the original
library/configuration remain intact.

A further compile-time capture at `game.exe+0x1426824` obtains frontend and
optimized IR without live argument instrumentation. Logging changes the run's
failure to `game.exe+0x1461222`, reading `0x30`; this is not a gameplay pass.
Two threads compile concurrently. The existing process-global capture mark can
be overwritten by another compilation, and the emitted-host-code arm reports
no matching marker. Until capture ownership is per compilation/thread, this
run cannot qualify the native code associated with that guest instruction.
Timing changed; neither a compiler defect nor a game race is established.
Raw IR and logs remain private. Production files/configuration are restored
and verified after this additional run as well.

See the [Build 26 record](evidence/solitaire-build26-native-write-and-teardown.json).

### Compiler capture ownership and SysV red zone, Builds 27–28

The compile-time IR capture mark now belongs to each thread's `PassManager`,
instead of a process-global RIP. A capture gets a session number carried through
frontend IR, optimization/RA output, guest-to-host mappings and emitted ARM64
bytes. Only that compiler's nonzero session can produce the host-code capture.
The process-wide limit still bounds the number of captures. All instrumentation
remains disabled by default, and this change inserts no live guest instructions.

Build 27's private graphics capture contains one complete matching session:
4,772 emitted bytes, 180 mappings, and a reconstructed byte hash equal to the
compile-time hash. The failing virtual-call sequence loads the guest object,
vtable and function in the expected order; its object pointer is already invalid.
This is evidence about one compiled block, not proof that all guest translation
or object producers are correct. Raw game IR and code remain private.

A separate synthetic SysV leaf function stores 16 qwords in the full 128-byte
red zone below RSP, then writes a protected page. On Build 27 all four writable
controls pass, but each of four handled write faults overwrites 12 of those
qwords. FEX's guest exception frame was aligned and placed immediately below
RSP. Relinked PS5 leaf functions use that space for live scratch even though
the exception dispatcher follows the Windows frame layout.

The iOS FEX exception path now subtracts 128 bytes before aligning/placing its
dispatcher frame. The saved guest context still contains the original RSP;
fault handling and instruction retry remain active. On Build 28 the same
fixture passes all eight checks, observes exactly four handled faults, exits 0
and returns to the library. A separate macOS x86 Wine run still clobbers one
slot through its different SEH path; it is not a passing reference for this fix.

### Private write-watch isolation, Build 28

The controlled game run still fails at `game.exe+0x1426827`, reading `0x20`.
The red-zone repair therefore fixes a reproduced ABI bug without resolving the
graphics-worker failure. A separate 60-second run uses the existing diagnostic
`APS5_NO_WRITE_WATCH=1`. It disables write watching for private guest arena
commits; shared-view tracking remains enabled. That run has no equivalent fatal
graphics access but presents only a white game area. Draw validation rejects
multisampling/coverage register `0x2f8 = 0x0030e003` and subgroup capability 61
in vertex stage 1. Avoiding a fault is not correct rendering, and this switch is
not adopted in the installed production configuration. Disabling private dirty
tracking can itself cause stale GPU resources.

To test the transparent Wine write-watch path independently,
`write_watch_probe.c` allocates 64 KiB at the same high guest address, checks
unwatched controls, and repeatedly rearms `MEM_WRITE_WATCH`. Two distinct AVX
vectors test all eight 64-bit lanes, including upper halves; a leaf function
checks SysV red-zone scratch around the faulting stores. The fixture compares
every allocation byte before and after `GetWriteWatch(WRITE_WATCH_FLAG_RESET)`,
checks dirty-address coverage and verifies an empty subsequent query. Boundary
cases cross 4-KiB and 16-KiB pages. Four additional event-synchronized threads
write distinct regions concurrently; rearming occurs while those threads are
waiting. Coarse host-page dirty reporting is accepted, missing written pages
are rejected.

The fixture passes 768 checks on both local x86 Wine and the iPad, exits 0 and
returns to the library on device. This does not test racing reset against an
active writer, AnyPS5's resource/cache integration, shared write tracking or
game object lifetimes. Those remain distinct possible causes. It provides no
basis for broadly disabling write watching in production.

After all bounded game experiments, the original 138-file installation is
hash-verified, the private cooperative kernel is removed, and configuration and
library are preserved byte-for-byte. Production Unity GC signal delivery and
the rejected graphics operations remain open. Solitaire is installed, with no
qualified visible gameplay, control acceptance or performance measurement.
See the [Build 28 record](evidence/solitaire-build28-redzone-and-write-watch.json).

### Captured vertex branch and preemption qualification

A subsequent bounded private shader capture still produces no playable image.
The device rejects viewport depths outside [0, 1], multisampling/coverage state
and a vertex subgroup capability. Fifteen requests are recovered locally from
the completed production-package readback. Fourteen have the data required for
replay; one lacks a dynamic descriptor source. Raw code stays outside Git.

The rejected vertex shader's only subgroup ballot implements a wave-mask branch
whose condition reads scalar draw constants. AnyPS5 0027 conservatively proves
that expression uniform and uses its already sensed Boolean directly. Any/all
of the same Boolean is that Boolean for every active invocation, including a
partial subgroup. Divergent phis are explicitly excluded even when their input
values are constants. Unknown or varying expressions keep the vote. Capability
analysis is updated together with branch emission, rather than weakening GPU
validation or simply deleting declared capabilities.

The captured vertex loses its single ballot and capabilities 61/64. All fourteen
complete replays pass SPIR-V validation before and after; the other thirteen are
byte-identical. Independent control-flow tests cover zero/nonzero EXEC/VCC
branches with both Boolean values and uniform/varying predicates. They verify
the emitted branch operand, vote count and capability requirement together.
The existing wide-subgroup regression also passes. The rebuilt driver and its
NID-patched private overlay pass the complete package's native import and 3,225
guest-NID audit. These host results do not establish correct device rendering.

A separate special-APC fixture checks whether a CPU-bound target can be
interrupted without entering an alertable Windows wait. Local x86 Wine accepts
the queue but only delivers after the target enters `SleepEx`; preemption fails
with exit 1. Target identity, arguments and SysV scratch pass. A later unlocked iPad run gives the same result: queue status 0, no callback
while spinning, one callback during the alertable wait, correct target/arguments
and preserved scratch, then process exit 1. This is a failed preemption test. This is why the cooperative
GC prototype is not promoted to production signal delivery.

The regular game package, configuration and library are restored. The combined
0027/0028 driver has now run in the controlled device pair below. Memory tracking,
MSAA and real GC preemption remain separate unresolved work. See the
[shader and preemption record](evidence/solitaire-uniform-vertex-branch.json).

### Exact stencil and eight-sample register states

The completed readback also contains eighteen `.regs` files written by the
existing rejected-draw path. Additional capture instrumentation is unnecessary.
The relevant viewport failure has Z scale 1, offset 0 and the negative-one-to-one
clip convention, producing a [-1,1] viewport. `DB_DEPTH_CONTROL=0x771` enables
stencil while disabling depth tests/writes. Its matched fragment program at
`0x7411540d00` has no `FragCoord` or `FragDepth` builtin.

AnyPS5 0028 resolves only an unobserved depth range to [0,1]. Its conservative
proof excludes depth tests, writes, bounds, bias, clamping, fragment depth exports
and all fragment-coordinate declarations. It requires one known fragment entry
point and walks bounded, length-checked SPIR-V instructions. Native unrestricted
ranges and legal/reversed ranges are kept. Clipping happens in clip coordinates
with the original convention; the adaptation does not change those coordinates,
XY, interpolation, stencil state or the original guest state. The same rule is
applied to the ordinary and cached-recipe draw paths. The
[Vulkan viewport equations](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html)
and [clip-volume rules](https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html)
explain why an unused framebuffer Z transform can be replaced independently.

Eligibility and depth/stencil state regressions pass under local x86 Wine. A
metadata replay loads the actual captured registers and matched fragment module,
reserving empty host memory only for the decoder's color-range check. Original
viewport validation fails; the resolved [0,1] range passes. This replay consumes
no image contents and performs no GPU rendering. The full graphics suite still
fails a fixed-function interpolation contract at PC 0; the full renderer is not
qualified. The rebuilt driver's native imports and 3,225 guest NIDs pass the
existing package's dependency audit.

Four other dumps describe real eight-sample targets: `PA_SC_AA_CONFIG=0x0030e003`
and `CB_COLOR_ATTRIB=0x0001b000` both encode sample counts of eight, with eight
stored fragments. This is not a harmless unused control bit. The backend's
single-sample attachments, depth surfaces, resource layouts, shader coverage and
resolve paths need actual multisample support; 0028 does not admit those draws.
Definitions and logarithmic encoding are available in AMD's
[register declarations](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx9/chip/gfx9_plus_merged_registers.h)
and [MSAA setup](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx9/gfx9MsaaState.cpp).

A private 138-file diagnostic package has now completed both bounded device
runs. Both keep multiblock enabled, MAXINST=5000 and DFE enabled; only the presence
of `APS5_NO_WRITE_WATCH` differs. Normal tracking fails in the graphics worker at
`game.exe+0x1461222`, reading 0x30. Without private tracking, the 60-second run
reports no fatal exception, but output remains white. The previous vertex vote
and unused viewport depth rejections do not recur; the remaining logged skipped
draws request eight samples. One screenshot transfer failed due to a remote XPC
connection invalidation; the later screenshot and closed-log transfer succeeded.

The original production runtime units and manifest were read back and verified
after restoration. Unchanged assets retain the independently verified original
device readback, avoiding a redundant full asset transfer on the nearly full Mac.
Configuration and library are byte-identical to their originals. The diagnostic
kernel and no-private-watch launch flag are not promoted to production.
See the [device pair record](evidence/solitaire-unobserved-stencil-depth.json).

A separate x86 Windows Vulkan capability probe ran through the actual iPad
Wine/FEX/MoltenVK route, exited 0 and returned to the library. The Apple M2 GPU
reports attachment sample masks 7, meaning 1, 2 and 4 samples, with no native
8-sample support. Queried RGBA8 UNORM/SRGB, BGRA8, RGBA16 float, D32 and D32/S8
attachment formats also report mask 7; D16/S8 is unsupported. These are queries,
not rendered MSAA or resolve tests. `gpu-probe --capabilities` performs this
query without claiming offscreen shader execution. Preserving the captured
8-sample rendering would require emulation. Merely deleting the validation
would request an unsupported image format/sample combination.
See the [actual device capability record](evidence/solitaire-ipad-msaa-capabilities.json).

The user reports audible but distorted audio. In this diagnostic run, native
streams mostly receive approximately real-time audio, with one 21.3-ms device
shortfall and source peaks below 0.4. Those counters do not establish correct
waveforms or explain the full audible defect. Audio remains unqualified.
