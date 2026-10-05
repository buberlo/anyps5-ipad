# HANDOVER: anyps5-ipad (for Codex)

Repo: https://github.com/buberlo/anyps5-ipad (public, owner Konrad Kern / buberlo)
State as of 2026-10-06 ~07:00 WIB (UTC+7):
- `main` = merged PR #1 (foundation), merge commit `20bb980`.
- Active work: draft PR #2, branch `cursor/anyps5-ipad-runner-followups-d755`, head `917dbae`.

## Goal
Run PS5 titles relinked by AnyPS5 on an iPad, through this chain:

```
PS5 ELF --AnyPS5 relinker (--windows --to-intel)--> Windows PE + HLE PRX libs
   --> Madeira (iOS 26 app: Wine 11 ARM64EC + FEX x86-64->ARM64 JIT, sideloaded, JIT via StikDebug)
   --> winevulkan --> MoltenVK (linked into the iOS app) --> Metal --> iPad
```
Research prototype. No game dumps, keys, firmware, or secrets in the repo (see `docs/LEGAL.md`).

## Upstreams (git submodules under `upstreams/`, pinned; changes live in `patches/`)
| Upstream | Commit |
|---|---|
| AnyPS5 (boykopovar/AnyPS5) | `0518f0e` |
| Madeira (willfaust/Madeira) | `bbbf8d0` |
| FEX fork (willfaust/FEX, `ios-port-2607`) | `be778d7` |
| Wine fork (`madeira-lgpl`) | `3a54f568` |
| MoltenVK | `fae55a18` |

Submodule trees stay at upstream. Use `scripts/apply-patches.sh` (apply and reverse, bash 3.2-compatible).

## Docs to read first
`README.md`, `docs/ARCHITECTURE.md`, `docs/MILESTONES.md` (M0-M5), `docs/PATCHES.md` (per patch: verified / compile-only / untested), `docs/SLIM.md`, `docs/UPSTREAMS.md`, `docs/LEGAL.md`.

## Key patches
- AnyPS5: Vulkan portability enumeration + `VK_KHR_portability_subset` (required: stock `vkCreateInstance` on MoltenVK returns `VK_ERROR_INCOMPATIBLE_DRIVER`). Configurable/lazy Windows GuestArena (upstream reserves 448 GiB VA; env `APS5_GUEST_ARENA_*`, `APS5_GUEST_ARENA_LAZY=1`). `APS5_SLIM=1` removes SDL from `libSceAgcDriver` (8.9 MB -> 6.7 MB Linux) and dlopens Vulkan directly.
- Madeira/Wine: build Wine with Vulkan (upstream Madeira uses `--without-vulkan`). New `vulkan_ios.c` / `vulkan_metal_ios.c` plus a MoltenVK static loader; Metal surface via `VK_EXT_metal_surface`/CAMetalLayer. Linux guards (`patches/wine/0001-linux-winevulkan-guards.patch`). Weak definitions for `ios_srcwatch_arm*` / `winios_dump_srcbits`.
- FEXBridge.mm: AVX on unless `MADEIRA_FEX_AVX=0` (upstream Madeira hardcodes `SupportsAVX=false`; PS5 code uses AVX2 without CPUID gating). SVE stays off.
- Entitlements: `extended-virtual-addressing` added (needs a signing profile that grants it).
- Slim app build: DXMT/madeira-d3d12 replaced by `scripts/slim-dxmt-stub.c` (empty stats/canary functions). `libmadeira_rppairing.a` stubbed (it is iOS 27 on-device StikJIT pairing). Under `APS5_SLIM=1`, ntdll-unix drops dwrite, winegstreamer and FFmpeg (placeholder `libav*.a` archives so Xcode `-l` resolves). `MadeiraJITHelper` is omitted in the unsigned build (StikJIT built with Swift 6.4, Xcode 26.3 has 6.2.4).

