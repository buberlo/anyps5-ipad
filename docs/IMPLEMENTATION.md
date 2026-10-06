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
  candidate 8–12 GiB arena; existing user settings are retained.
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
toolchain, whose archive has an explicit SHA-256. It builds the six real HLE
targets, the relinker/NID patcher, the smoke demo and CPU/exception guests.
`scripts/package-runtime.py` accepts unpatched HLE outputs, patches a fresh copy,
walks native imports/forwarders recursively and resolves every guest NID before
writing the package. Test-only ELF linker stubs cannot be packaged as PRX files.
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
  did not establish a verified live debugger. The app correctly withheld the
  guest launch. App launch is therefore not yet Wine/FEX execution evidence.
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
iPad runtime mapping behavior still needs the guest probes.

Additional local evidence lives under ignored `build/logs/`,
`build/ios-runtime/logs/` and the fixture directories. The signed probe,
installation/launch receipts, final executable hashes and screenshot are in
`build/ipad-probe/`. Its process was closed and the shared-device reservation
released after the test. Wine/FEX execution and the full iPad acceptance below
remain open; prepared CI has not established them.

The initial Xcode **"No Accounts"** signing failure was resolved by signing in
to the developer account and generating the app's explicit capability profile.
The signed export is `build/ios-runtime/AnyPS5-iPad-SIGNED.ipa`, with a separate
archive/component hash manifest and `signing-verification.json`. The earlier
unsigned export remains separately labelled. iOS signing does not embed the
macOS `allow-jit` entitlement; a working debugger and actual JIT execution are
still required and cannot be inferred from successful signing.

## Acceptance and current limits

The initial guest-arena candidate is base `0x200000000`, size `0x100000000`,
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

Dreaming Sarah remains a later data-dependent test: inventory the exact ELF,
module/NID imports and transitive runtime dependencies; establish the same
version on Windows before comparing menu/gameplay/audio/saves on iPad. The
original demo is not commercial-title compatibility evidence.
