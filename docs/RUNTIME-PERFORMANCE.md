# Runtime diagnostics and display measurement

The app retains Madeira's library, settings, JIT setup and touch controls. These
changes affect the shared execution and measurement paths, not the launcher.

## Default cost and explicit diagnostics

- Native thread samplers, register watchdogs, XPROBE, WPROF and the legacy
  suspending profiler are disabled by default. `MADEIRA_QUIET` alone did not
  control all of them. Set `MADEIRA_DIAGNOSTICS=1` before runtime initialization
  to enable them, or use individual switches: `MADEIRA_THREAD_SAMPLER`,
  `MADEIRA_XPROBE`, `MADEIRA_WPROF`, `MADEIRA_LEGACY_PROFILER`,
  `MADEIRA_EXCEPTION_DIAGNOSTICS`, `MADEIRA_SURFACE_DIAGNOSTICS`, `MADEIRA_FEX_LOG`.
- Opt-in requires exactly `1`. `MADEIRA_DIAGNOSTICS_SECONDS` defaults to 30,
  is limited to 60, and determines the capture window. Exception handlers read
  preinitialized switches and shared tick data instead of taking CRT environment
  locks. Context reconstruction and fault handling always run, regardless of logs.
- FEX DEBUG/INFO chatter is filtered; errors, assertions, throws, unknown levels,
  incomplete records and multiline records remain visible. This does not suppress
  Wine errors or eliminate every remaining server log producer.
- `APS5_FRAME_TIMING=1` enables AnyPS5 frame metric maps, diagnostic locks and
  report construction. With timing logging compiled off and no opt-in these are
  skipped. Submission identity checks, GPU fences and completion remain intact.
- Draw-worker profiling only collects timestamps when `APS5_PROFILE_DRAW` is
  set. Keep draw/GPU profiling off for final throughput comparisons.
- `APS5_NO_WINDOW_TITLE=1` skips the per-frame SDL title update. The iPad host
  defaults this switch to 1 because that title bar is invisible. Set it to 0
  before launch to restore title diagnostics.

## Three swapchain images

The HLE now requests three images on initial creation and resize. Set
`APS5_SWAPCHAIN_IMAGES=2` before starting Wine for the former comparison path;
`3` selects the default explicitly. Other values fail with a configuration error.
The request respects the surface minimum and maximum; Vulkan's zero maximum is
unbounded. Logs distinguish the requested count from the count actually returned.

An additional image can improve overlap between rendering and FIFO presentation.
It can also add drawable memory and presentation latency. Resolution, interpolation,
FIFO mode, acquire fences, per-image binary semaphores, render-slot fences and
retirement ordering remain unchanged. Image count is separate from
`APS5_FLIP_INFLIGHT` and does not authorize reuse of unfinished GPU resources.
The reported 60.0 reading in an image-count comparison motivates this candidate;
it is not a sustained device benchmark or proof of unique displayed game frames.

Existing installations retain their game-local HLE. Reprepare the game with the
new HLE archive; updating only the native app does not replace those libraries.

## Reported presentation timing versus accepted submissions

`madeira_get_present_count()` remains the count of accepted Vulkan presents.
It does not establish how many images the display actually showed. The native
Vulkan bridge adds these separate exports after Wine handle conversion:

```c
uint64_t aps5_vulkan_displayed_count(void);
int aps5_vulkan_display_timing_available(void);
double aps5_vulkan_display_fps(void);
```

It enables `VK_GOOGLE_display_timing` only if the host advertises it. The adapter
uses the extension's native ABI because the pinned Wine headers omit it. Caller
pNext chains, desired presentation times and native Vulkan errors are preserved.
No pacing change, frame generation, image skipping or readback is introduced.

Only the newest tracked swapchain contributes. Replacement and foreground
transitions start a new measurement epoch, reset the interval window and drain
pending history. Up to eight devices and eight swapchains are tracked; telemetry
is unavailable when capacity or extension/function support is missing. Zero and
non-increasing timestamps cannot inflate the counter. FPS comes from the last
256 reported presentation intervals; unavailable/inactive returns -1, and absent
or stale completions (over 1.5 seconds) return zero.

The Madeira overlays label this source as **present FPS**. Where display timing
is unavailable, the existing accepted-present calculation is **submit FPS**.
The latter is a fallback diagnostic, never a display-performance result.
The pinned MoltenVK can substitute a completion clock when Metal supplies no
presentation timestamp. This adapter does not independently qualify that source:
even positive GOOGLE timestamps and a 60.0 HUD reading are not proof of physical
display cadence. A raw-Metal timestamp policy and visible-scene evidence are
needed before accepting a sustained displayed-FPS claim.

`APS5_PERF_REPORT=1` emits display and native aggregate JSON at most once per
second. `[anyps5-display]` includes actual time, epoch, swapchain identity,
resolution, refresh period, displayed count and a cumulative 257-bin interval
histogram. Bins are 0.1 ms; intervals above 25.6 ms enter an explicit overflow
bin. `[anyps5-native]` records submit, present, acquire and idle call time after
the lifecycle gate; it does not measure translated guest CPU work or GPU time.

```sh
python3 tools/perf/summarize_display.py private-run.log --warmup-seconds 10 --minimum-seconds 60
```

The parser separates foreground/swapchain/resolution epochs, subtracts cumulative
histograms, and rejects regressing or mismatched data. Percentiles are histogram
upper bounds, with overflow unbounded. Logs stay local. Presentation timestamps
still require a separately confirmed visible foreground game scene. Neither an
extension test nor an import-resolved binary proves gameplay or stable 60 FPS.

## Build and verification

`scripts/m3-madeira-ios.sh` defaults to `APS5_BUILD_PROFILE=performance`: the
qualified Debug/JIT configuration and signing/ABI settings are retained while
native C/ObjC uses `-O2` and Swift uses `-O`, with dSYM symbols. Use `diagnostic`
for the previous unoptimized host. This is a build profile, not a measured gain.

The PE build now rebuilds and verifies `ntdll.dll` as well as `xtajit64.dll`,
`winevulkan.dll` and `vulkan-1.dll`. Otherwise the ARM64EC exception-policy change
could remain absent from the installed app despite updated native sources.
`export-hle-runtime.py --provenance FILE` can embed local build provenance.

Run these locally after applying the patches:

```sh
python3 tools/checks/test_frame_timing.py
python3 tools/checks/test_runtime_diagnostics.py
python3 tools/checks/test_display_timing.py
python3 tools/checks/test_display_parser.py
python3 tools/checks/test_vulkan_lifecycle.py
python3 tools/checks/test_swapchain_images.py
```

These compile production policy/measurement code and exercise error propagation,
pNext preservation, unsupported fallback, timestamp ordering, pause/resume and
replacement lifetimes. They do not replace a full signed build or physical-iPad
benchmark. No GitHub Actions are used.