## Measured results (hosted GitHub runners, all free because the repo is public)
- MoltenVK on `macos-15` (Apple Paravirtual GPU, Vulkan 1.1.357): `tools/vk-requirements` reports `hard_fail=0` via portability enumeration. BDA, shaderInt64, 8-bit storage, float controls, stores/atomics and BC textures all pass. Missing optional features: mesh shader, shader clock, depth range unrestricted, primitive topology list restart, image view min LOD, shaderFloat64, depthBounds. A virtual GPU, not an iPad. No AnyPS5 VkDevice created yet. `macos-14` is out of the matrix (its paravirt device lacks `newArgumentEncoderWithLayout:`).
- lavapipe (Ubuntu 22.04 + 24.04): `hard_fail=0`.
- AnyPS5 Linux build plus 17 synthetic relinker tests pass. Synthetic PE (`--windows` and `--to-intel`) runs under x86-64 Wine and exits 42.
- WinLibs GCC 15.2.0 posix-seh (`windows-latest`, `APS5_SLIM=ON`): `libc.prx` (875,965 B), `libkernel.prx` and `libSceAgcDriver.prx` (7,068,172 B) link. Not executed.
- `winevulkan-ios` (Xcode 26.3, iPhoneOS 26.2 SDK): arm64 Mach-O `winevulkan.so` links. `moltenvk_static_loader.o`, `vulkan_metal_ios.o` and `vulkan_ios.o` compile. Not executed.
- `ubuntu-24.04-arm`: native FEX runs an x86-64 nostdlib guest (exit 42). Synthetic PE under Wine+FEX does NOT run yet (`could not load kernel32.dll, status c0000135`, exit 53). Latest attempt bind-mounts the Wine dirs and logs with `WINEDEBUG=+module,+file`.
- `madeira-simulator`: iOS FEX archives build (`libFEXCore.a` 4.9 MB), and GnuTLS, FreeType 2.13.3 and FFmpeg 7.1.1 build for iPhoneOS. Swift/ObjC compile. The app does NOT link yet. It needs `libntdll_unix.a`, `libwin32u_unix.a` and `libwineserver.a`, which Madeira doesn't publish (only `Madeira-0.1.3.ipa`), so they are built from source now. ntdll-unix failed on `server_ios.c` (`ri_page_wait_time_mach` missing in the 26.2 SDK, now passes 0) and on dwrite/winegstreamer headers (now dropped under SLIM). The run for `917dbae` was pending at handover.

## Open work, in order
1. Get `madeira-simulator` to link the slim unsigned app for iphoneos plus the simulator (CI job in `.github/workflows`, script `scripts/m3-madeira-simulator.sh`).
2. Get the synthetic PE running under Wine + native FEX on `ubuntu-24.04-arm` (M1), then with Vulkan (M2).
3. Execute the Windows PRX libs under Wine (x86-64), then under FEX.
4. Create an AnyPS5 VkDevice on MoltenVK (macOS runner) with the portability patch.
5. Signed-IPA build script plus `docs/DEVICE.md`: Konrad's iPad boot (iOS 26, sideload, StikDebug JIT, profile with increased-memory-limit and extended-virtual-addressing, `MADEIRA_FEX_AVX=1`, `APS5_GUEST_ARENA_LAZY=1`). Only this step needs Konrad and a real M-series iPad.
6. M5: first menu of a real title (Dreaming Sarah is the only verified AnyPS5 title upstream). Konrad supplies his own legal dump; never commit it.

## Working rules
- Keep `docs/PATCHES.md`, `docs/SLIM.md`, `docs/MILESTONES.md` and the PR description honest: verified / compile-only / untested.
- Work on branches and PRs against `main`. CI runs on hosted runners. Concurrency group is per commit with `cancel-in-progress` off. Fallback runners: `ubuntu-22.04`, `windows-2022`.
- Respect licenses: Madeira GPL-3.0-or-later, Wine LGPL, FEX MIT, MoltenVK Apache-2.0, AnyPS5 (see its repo).
- Known runner quirks: hosted-runner acquisition timeouts (15 min), lost-communication cancels. Re-run before debugging.
