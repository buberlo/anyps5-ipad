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

## Optional guest thread priorities

`APS5_HOST_THREAD_PRIORITY=1` enables coarse guest-to-Windows priority mapping
in the rebuilt `libkernel.prx`. Guest priority 700 maps to normal; each 128 units
selects one regular Windows priority step, clamped to -2 through +2. Smaller
guest values mean greater importance. This is an approximation, not a complete
PS5 scheduler contract. A thread applies its initial priority before publishing
successful creation. Failed host requests return a guest error without publishing
the thread or changing its recorded priority.

On iOS, also set `MADEIRA_IOS_THREAD_PRECEDENCE=1` before starting Wine. Wine's
server retains a validated same-task Mach send right from the native thread's
startup handshake and applies `THREAD_PRECEDENCE_POLICY`, checking the result by
reading it back. It preserves the existing QoS/ECO settings and releases the right
with the server thread object. Failed operations preserve thread metadata and
attempt to restore the previous policy. Live process-wide scheduling changes are
rejected before mutation because this prototype cannot atomically update every
thread. Unchanged requests and pre-thread process initialization remain supported.

Both switches require exactly `1` and default to off. Updating the app alone does
not replace a game's kernel; rebuild and reprepare its HLE package. Native policy
readback is verified separately on the iPad. Two diagnostic game runs collected
full ingress/postmix windows, but XCTest automation failed and the app became
inactive. Strong repeated audio remains in both recordings. This comparison
qualifies no audio repair or scheduler speedup; the switches stay off in the
normal game profile pending a valid foreground comparison.

