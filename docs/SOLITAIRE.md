# 15in1 Solitaire runs locally on iPad

The decrypted **PS5 version of 15in1 Solitaire, 01.000.000**, now reaches real
card gameplay on an iPad Air 13-inch M2. In a 12-minute physical-device run we
selected Golf, dealt its complete board, drew cards, selected 2♣, moved it onto
A♥ to expose 9♣, and undid the move. The game process survived all seven
reviewed inputs and the full run. This is the central compatibility milestone.
[Gameplay and input evidence](evidence/ipad-solitaire-zero-stencil-invariant-controls-20261009.json).

The game is its relinked PS5 binary. AnyPS5 prepares an x86-64 Windows PE and
PS5 API replacements; Madeira runs it through Wine and FEX. AGC commands and
RDNA shaders become Vulkan and SPIR-V, then MoltenVK translates them to Metal.
The Wine/SDL window supplies the CAMetalLayer. Execution is local on the iPad.

## What made it work

There were separate CPU, module-startup and graphics failures. Updating one
component or creating a swapchain was insufficient. The working path combines
the following repairs and compatibility settings:

| Blocker | Implemented change |
| --- | --- |
| Exceptions and protected writes could lose guest CPU state. | The paired native/AnyPS5 bridges preserve upper YMM state and x86 flags, clear DF before C++ handlers and restore handler-selected state on continuation. The asynchronous path also preserves the SysV red zone. FEX's pending AVX state was repaired and the iOS port reconciled with FEX-2610. Protected-page tracking stays enabled. [CPU contracts](EXCEPTION-MEMORY-CONTRACTS.md), [integrated runtime and device probes](evidence/fex-2610-integration-20261008.json). |
| A retained guest export could be called before its module initialized. | Relinker patch0045 retains guest constructors and destructors when dependent modules still use guest exports. The game and bundled modules were freshly relinked and NID-patched with real HLE providers; Solitaire's 47 explicit HLE dependencies resolve. [Lifecycle repair](evidence/retained-guest-export-lifecycle-20261008.json). |
| RDNA shader operations and descriptor types were not supported by the iPad path. | Validated fixed-function interpolation replaces the admitted barycentric sequences. The inspector registers opaque image/sampler types and checks their actual descriptor roles and shapes. Unsupported forms still fail validation. [Interpolation implementation](../README.md#3-graphics-ps5-commands-to-metal), [descriptor repair](evidence/opaque-descriptor-shader-validation-20261008.json). |
| Solitaire requests eight MSAA samples; this M2 path supports native attachments up to four. | Eight logical samples are represented by two four-sample image layers. Production RGBA8/S8 transfers preserve sample positions and guest tiled layouts. Canonical internal rectangle stages and the restricted GC10 resolve reconstruct the single-sample output. The game retains all eight logical samples. [MSAA implementation](MSAA-EMULATION.md), [resolve probe](evidence/ipad-fixed-color-resolve-20261008.json). |
| The recorded/resident draw path could leave the selected output white. | The working profile enables `APS5_DRAW_TRANSITIONS=1`, retaining per-draw upload/download barriers, target layout transitions and one render pass per draw. Snapshots, guest dirty tracking, fences and changed-byte writeback remain active. This is the verified compatibility path; the lean resident path is not accepted for Solitaire. [Transition/menu checkpoint](evidence/ipad-solitaire-transition-menu-20261008.json). |

Transfer-cost work accompanied the compatibility repairs. Patch0052 replaces
imported variable-size copies in the inner color-texel loops with a single
dispatch to literal-size copies, preserving the AMD address equations and
padding. Its subsequent controlled device run accepts Right/A and enters Golf.
Later GPU color/stencil transfers and pipeline caches preserve those sample and
memory contracts. The records establish the combined working configuration;
they do not isolate each change's individual performance contribution.
[Source/build checks](evidence/inline-color-texel-build-20261008.json), [game-entry record](evidence/ipad-solitaire-inline-color-texel-input-20261008.json).

The touch path is Madeira's controller → XInput → SDL → `scePad`. It was
verified through visible card changes, rather than merely successful touch
dispatch or an advancing frame counter. B is Undo in the tested Golf scene.

## Installed configuration

The current installation is **Native54 / Source65**. It preserves the working
rendering and exception settings, the 18 controls and the game data. Its latest
two-minute check shows the rendered selection menu with an FPS-only overlay.
The earlier seven-action gameplay proof belongs to Native48 / Source60; touch
automation timed out before a new Source65 card-action test could run.

The principal settings of this installed profile are:

```ini
env.MADEIRA_FEX_AVX = 1
env.APS5_X64_SIGNAL_SUSPEND = 1
env.APS5_PRESERVE_ASYNC_AVX = 1
env.APS5_PRESERVE_ASYNC_FLAGS = 1
env.APS5_VEH_CONTEXT_BRIDGE = 1
env.APS5_FIXED_FUNCTION_INTERPOLATION = 1
env.APS5_ENABLE_SAMPLE_GROUPS = 1
env.APS5_DRAW_TRANSITIONS = 1
env.APS5_GPU_COLOR_SAMPLE_TILING = 1
env.APS5_GPU_STENCIL_SAMPLE_TILING = 1
env.APS5_ZERO_STENCIL_INVARIANT = 1
env.APS5_SWAPCHAIN_IMAGES = 3
env.APS5_AUDIOOUT_FRAME_PACING = 1
```

This is a summary of one qualified title's profile, not a universal recipe.
The lazy guest arena reserves 4 GiB of virtual address space in 256 MiB chunks;
this is separate from physical RAM. The actual JIT pool is 512 MiB through
Madeira's **global** `pool` setting. Per-game `pool` is not used by this host.
The reviewed gameplay run had a 1920 × 1080 guest color target, a 1280 × 720
host window and a 768 × 432 three-image swapchain. Those are distinct sizes.

## Follow-up work

Patch0065 adds optional legacy AudioOut producer pacing. The controlled capture
no longer exhibits the previous four-packet repetition or consecutive identical
native audio blocks; audible quality still needs listening verification.
Native54 also makes the expensive statistical memory census optional while
retaining JIT warming, footprint sampling and passing heap checks. These are
follow-up changes, rather than the original white-frame rendering repair.
[AudioOut implementation and installation](AUDIOOUT-FRAME-PACING.md), [native component record](evidence/quiet-runtime-diagnostics-20261009.json).

A completed game, all fifteen modes, clean audio, save/load, background recovery
and sustained performance remain unqualified. The latest native component test
also reports a serious-to-critical thermal-state transition. Neither its single
HUD reading nor the earlier
mixed gameplay timing demonstrates constant 60 FPS.
