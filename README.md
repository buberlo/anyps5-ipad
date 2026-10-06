# anyps5-ipad

**The PS5 build of Dreaming Sarah boots on an M2 iPad and is playable in a basic sense.**

[AnyPS5](https://github.com/boykopovar/AnyPS5) relinks that title's own binary into an x86-64 Windows PE. [Madeira](https://github.com/willfaust/Madeira) runs the PE on the iPad: Wine ARM64EC, FEX, and this repo's `winevulkan` path through MoltenVK to Metal. The binary is the PS5 build, not a PC or Switch port. Dreaming Sarah is not a PS5 exclusive. This is local execution: not streaming, not Remote Play, and not a console emulator.

On 2026-10-06, about 16:11 UTC (23:11 WIB), buberlo recorded roughly two minutes on an iPad Air 13-inch (M2), iPadOS 27.0.1. The in-app library entry `Dreaming Sarah (PS5) 01.000.000, 64-bit, Vulkan, 5.26 GB` launched with JIT through the StikDebug-style flow. The Ratalaika Games publisher logo played with audio. The title screen and Options menu worked. New game started, the Asteristic studio intro and its music played, and about 60 seconds of gameplay followed: the player character was visible, walked and jumped through the forest, music continued, and a dialogue box rendered while she talked to an NPC. Input was the on-screen touch controller. The recording did not crash. Displayed FPS was not measured.

> **Demo:** TODO — put the public video or GIF link here.
>
> <!-- TODO: media URL. Do not commit game footage, GIFs, or screenshots. -->

The recording is a maintainer observation, written up in [docs/evidence/ipad-m2-dreaming-sarah-gameplay-recording.json](docs/evidence/ipad-m2-dreaming-sarah-gameplay-recording.json). Instrumented runs from the same day are in [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md).

Owner: buberlo.

The single-game host now opens the installed Dreaming Sarah package directly,
without Madeira onboarding, library or Steam menus. It embeds the private Blur
host’s local packet tunnel and automatically activates Madeira’s existing JIT
helper when needed. iOS requires one initial VPN permission; stored pairing and
private game files stay in the app container. Signed Build 15 completed three
ordinary cold starts on the M2 iPad, with all 18 controls and a native Vulkan
present-rate FPS counter visible. See [direct launch and automatic JIT](docs/SINGLE_GAME_BOOT.md)
and [bounded startup evidence](docs/evidence/ipad-m2-single-game-boot-build15.json).
These startup checks do not close sustained FPS, all-button input or save/load
acceptance.


## How it works

```
PS5 ELF, supplied by the user
  -> AnyPS5  (--windows --to-intel)
  -> x86-64 Windows PE + HLE PRX
  -> FEX on Apple ARM, AVX/AVX2 enabled
  -> Wine ARM64EC
  -> iOS winevulkan
  -> MoltenVK
  -> Metal
```

JIT comes from StikDebug/StikJIT. Nothing in the chain emulates the PS5 firmware.

This repository adds the pieces the pinned upstreams do not ship:

- iOS `winevulkan` bound to MoltenVK, so AnyPS5's Vulkan output reaches Metal. Madeira's DXMT/D3D path is not the one this title uses.
- AVX/AVX2 in FEX (`MADEIRA_FEX_AVX`), including the in-process bridge that upstream left off.
- A 4 GiB guest arena at 464–468 GiB virtual address, reserved in lazy chunks, so the Windows guest mapping fits this iPad.
- RDNA shader fixes for M2/MoltenVK: masked-shift folding that drops unsupported subgroup use, and validated fixed-function interpolation.

Longer form: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/PATCHES.md](docs/PATCHES.md), [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md).

## Status

Device notes below are the iPad Air 13-inch M2 on iPadOS 27.0.1, unless a row says otherwise.