`MADEIRA_IOS_THREAD_PRECEDENCE_TRACE=1` additionally prints at most 32 explicit
priority/boost requests, including mapped importance, status and readback match.
It produces no bootstrap trace and is also off by default. Keep audio captures
and other diagnostics off during performance qualification.
[Implementation and verification](PATCHES.md#optional-host-thread-priorities-wine0004-and-anyps50064).

## Private bounded audio capture

Madeira0049 provides an optional postmix diagnostic, after the existing stereo
mix and clamp. Set exact `MADEIRA_AUDIO_CAPTURE_POSTMIX=1` before native runtime
initialization to capture at most 480,000 float32 stereo frames at 48 kHz.
`MADEIRA_AUDIO_CAPTURE_SKIP_FRAMES` defaults to zero and accepts strict unsigned
decimal values through 28,800,000. It skips rendered audio frames; neither the
skip nor the capture duration is a game-time measurement.

The callback uses preallocated arrays and bounded copies. A non-real-time worker
exports a private WAV and timestamp/epoch JSON beneath the app's Documents
directory, without overwriting existing files. Invalid setup, callback contention,
record limits or a timeout produce explicit incomplete results. No export/free
occurs while a producer owns the arrays. Keep this switch off during performance
qualification: callback copy overhead is not measured or accepted.

The separate [iPad recording analysis](evidence/ipad-solitaire-audio-period-replay-20261009.json)
finds an exact repeating 1,024-frame period with continuous callback timing.
This is a diagnostic observation, not an audio fix or quality acceptance.
Captured audio and raw metadata stay local, outside the repository.

Madeira0050 adds a separate source diagnostic at native WASAPI `ReleaseBuffer`,
after existing silent-buffer zeroing and frame clamping, before the scratch-to-ring
copy. Exact `MADEIRA_AUDIO_TRACE_RELEASE=1` enables it;
`MADEIRA_AUDIO_TRACE_RELEASE_SKIP_FRAMES` has the same strict numeric bounds,
but counts source frames independently for each stream. At most 16 unrecycled
stream generations retain 480,000 source frames as canonical 1,024-frame
fingerprints and bounded submission metadata. Unsupported formats, lifecycle
interruptions, limits and deadline expiry remain explicit incomplete results.

This trace exports private JSON only, with no PCM. The native real-time callback
is unchanged from Madeira0049. Hashing, timestamp sampling and a metadata mutex
occur on the non-real-time source path only when enabled; their overhead remains
unmeasured. Equal fingerprints do not establish byte identity. Equal source and
postmix skip values do not align windows, because their ordinals and callback
epochs describe different events. See the separate
[source and signed-host checkpoint](evidence/audio-release-trace-host-build-20261009.json).

A [separate iPad source/audio run](evidence/ipad-solitaire-audio-release-source-20261009.json)
finds the same exact 1,024-frame postmix period and constant fingerprints before
the native ring copy. Source ordinals and ring cursors still advance. The run
also verifies a legal Golf move and undo; it does not identify the audio defect,
accept sound quality or qualify performance. Original runtime bytes and Native48
are restored after the temporary test.

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

## Refreshed runtime and hardware audio period

The 2026-10-07 pin refresh uses AnyPS5 `ee391a56`, Madeira `48f97642`,
FEX `3bec2ac49` and Wine `257f271cfff`. The iPad's qualified lazy 4 GiB
window at `0x7400000000` still overrides the larger upstream arena; it is
virtual address space, not a 4 GiB physical allocation. Repeated savedata
initialization fixes from AnyPS5 are included. Shaders requiring unavailable
64-bit buffer atomics fail explicitly.

After successful `AVAudioSession` activation, the native bridge publishes
`IOBufferDuration` atomically to the WASAPI device-period query. A 1024-frame
48 kHz hardware callback requires a 21.333 ms producer period. The previous
10 ms response let SDL queue only 960 frames, leaving 64 silent frames per
callback. Invalid or unavailable durations retain the 10 ms fallback. This
changes producer block sizing, not samples, effects or sample rate. Route
changes and longer physical-device audio tests remain separate qualification.
The Build 18 iPad launch reports the expected 21.333 ms hardware period.
Run the production-policy regression locally after applying the patches:

```sh
cc -std=c11 -Wall -Wextra -Werror -I upstreams/Madeira/build/ntdll-unix \
  tools/perf/test_audio_period.c -lm -o build/audio-period-test
build/audio-period-test
```

Additional cold-start experiment switches are `APS5_MEASURE=1` (quiet Wine and
MoltenVK logs), `APS5_PERF_REPORT=1` (native phase/memory reports once per second),
`APS5_FRAME_COUNTERS=1`, `APS5_VBLANK_HZ=60`, `APS5_TEXTURE_CACHE_MIB=1..4096`,
and the existing `APS5_FLIP_INFLIGHT`. Leave vblank, inflight and FEX
synchronization at their defaults for the initial installation. The explicit
`APS5_SWAPCHAIN_IMAGES=2|3` policy remains the authoritative buffer-count
control, including resize. Configure these through the existing library's
per-game environment settings; host settings must be applied before Wine starts.

The optional MoltenVK stability series and FEX VirtualProtect experiment are
excluded from the default build. Neither a HUD value nor the selected three-image
profile establishes sustained 60 unique game images per physical refresh.

## Solitaire diagnostic profile and candidates0054–0056

The separate 53-patch diagnostic run records nine late profile intervals with
250 draws each: 240 recorded and ten synchronous. Dividing each interval's
aggregate phase totals by its ten synchronous draws gives median diagnostic
values of 116.53 ms for `readTarget`, 225.95 ms for `prepare`, 368.97 ms for
`writeBack`, 1.96 ms for pipeline lookup/construction and 18.08 ms for `sync`.
The totals include work across all 250 draws. These normalized aggregates are
neither isolated per-frame measurements nor FPS; profiling, transition controls
and captures are active. See the [bounded physical profile](evidence/ipad-solitaire-scoped-bulk-driver-writes-profile-20261009.json).

The grouped CPU path makes the expensive work explicit: target reads detile
guest color samples, preparation creates or acquires transfer resources, and
writeback tiles color/stencil samples before guest-memory writes. The new
candidates address narrower parts of that work. Patch0054's internal resolve
pipeline cache avoids repeated construction for identical generated SPIR-V;
the small pipeline total alone cannot justify a large speed prediction.
Patch0055's owned tiled resolve-source snapshot avoids that resolve's CPU
detile/retile cycle, but does not remove the producer Draw path's readTarget or
writeBack transfers. Patch0056 replaces variable-size depth/stencil inner-loop
copies with literal-size copies, removing imported CRT transitions while keeping
every sample and padding rule.

Only exact `APS5_CACHE_COLOR_RESOLVE_PIPELINES=1` and
`APS5_RAW_RESOLVE_SNAPSHOT=1` enable the first two candidates; both default off.
Supported fixed-size depth copies are always used. Focused native checks and
the combined 56-patch HLE/host build pass, with separate mock/oracle boundaries.
The package combines three changes and is not a matched one-change benchmark.
No device speed improvement or constant-60-FPS acceptance is recorded here.
[Pipeline-cache checks](evidence/color-resolve-pipeline-cache-native-20261009.json),
[combined source/build qualification](evidence/resolve-snapshot-depth-copy-build-20261009.json).

## Bounded iPad run of candidates 0054–0056

The combined 56-patch HLE now has a separate 420-second physical-iPad run
with the retained Build 47 host. The full selection menu is visible at the
60-second capture, and Right followed by A enters Golf. The complete seven-column
tableau is visible at 240 seconds. A later Y input changes the waste card from
10♦ to 4♣ while the same app and JIT-helper processes remain alive. The earlier
53-patch UI run shows its full menu at 120 seconds, is still dealing at 240 seconds,
and shows the full board at 300 seconds. The deals differ, so this is evidence of
startup, rendering and one stock action rather than a matched gameplay route.

The diagnostic menu profiles use the same environment apart from the new raw
snapshot and pipeline-cache opt-ins. Nine stable 53 intervals contain 250 draws and
ten synchronous draws each; twelve stable 56 intervals contain 500 draws and twenty
synchronous draws each. Their median reported synchronous-draw means are
732.236 ms and 483.396 ms. The phase totals, divided by each interval's synchronous
draw count, are:

| Diagnostic phase | 53 normalized median | 56 normalized median |
| --- | ---: | ---: |
| Read target | 116.53 ms | 116.2375 ms |
| Prepare | 225.95 ms | 103.15 ms |
| Write back | 368.97 ms | 240.985 ms |
| Pipeline | 1.96 ms | 1.55 ms |

These phase totals include work across all draws; the normalization does not
turn them into isolated per-frame measurements. Captures, trace, bounded pixel
statistics, profiling and the per-draw transition control remain enabled. The
separate 53 UI run has profiling off, unlike 56. Neither comparison isolates one
of the three changes, repeats matched thermal conditions, or accepts a sustained
speedup.

During the common first 175 seconds, the selected-source trace records 323 present
requests for 56, compared with 166 for the 53 profile run and 167 for the 53 UI run.
Median source-blit intervals are 513.138 ms, 1004.9135 ms and 1001.483 ms respectively.
These are source requests, not actual display timestamps or displayed FPS. More
directly, 56's visible Golf timer advances from 00:01 after the stock input to 00:21
at the 420-second capture, roughly 104 real seconds later. The game still runs
substantially slower than real time.

The audio telemetry records one actual shortage: 21.3 ms, one 1024-frame pass in
a 469-pass reporting interval. Its other pacing reports show no shortage, and no
post-mix output clamps are logged. The source statistic named `clipped` counts
values at or above 0.99 in magnitude; its 2423 near-full-scale samples with peak
0.997 do not prove hard output clipping. No listening or recorded-audio quality
review was performed.

The bounded test ends by terminating the app. All 56 backed-up runtime files,
configuration, library metadata, installation manifest and prior diagnostics
are restored; the app/helper are absent afterward. No successful guest exit,
legal tableau move, full gameplay, audio, save/load, background recovery or 60-FPS
acceptance is claimed. Private screenshots and raw logs stay local; their hashes
and the measured boundaries are in the
[separate physical-device record](evidence/ipad-solitaire-resolve-snapshot-depth-copy-20261009.json).

## Bounded iPad run of candidate 0057

The 57-patch HLE has a separate 720-second physical-iPad run with the retained
Build 47 host and exact `APS5_GPU_COLOR_SAMPLE_TILING=1`. Its preparation marker
is present, normal guest-memory tracking remains enabled, and all 56 backed-up
runtime files are restored afterward. This run also enables `APS5_PERF_REPORT=1`,
so its comparison with 56 includes an additional diagnostic cost.

The original screenshots show the selection menu at 60, 120 and 180 seconds.
Right followed by A enters a different board: thirteen four-card columns and
four foundation slots, rather than the prior Golf layout. The mode name is not
verified. Its captured board region remains identical through 720 seconds, and
the Y capture is byte-identical to the preceding 300-second screenshot. No legal
move or stock response is established. The timer remains 00:00; without a verified
action starting it, that does not measure simulation speed. Black results from an
inconsistent image preview were rejected by decoding the original PNG files;
they are not a device rendering regression.

Twelve stable diagnostic intervals contain 900 draws and 36 synchronous draws
each. Their median reported synchronous-draw mean is 256.215 ms. The corresponding
phase totals divided by synchronous count are 9.2736 ms for `readTarget`,
102.8819 ms for `prepare`, 106.2778 ms for `writeBack`, 1.5417 ms for pipeline work
and 34.6542 ms for `sync`. These aggregates include all draws in each interval.
They are not isolated per-frame timings, and the differing board mode and added
diagnostics prevent a matched gameplay or single-change speed claim.

The strict display parser rejects the complete log at line 37547; a second
interleaved display row occurs at 43047. Separate unchanged contiguous ranges
around those rows pass the same strict reader and report approximately 3.50–3.52
display completions per second at 768 × 432. No row was repaired and no ranges
were joined. The first range includes startup/menu and an input boundary; the
last shows the different card board without a verified legal action. This is
partial completion-timing evidence, not an accepted gameplay-FPS measurement.
The source trace separately records 568 present requests in its first 175 seconds,
with a median blit interval of 283.5755 ms. Requests do not establish displayed FPS.

Native memory reports range from 1262 to 3848 logged MB; the separate fast sampler
reaches 3916 MB. The shared pool remains jetsam-counted. Audio records no device
shortage, but ten overlapping stream reports contain nonzero deltas of the shared
output-clamp counter, up to 10318 samples in one report. Those deltas cannot be
summed as unique clamped samples. No listening review, stable-memory qualification,
legal card move, save/load, background recovery or 60-FPS acceptance is recorded.
The same app/helper processes survive both input sequences and are absent after
explicit termination and verified runtime/configuration restoration. See the
[separate physical-device record](evidence/ipad-solitaire-gpu-color-sample-tiling-20261009.json).

## Bounded iPad run of candidate 0058

The 58-patch HLE has a separate 720-second run with the retained Build 47 host,
normal memory tracking and exact `APS5_GPU_STENCIL_SAMPLE_TILING=1`. Both GPU
color and stencil transfer admission markers appear. The diagnostics and other
opt-ins from 57 remain enabled. Four separately guarded inputs use the visible
state after each action: Right250 selects Golf, A500 starts its complete seven by
five tableau, and Y500 followed later by Y250 changes the waste from 3♣ to 5♠
and then 4♠. Held buttons can repeat; exactly one drawn card per input is not
asserted. The timer reads 00:00 after start, 00:12 after the first stock input,
00:56 after the second, and 03:18 at the 720-second capture. No legal tableau move
was completed before the input cutoff.

Fourteen complete menu diagnostic intervals in native-clock seconds 35–175
contain 2426–2975 draws and 97–119 synchronous draws each. Their median reported
synchronous-draw mean is 70.2255 ms. Phase totals divided by synchronous count are:

| Diagnostic phase | 57 menu normalized median | 58 menu normalized median |
| --- | ---: | ---: |
| Read target | 9.2736 ms | 9.7765 ms |
| Prepare | 102.8819 ms | 4.1404 ms |
| Write back | 106.2778 ms | 7.1017 ms |
| Pipeline | 1.5417 ms | 1.5005 ms |
| Sync | 34.6542 ms | 46.0480 ms |

The large remaining aggregate is synchronization. These totals cover all draws;
normalizing them does not isolate per-frame work. Later Golf intervals have a
70.0905 ms median synchronous mean. Inputs occur later than in 57 and enter a
different game mode; deals, thermal repetitions and legal gameplay routes are not
matched. These observations do not accept a single-change performance result.

The complete log still fails the unchanged strict display parser at line 40195;
other malformed display rows occur at 44221 and 54214. Separate untouched
contiguous ranges report approximately 9.44–9.77 display completions per second.
An uninterrupted original range during late Golf-board captures reports 942
completions over 99.3006 seconds, or 9.4863 per second. Its p50, p95 and p99 all
overflow the histogram above 25.6 ms. No malformed rows were repaired or ranges
joined, and whole-log acceptance remains rejected. The first 175 source-trace
seconds separately contain 1633 present requests with a 101.709 ms median blit
interval, compared with 568 requests and 283.5755 ms for 57. Source requests are
not displayed FPS; completion telemetry also does not prove unique correct game
images or simulation speed.

Native physical-footprint reports span 1281–3973 logged MB, with a separate
footprint maximum of 4048 MB. Two nearby per-stream audio reports each contain a
21.3 ms shortage in one 1024-frame pass; their unique physical output duration is
not established. All reported output clamps are zero, and the largest source
peak is 0.349. No listening review accepts sound quality. There are no logged
skipped draws, AGC graphics rejections, `bad_alloc`, `FATAL` or `VK_ERROR` markers.
Throttled native fault diagnostics remain and are not treated as terminal errors.

The same main app survives all four input actions and cleanup-before. The JIT
helper has already detached and is absent by the start-game receipt; continuity
of both processes is not claimed. Explicit app termination ends the bounded run.
Independent hashes verify all 56 restored runtime files and exact configuration,
library and installation-manifest bytes. Prior diagnostics have successful copy
receipts without an additional byte readback. Full gameplay, a legal tableau
move, audio, save/load, background recovery, stable memory and constant 60 FPS
remain unqualified. See the
[separate physical-device record](evidence/ipad-solitaire-gpu-stencil-sample-tiling-20261009.json).

## Build 48 telemetry and a verified Golf move

The next bounded 720-second run uses the same 58-patch HLE and launch options
with the Build 48 host. Its native display telemetry emits each complete row
in one write. The unchanged strict parser accepts all 682 rows without repairs.
After a 60-second warmup, it reports 6198 completion intervals over 646.6213
seconds, or 9.5852 native display completions per second, with 32599 missed
vblanks at 768 × 432. The p50, p95 and p99 exceed the histogram's 25.6 ms upper
range. A late Golf-board window reports 1149 completions over 118.6507 seconds,
or 9.6839 per second. These are completion timestamps; they do not identify
unique correct physical display images or establish simulation cadence.

Six separately guarded actions preserve the main process. Right250 selects
Golf, A500 starts its seven by five tableau, and another Right250 selects the
visible Q♠ in column 1, using zero-based column indices. A500 then removes Q♠
onto the K♣ waste and exposes 5♦. Original before/after screenshots independently
verify this first legal tableau move. Two later Y250 inputs change the waste
from Q♠ to 10♣ and then A♣. The final capture retains the changed board and reads
05:26. A queued B request is refused at 594.539 seconds outside the bounded
input window; no Back or return-to-menu behavior is accepted.

Fourteen menu diagnostic intervals have a 69.3235 ms median synchronous-draw
mean; thirteen late Golf intervals have 69.881 ms. The late phase totals divided
by synchronous count are 9.4928 ms for target reads, 3.9845 ms for preparation,
7.2449 ms for writeback, 1.5701 ms for pipelines and 46.1 ms for synchronization.
These totals include all draws and do not isolate individual frame costs.
The source trace records 6811 present requests and 6812 blits; its first 175
seconds contain 1664 requests with a 100.2195 ms median blit interval. Source
requests do not establish displayed FPS. Different deals and input schedules
prevent a matched performance claim against the preceding run.

The audio log contains one per-stream 21.3 ms shortage in one 1024-frame pass.
It also contains 58 nonzero reports of the shared output-clamp counter, with a
maximum delta of 3283 samples. Their overlapping per-stream windows cannot be
summed as unique clamped output. Sampled source peaks reach 1.502. The source
field named `clipped` counts magnitudes of at least 0.99; it is distinct from
the output callback's actual clamp to ±1. No recording or listening review
accepts sound quality. Native physical-footprint reports span 1264–3963 logged
MB, with a separate footprint maximum of 4031 MB; stable memory remains
unqualified. No skipped-draw, AGC graphics rejection, `bad_alloc`, `FATAL` or
`VK_ERROR` marker appears. The 206 throttled native `UNHANDLED` reports are not
treated as terminal failures.

Main process 8044 remains present through all six actions and cleanup-before;
the already detached JIT helper is absent by the move receipt. Explicit
termination ends the run. All 56 runtime files and configuration, library and
installation-manifest bytes are independently verified after rollback. The
native Build 48 host remains installed; this rollback restores the prior runtime
data, not Build 47. The immutable host-build record retains its earlier
pre-installation status. One legal move and two stock responses are accepted;
continuous gameplay, audio, save/load, background recovery, stable memory and
constant 60 FPS remain open. See the
[separate Build 48 device record](evidence/ipad-solitaire-build48-whole-row-telemetry-20261009.json).

## Bounded iPad run of candidate 0059

A separate 360-second run uses the retained Build 48 host and the 59-patch HLE
with exact `APS5_CACHE_STENCIL_TRANSFER_PIPELINES=1`. The production path logs
reuse of an immutable stencil bundle after matching its ordered sample positions.
The menu and complete Golf board render, but this run sends only Right250 to
select Golf and A500 to start it. The final board has A♠ waste and remains at
00:00; no legal tableau move or stock action was attempted. This idle-board timer
does not measure simulation speed.

All 337 display rows pass the unchanged strict parser without repairs. After
60 seconds of warmup, it reports 2784 completion intervals over 284.8849 seconds,
or 9.7724 native display completions per second, and 14309 missed vblanks at
768 × 432. The p50, p95 and p99 exceed 25.6 ms. Native completion timestamps
do not establish unique physical images, gameplay cadence or constant 60 FPS.
Twelve late idle-board diagnostic intervals have a 68.6725 ms median
synchronous-draw mean. Their normalized phase totals are 8.8803 ms for target
reads, 3.4342 ms for preparation, 7.1703 ms for writeback, 1.5586 ms for pipelines
and 46.1162 ms for synchronization. These totals cover all draws. The changed
deal, earlier inputs and shorter run prevent a matched speed comparison with 58.

Seventy audio pacing reports contain no logged shortage or output-clamp delta;
sampled source peaks reach 0.467. No listening or output recording accepts sound
quality, and this is not the preceding legal-move scene. Native physical-footprint
reports span 1260–3991 logged MB; the separate footprint maximum is 4065 MB.
There are no skipped-draw, AGC graphics rejection, `bad_alloc`, `FATAL` or
`VK_ERROR` markers. Both main process 8134 and helper 8139 remain present through
the two actions and cleanup-before. Explicit termination ends the run; all 56
runtime files and configuration, library and manifest bytes match the backups,
and both processes are absent afterward. The native Build 48 host remains.
Cache hit rate, measured speed, legal gameplay, audio, save/load, background
recovery, stable memory and 60 FPS remain unqualified by this run. See the
[separate 59 device record](evidence/ipad-solitaire-stencil-transfer-program-cache-20261009.json).

## First bounded iPad run of candidate 0060

The first 720-second run retains native Build 48 and temporarily loads the
60-patch HLE. Its launch environment matches 59 with the addition of exact
`APS5_ZERO_STENCIL_INVARIANT=1`. The actual production path reports admission
of the zero stencil snapshot and a stencil-only clear with the draw retained.
Normal memory tracking, all eight logical samples, the 1920 × 1080 source,
fixed-function interpolation and the three-image 768 × 432 swapchain remain.
This marker establishes that the path was entered, without counting every
admission or qualifying all subsequent rendering.

The complete Canfield menu and touch controls are visible at 60 seconds. The
requested Right250 action fails while XCTest initializes UI automation, with
exit 65 and a timeout enabling automation mode. Its captured screen, and the
120- and 175-second captures, show the system's Touch ID authorization dialog.
No actual Right input or visible game response is accepted. The dialog is gone
by 180 seconds, and the 720-second screen still shows Canfield selected. This
run does not start Golf or verify a legal card move, stock input or undo.

All 604 display rows in the closed, unchanged log pass the strict parser.
Keeping its active epochs separate, the 60-second warmup leaves 285 completion
intervals over 24.9838 seconds in epoch 2 and 5544 over 483.9886 seconds in
epoch 4. These correspond to 11.4074 and 11.4548 native display completions per
second; the first segment fails the 30-second duration threshold. Their
missed-vblank deltas are 1214 and 23495, and all histogram percentiles exceed
25.6 ms. At the last native report, cumulative counters contain 50520 submits,
7221 presents, 7222 acquires and 7217 swapchain completion records. These are
API diagnostics, not unique correct physical game images or accepted gameplay
FPS. The system interruption, idle menu and unmatched scene prevent a speed
comparison or constant-60-FPS claim.

Native physical-footprint reports span 1241–4000 logged MB; the separate
footprint peak reaches 4080 MB. All 142 audio pacing reports contain zero logged
device shortage and output-clamp delta, with sampled source peaks up to 0.275.
There is no recording or listening acceptance. No skipped-draw, AGC graphics
rejection, `bad_alloc`, `FATAL` or `VK_ERROR` marker appears; 207 throttled native
`UNHANDLED` diagnostics are not treated as terminal failures. These observations
do not qualify stable memory or audio quality.

Main process 8218 and helper 8220 remain present around the failed action and
cleanup-before. After explicit termination, both are absent. Independent
readbacks verify all 56 installed candidate files against its manifest, then
verify that all 56 restored runtime files, configuration, library and manifest
match the backups. The seven prior diagnostic files also match independent
restored hashes. The native Build 48 host remains installed. This first run is
preserved separately from subsequent authorized-control tests, with no gameplay,
audio, save/load, background recovery, stable-memory or performance acceptance.
See the [separate first 60 device record](evidence/ipad-solitaire-zero-stencil-invariant-first-run-20261009.json).

## Authorized controls with candidate0060: 2026-10-09

After the user confirmed iPadOS UI Automation with Touch ID, a new bounded
720-second run kept the same native Build48 and source60 HLE. The actual
zero-stencil clear is logged. Seven separately captured actions select Golf,
start its complete board, draw K♦ → 9♥ → A♥, select 2♣, move it onto A♥
exposing 9♣, then undo with B. The main process stays alive; the helper exits
during the second stock action. The final capture retains the full undo-restored
board with timer04:49. B is undo here, not a return to the library.

All 678 complete display rows pass the unchanged strict reader. After per-epoch
warmup, 7,858 completion intervals over 700.226861 seconds yield 11.222077
completion intervals/s, with 34,159 missed-vblank reports. p50/p95/p99 lie in
the unbounded >25.6-ms histogram bin. This mixed menu/deal/input/undo route and
different deal do not establish a matched speed gain or unique displayed game
FPS. Native footprint peaks at 3,916 MiB; this short run does not qualify a
long-session resource trend. Audio pacing reports no short callbacks, but four
overlapping rows report clamping. Neither counters nor these captures establish
audible quality, save/load, background recovery, complete gameplay or 60 FPS.

All 56 actual original runtime files, configuration/library/manifest bytes and
seven prior diagnostic hashes restore; the own app processes are absent and the
lease is released. The candidate remains temporary. See the [separate source60
controls/device record](evidence/ipad-solitaire-zero-stencil-invariant-controls-20261009.json).

## Legacy AudioOut ingress and pixel-owned color transfer

AnyPS5 patch0062 is a bounded diagnostic, enabled only by exact
`env.APS5_TRACE_AUDIOOUT_INGRESS = 1`. Optional
`env.APS5_AUDIOOUT_INGRESS_SKIP_FRAMES` accepts strict decimal 0 through
28,800,000; the default is zero. `env.APS5_AUDIOOUT_INGRESS_DIRECTORY` chooses
the private Wine export parent; the default is `C:/anyps5-audio-captures`.
Each run creates a unique directory with completed port-generation JSON files.
Raw-before-processing and queued-after-wait fingerprints have separate formats,
frame phases and ordinals. They retain no PCM and cannot prove byte identity,
clock alignment or the origin of an audio defect.

The capture cap is 480,000 frames per side, ten rendered seconds only at 48 kHz.
The common worker deadline is initialization plus `floor(skip/48000) + 45`
wall seconds. The reviewed long diagnostic requires successful completed exports
and the actual `[audioout-ingress] closed` marker before forced process cleanup.
General DLL unload remains unqualified because detach must not join a live
worker under the loader lock. Disable this diagnostic for benchmarks.
[Source checks](evidence/audioout-ingress-trace-focused-20261009.json).

A bounded twelve-minute native50/source63 device run completes both exports
and logs worker closure before cleanup. The main legacy port captures 1,875
256-frame calls on each side. Incoming fingerprints already recur every four
calls; four same-call input/queued fingerprints differ without conversion or
an intermediate copy. The native postmix has five byte-distinct 1,024-frame
patterns, including 7.808 rendered seconds of exact consecutive repetition.
The whole ten-second window is not byte-periodic. The separately opened
background port makes no calls before its deadline. These observations narrow
the investigation to the producer and mutable input but do not identify the
cause or align the three clocks. Later clipping remains visible outside the
captured window. Stock draws and selection respond; this run verifies no legal
move or undo. All original runtime files and metadata restore, and native48 is
reinstalled. A brief own-process suspension obtains the growing log; this is
diagnostic evidence, not a performance/lifecycle comparison.
[Separate device result](evidence/ipad-solitaire-audioout-ingress-20261009.json).

Patch0063 is enabled only by exact
`env.APS5_GPU_COLOR_SAMPLE_PIXEL_OWNED = 1`, together with the admitted GPU color
tiling path. The variant is immutable per tiler owner; start a fresh app process
for each off/on comparison. The new shader transfers all eight sample words
from one XY invocation with Z=1. Disabled eight-sample and all two/four-sample
paths use the original shader. It changes no resolution, interpolation, sample
count, synchronization or guest-memory tracking.

Global configuration can be overridden by per-game configuration and the
Windows environment. A generic sample-tiling marker proves neither the new
module nor Z=1. Record actual variant selection in a separate diagnostic, then
compare matched scenes with capture/profiling disabled. The native Mac byte
checks and timestamp chain are not iPad FPS evidence.
[Native qualification](evidence/color-sample-pixel-owned-native-20261009.json),
[combined host build](evidence/pixel-owned-and-audio-ingress-build-20261009.json).

A separate enabled native48/source63 iPad diagnostic visibly selects Golf and
opens its full board through two verified touch inputs. Its fresh native
pipeline cache is completely decoded and contains the expected module size/hash
key and converted Metal kernel with the XY/eight-sample loop. Raw shader dumps
remain absent; a cache key is not collision-free raw SPIR-V identity or GPU
completion evidence. The original runtime, metadata, diagnostics and cache are
restored afterward. This captured/profiled run establishes no card move, audio
quality or matched off/on performance gain; keep the variant default-off until
a separate comparison qualifies it.
[Enabled device checkpoint](evidence/ipad-solitaire-pixel-owned-controls-20261009.json).