| Verified on device | What was actually seen |
| --- | --- |
| Library launch into gameplay | 2026-10-06 recording, about two minutes, no crash. Logo with audio, title and Options, New game, Asteristic intro and music, then about 60 seconds of touch-controlled play: visible character, walk and jump, forest, music, NPC dialogue box. Displayed FPS was not measured. |
| Build 12 forest traversal | Six-minute observation. Sarah and the forest render. Held D-pad and analog touches go to the pool and back. No skipped draws and no fault terminal in that log. [Evidence](docs/evidence/ipad-m2-dreaming-sarah-held-input.json). |
| Build 13 app inactivity | Vulkan admission and drain. A brief background cycle drains both live devices and rendering resumes. [Evidence](docs/evidence/ipad-m2-vulkan-lifecycle-build13.json). |
| Supporting probes on the same iPad | Standalone AVX2 through Wine/FEX; the 4 GiB arena at 464–468 GiB; the original RDNA scene; a 600-second 1280×720 demo that exits 0. Guest loop rates in those logs are not displayed FPS. |

| Still open | Why it is open |
| --- | --- |
| Displayed FPS | Not measured on the gameplay recording. The commercial HUD's Frame 0 and its nominal 30 FPS are not acceptance. Guest loop counters are not displayed frame time. |
| Long sessions | The new recording is about two minutes. Ten-minute displayed-rate acceptance is open. Build 12's six minutes did not record displayed FPS either. |
| Full background recovery | Build 13 covers a brief cycle only. The separate long-cycle UI helper crashes. |
| Save/load | Not shown. |
| Audio acceptance | The recording includes publisher-logo audio, intro music, and gameplay music. PR #3's audio acceptance gate is still open. |
| Other iPads | This M2 only. |
| Public IPA | No installable IPA is published in this repository. A locally signed build has been installed on the test iPad; that file is not in Git. |
| Matching Windows gameplay | The updated-driver UM790 run shows the menu. Its final capture is a white transition, so Windows gameplay stays unqualified. [Evidence](docs/evidence/windows-um790-entry-prefix-menu.json). |

The 2026-10-05 foundation log is historical. It lives in [docs/MILESTONES.md](docs/MILESTONES.md) and [docs/PATCHES.md](docs/PATCHES.md). It is not the current device status.

## Credits

- [AnyPS5](https://github.com/boykopovar/AnyPS5) by boykopovar. Relinker and PS5 HLE, including the Vulkan graphics driver.
- [Madeira](https://github.com/willfaust/Madeira) by Will Faust. Wine ARM64EC and the iOS app shell.
- [FEX-Emu](https://github.com/FEX-Emu/FEX). The pinned iOS port is [willfaust/FEX](https://github.com/willfaust/FEX).
- [Wine](https://www.winehq.org/). The pinned tree is [willfaust/wine](https://github.com/willfaust/wine), branch `madeira-lgpl`.
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK). Vulkan on Metal.
- [StikDebug](https://github.com/StikDebug/StikDebug) and [StikJIT](https://github.com/StikDebug/StikJIT). On-device JIT.

Pins and licenses: [docs/UPSTREAMS.md](docs/UPSTREAMS.md), [docs/LEGAL.md](docs/LEGAL.md).

## Legal

No games, keys, firmware, or SDK files are in this repo. Users must use games they own and dumped themselves. Nothing here helps obtain them. Not affiliated with Sony Interactive Entertainment or Apple.

AnyPS5 is GPL-2.0-only and Madeira is GPL-3.0-or-later. They stay separate submodules. [docs/LEGAL.md](docs/LEGAL.md).

## Layout

```
upstreams/     pinned submodules, unmodified until you apply patches
patches/       one series per upstream
scripts/       apply patches, checks, build entry points
tools/         probes and host-side checks
docs/          architecture, patches, implementation, evidence
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

Build and device steps: [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md).
What a Vulkan-only iPad build leaves out: [docs/SLIM.md](docs/SLIM.md).

## iPad configuration the patches expect

These are switches, not measurements:

```sh
MADEIRA_WITH_VULKAN=1          # Madeira build: winevulkan + MoltenVK unix side
MADEIRA_FEX_AVX=1              # xtajit64's FEXCore; the in-process bridge defaults on
APS5_GUEST_ARENA_LAZY=1        # reserve guest VA in 256 MiB chunks
APS5_GUEST_ARENA_SIZE=0x100000000   # 4 GiB example; default remains 448 GiB
```

The qualified M2 profile sets `APS5_GUEST_ARENA_BASE=0x7400000000` (464 GiB)
with that 4 GiB size, so the guest window is 464–468 GiB.
`Madeira.entitlements` gains `com.apple.developer.kernel.extended-virtual-addressing`
only after the patch is applied. The provisioning profile still has to grant it.
