# Patch status

The current FEX stack is consolidated into two release-based patches:
[0000 iOS port and 0001 allocator](../patches/fex/README.md), applied to official
FEX-2610. Historical patch numbers/results below describe their original builds.
Madeira0046 adapts the native bridge to FEX's new API; Madeira0047 preserves the
native process-name lifetime after an embedded Wine session. The latest AnyPS5
pin retains all 33 reconciled local patches. Patches0034–0039 add multisample
layouts, transfers, resource allocation and shader eligibility.
Patches0040–0044 connect an experimental grouped-sample renderer in source.
It is default-off (`APS5_ENABLE_SAMPLE_GROUPS=1` opts in). The full 50-PRX HLE
package is built and its 22 selected Windows host test suites pass under desktop
Wine; the fresh Solitaire candidate resolves all 47 explicit HLE dependencies.
At the separately recorded as-built 0044 checkpoint, all 44 patches apply from
the pin, match 125 checked source paths and reverse cleanly; their hashes match
the build provenance. That record does not include later patches.
Patch0045 repairs retained guest-export lifecycle ownership; its freshly prepared
device package now starts through built-in JIT and reaches the Vulkan swapchain.
That earlier game surface stays white with two distinct graphics rejections.
Patch0046 adds canonical internal rectangle-stage validation and has static,
sanitizer and syntax checks; its actual-resource and full graphics host suites
subsequently pass.
Patch0047's restricted fixed-function color-resolve implementation is now built.
An isolated production-fragment/rectangle GPU probe passes six cases on the iPad;
this is separate from complete guest renderer qualification. The earlier 47-patch
series matches 137 paths and applies/reverses cleanly. Its 50-PRX/three-DLL HLE
archive is verified, and all 22 selected host suites pass. The fresh prepared
game package has run on the iPad: the earlier guard messages are absent, but
one unknown-SPIR-V-type rejection remains and the surface stays white.
Correct connected rendering remains unqualified.
Patch0048 repairs the opaque image/sampler type inspector while retaining
descriptor-kind, image-shape and fixed-array-count checks. Its sanitizer fixtures,
48-patch/137-path source verification, new 50-PRX/three-DLL HLE build and host
suites pass. The latest game device run has no logged skipped draw or AGC error,
but the reviewed surface remains white; correct game frames are unqualified.
The earlier 0044/0045/0047 records remain unchanged.
Two later patch0048 runs compare the pre-existing selected-source GPU capture
with resident presentation enabled and with the diagnostic guest-memory source.
Every decoded pixel in all four scaled captures is white in both paths; this
localizes the white result before final presentation without qualifying full
source or final swapchain pixels. Patch0049 adds default-off bounded pixel
statistics. Its native fixtures, 49-patch/138-path source verification,
50-PRX/three-DLL build and 22 selected host suites pass; no device acceptance
is established by its source/build record. A separately recorded patch0049
device run observes a sampled MSAA producer change and identical resolve-source
statistics. Its selected scaled output stays white; resolve destination pixels
on that baseline and correct game rendering remain unqualified. Later same-package
target-copy, synchronous-draw and transition controls change output to blue or an
early green background. Their perturbing copied-target readback has matching
resolve/display logical colors, without qualifying a default repair.
Extending the transition-only control produces the full title/selection menu
by the later source and physical captures; interaction and gameplay remain
unqualified, with a matched long baseline still outstanding.
Patch0050 adds a default-off owned-initial-descriptor-write experiment. Its
source/full build/host checks pass, but an exercised long device run stays white;
no default repair is accepted. Its 50-patch series matches 139 paths.
Patch0051's separate default-off close-draw-pass control also remains white
after 180 seconds despite verified exercise, build and host checks.
Patch0052 dispatches color tiling/detiling to literal-size texel copies; native
and full HLE/host checks pass, and its 52-patch series matches 139 paths. A
synchronized controlled iPad run visibly responds to Right/A, enters Golf and
deals the board. Individual card actions, complete gameplay and sustained FPS
remain unqualified; no default-path repair is accepted.
Patch0053 adds default-off scoped bulk driver copies and unconditional repairs
to existing HostWrite failure/identity handling. Its focused Windows fixtures,
53-patch/141-path series, full HLE build and host/graphics suites pass. A separate
controlled iPad run exercises a bulk scope and shows Golf's waste changing after
Y. Bulk-specific speed improvement, full gameplay and concurrent foreign CPU-write
attribution remain unqualified.
Patches0054–0056 add an opt-in exact-SPIR-V pipeline identity for internal resolve
shaders, an opt-in owned raw tiled resolve snapshot, and fixed-size depth/stencil
texel copies. The complete 56-patch series matches 142 paths; the full HLE rebuild
and 22 host plus two graphics suites pass. Native correctness and object-code
checks have separate scopes. Device behavior and speed require separate evidence.
Patch0057 adds default-off GPU conversion for the qualified RGBA8 multisample
layout. Its 57-patch/146-path source series, native Mac GPU helper/image-transfer
probe and actual Windows write-tracking regression pass. The complete HLE rebuild
contains 53 verified binaries; 22 CPU/API suites, the actual CMake tracking target,
resource classification and full Graphics suite pass. Physical iPad behavior and
performance remain separate checks.
See the
[integration evidence](evidence/fex-2610-integration-20261008.json) for separate
source, build, device and unqualified-path states.

## Current MSAA integration checkpoint: 2026-10-09

| Patch | Implemented behavior | Qualification |
|---|---|---|
| 0034 | Stored color sample coordinates and full-coordinate pipe XOR | Independent AMD layout comparison under sanitizers. |
| 0035 | Color sample/fragment decoding and CPU transfers | Sanitizer tests with mapped-memory adapter; focused real GuestMemory PE under desktop Wine. |
| 0036 | Raw D16/D32/S8 sample memory layouts | Independent AMD comparison: 60 layouts and 34,817,526 addresses. Patch0043 connects S8 transfers; guest Z import/export remains unsupported. |
| 0037 | Production color image owner with grouped sample layers/views; device feature selection | Actual owner passes both synthetic/captured position patterns on iPad. Independent probe pipelines/descriptors; correct connected game rendering remains unqualified. |
| 0038 | Conservative static eligibility prerequisite for repeated VS/PS draws | 31 sanitizer cases with validated synthetic shaders. Patch0043 requires this check and rejects dynamic attachment/input and output/output aliases. |
| 0039 | Grouped depth/stencil allocation, clear, cache identity and rollback | Production command tests with explicit Vulkan/scheduling mocks. Guest depth consumption remains blocked; the later independent S8 probe preserves synthetic depth. |
| 0040 | Logical/native sample state, guest positions, per-group pipelines and cache identity | 46 sanitizer state/pipeline cases, including five portability-feature rejection cases added in patch0043. Multisample depth/stencil admission remains restricted to the proven always-pass stencil initializer. |
| 0041 | Exact RGBA8 sample upload and compute readback | Three layout contracts and 89 rejection/unwind cases under sanitizers; independent iPad production-transfer probe passes 9,044 sample words. |
| 0042 | Guest MSAA texture decoding, logical/native shader routing and cache keys | 28 production SPIR-V fixtures independently validated; emitted arithmetic checks 84 distinct sample addresses. Connected shader fixtures have not executed on the iPad GPU. |
| 0043 | Synchronous grouped draws, fresh color/S8 snapshots, shader resources, guest writeback and portability guards | Full 50-PRX local HLE build and 22 selected Wine host test suites pass. Fresh Solitaire candidate passes its 47-dependency HLE audit; device rendering and performance remain unqualified. |
| 0044 | Exact S8 bitplane upload and packed compute readback, preserving combined depth | Six layout contracts and 234 rejection/unwind cases under sanitizers; independent iPad probe passes 9,044 S8 bytes and 9,044 constant-depth checks within 0.000001 of 0.625. |
| 0045 | Preserve guest init/fini when defined, visible, non-absolute exports remain reachable beside an HLE replacement | 32 host-tool cases, 52 ELF/PE conversions and 25 Wine PE executions pass. A fresh iPad package reaches Vulkan and a three-image swapchain without a logged `bad_alloc`; graphics remain white. |
| 0046 | Qualify exact factory-generated rectangle stages for grouped draws; distinguish host fault completion from guest writes | Three canonical accepts and 90 rejection cases pass under sanitizers; 24 SPIR-V modules validate independently, existing 31 pair cases and production syntax checks pass. Actual-resource and full graphics host suites pass. Connected game output remains unqualified. |
| 0047 | Restricted GC10 fixed-function RGBA8 color resolve from 2/4/8 stored samples to one sample, preserving actual rectangle coverage | 144 native sanitized cases and 401,712 scalar channel values pass. Isolated iPad production-fragment/rectangle probe passes six cases and guest exit 0, with exact non-tie/preserved bytes and one-unit half-tie tolerance. The game attempts the production path but rejects an unknown SPIR-V type; correct complete rendering remains unqualified. |
| 0048 | Register opaque image/sampler types and validate separate descriptor roles, image shape and one fixed-array count | Ten accepted and 56 rejected production-inspector cases pass ASan/UBSan; six original and 18 factory SPIR-V modules validate. Full HLE build, 22 selected host suites and resource/full graphics suites pass. Latest iPad game log has no skipped draw/AGC error; reviewed surface remains white and correct game frames are unqualified. |
| 0049 | Default-off bounded statistics over existing producer/resolve buffers, including every logical sample at selected XY positions | ASan/UBSan statistics fixtures pass with diagnostics unset, zero and enabled. Full 50-PRX/three-DLL build, 22 selected host suites and 49-patch/138-path forward/reverse source verification pass. A separate iPad run observes a sampled producer write and matching resolve-source statistics; the selected scaled output remains white. Resolve destination and correct game frames are unqualified. |
| 0050 | Default-off replay of owned initial descriptor writes into the fresh DrawBindings set, preserving read-only snapshot overrides | Four sanitizer configurations pass 64 metadata checks each. Full HLE build, 22 host suites, resource/full graphics suites and 50-patch/139-path source verification pass. Device replay is logged; five scaled captures and the nominal 180-second image remain white. Negative experiment, no repair accepted. |
| 0051 | Default-off close-after-draw control, retaining GENERAL layouts, read-only snapshots and asynchronous batches | Production Windows syntax, 51-patch/139-path series, full HLE and host/graphics checks pass. One device marker proves exercise; five source captures and nominal 180-second physical image stay white. Negative experiment, default remains off. |
| 0052 | Dispatch once per color Tile/Detile surface to literal-size 1/2/4/8/16-byte texel copies | Independent AMD layout/address oracle, original layout suite, ASan/UBSan and optimized MinGW negative-control checks pass. Full 52-patch/139-path series, 53 binary hashes and 22 host plus two graphics suites pass. Controlled device run visibly responds to Right/A, enters Golf and deals its board; card actions/full gameplay/FPS remain unqualified. |
| 0053 | Opt-in full-copy HostWrite scopes retained by registered RW mapping leases; unconditional transactional HostWrite failure and identity repairs | Real shared mappings/tracker/native-copy fixture passes four exact-option runs and seven scope controls each; eligible copy faults fall from four to zero. Full 53-patch/141-path series, 53 binary hashes, 22 host plus two graphics suites pass. Device marker confirms exercise; Golf's waste changes after Y. Bulk remains default-off; bulk-specific speed, full gameplay and concurrent foreign CPU attribution unqualified. |
| 0054 | Opt-in pipeline-only identity for internal fixed-function resolve shaders, keyed by exact generated fragment and canonical rectangle SPIR-V | Four sanitizer configurations pass; exact-one run checks 3,273 assertions with counted Vulkan mocks. Ordinary unknown shaders and resource/recipe cache gates remain unchanged. Full combined 56-patch HLE/host build passes; no measured device speed gain. |
| 0055 | Opt-in owned raw tiled RGBA8 resolve-source snapshot, retaining pending flushes, complete range checks and post-copy write stamps | Four sanitizer configurations pass 12 cases each; exact-one run checks 198 assertions. Actual snapshot/layout code is tested with explicit flush/stamp mocks, not actual GPU alias or recycled-address tracking. Full combined HLE/host build passes; default remains off. |
| 0056 | Dispatch once per depth/stencil Tile/Detile surface to literal-size raw 1/2/4-byte copies | Candidate and prior source pass 84 independent AMD layouts, 35,028,036 address comparisons and 48 guarded misaligned round trips. Optimized actual MinGW TU has zero hot CRT copy calls versus two prior calls. Full 56-patch/142-path series, 53 binary hashes and 22 host plus two graphics suites pass; no additional depth feature or accepted speed gain. |
| 0057 | Opt-in GPU detile/retile for exact uncompressed RGBA8 R64KB_X 2/4/8-sample targets, retaining the synchronous draw and changed-byte guest commit | Native contract checks and 99 compute plus three production image-transfer chains pass on the Mac GPU against independent AMD bytes/padding. Windows shared-alias commit checks pass with four bulk-option settings. The 57-patch/146-path series and full HLE build pass, including 22 CPU/API and three additional runtime suites. Device/FPS qualification remains separate. [Native evidence](evidence/gpu-color-sample-tiling-native-20261009.json), [build evidence](evidence/gpu-color-sample-tiling-build-20261009.json). |
| 0058 | Opt-in GPU detile/retile for qualified S8 SW_64KB_Z_X eight-sample planes, preserving the original tiled seed and changed-byte commit | Eight-path round trip, 28 contract controls, 42 native compute and six D32S8 image chains pass. The 58-patch/150-path series, full HLE build with 53 verified binaries, 22 CPU/API and three additional runtime suites pass; private preparation verifies all 137 files. Actual Draw/DepthSurface, iPad and performance qualification remain separate. [Native evidence](evidence/gpu-stencil-sample-tiling-native-20261009.json), [build evidence](evidence/gpu-stencil-sample-tiling-build-20261009.json). |
| 0059 | Opt-in per-device immutable stencil transfer program cache, keyed by format/counts and full ordered bit-exact sample positions | Four-path source comparison, cache/legacy contracts and six native D32S8 chains pass. The full 59-patch HLE, 22 CPU/API and three additional runtime suites pass; 137 private preparation files are verified. A bounded iPad run logs real program reuse and renders the full Golf board; cache hit rate, speed and gameplay/FPS remain unqualified. [Source/native evidence](evidence/stencil-transfer-program-cache-native-20261009.json), [build checkpoint](evidence/stencil-transfer-program-cache-build-20261009.json), [device record](evidence/ipad-solitaire-stencil-transfer-program-cache-20261009.json). |
| 0060 | Opt-in zero-invariant D32S8 path: per-draw stencil-only clear replaces transfers when the entire eight-sample S8 snapshot remains provably zero | Source/native contracts and independent review pass. The complete 60-patch/152-path series, full HLE with 53 verified binaries, 22 CPU/API and three additional runtime suites pass; 137 private preparation files and dependency closure are independently verified. A bounded iPad run exercises the real clear and visibly verifies a legal Golf move and undo. Speed, audio and complete gameplay remain unqualified. [Source/native evidence](evidence/zero-stencil-invariant-native-20261009.json), [build checkpoint](evidence/zero-stencil-invariant-build-20261009.json), [device controls](evidence/ipad-solitaire-zero-stencil-invariant-controls-20261009.json). |
| 0061 | Opt-in immutable coherent color baseline and full GPU output seed, removing one CPU snapshot copy while preserving padding/fence/alias commits | Helper contracts, 60 native raster comparisons, 15 omitted-copy negatives and four Windows guest-memory modes pass. Full 61-patch/154-path HLE and 25 host suites pass. iPad stock draw, undo and selection respond; two A attempts produce no visible tableau change. No legal move or speed/audio acceptance in this run. [Focused qualification](evidence/color-sample-staging-copy-native-20261009.json), [build checkpoint](evidence/color-sample-staging-copy-build-20261009.json), [device result](evidence/ipad-solitaire-color-sample-staging-copy-controls-20261009.json). |

## White selected-source comparison: patch0048

Two completed runs use the same prepared manifest and HLE archive. The resident
source trace contains 26 present events, 27 blits and 95 accepted draw rows over
about 55 seconds; its selected-source dump is at present ordinal 16. Disabling
resident presentation for the isolated diagnostic selects guest memory instead:
that trace contains 25 presents, 26 blits and 95 accepted draw rows, with its
last present at 54.63 seconds. Captures occur at present ordinals 8, 16 and 24.
These event counts are not completed GPU work or displayed-frame/FPS counters.

All four BMPs decode to 480 × 270, down from the selected 1920 × 1080 source
with scale divisor four. Every one of each capture's 129,600 BGRA pixels is
`ffffff00`. The complete BMP hash is
`634b8d6d1a934faeda4027c2f69f5708d40a49ff00d8d315249b6741a79112d6`;
the distinct pixel-payload hash is
`cf02792e813ffd6ccb16b69df2d4693e2bfc037f3ce07f68797d609d31ad8ec1`.
Configuration, library and manifest bytes are restored in both runs, and the
receipts verify runtime rollback.

The captured scaled selected source is already white in both paths. A cause
confined solely to the resident-present cache, later GDI compositing or final
Metal presentation does not explain all these observations. Earlier rendering,
resolve contents and memory coherency remain possible causes. Full-resolution
source and final swapchain pixels were not exhaustively read back. The diagnostic
memory-source switch is not a production repair. Only statistics and hashes are
published; private captures remain local. See the
[comparison record](evidence/ipad-solitaire-white-source-comparison-20261008.json).

## Bounded pixel diagnostics: patch0049 source/build checkpoint

`APS5_FRAME_PIXEL_DIAGNOSTICS` defaults off when unset, empty or zero. When
enabled, it selects event ordinals 1, 2, 3, 8, 16, 32, 64 and 128 independently
for each stage. Each event samples at most 1,024 deterministic XY positions and
includes every logical sample at those positions. Statistics report RGB
black/white, alpha state, distinct RGB/RGBA counts and aggregate/per-sample FNV64.
No raw pixel files are produced.

Producer input uses the existing flushed color transfer after fast-clear handling;
output uses the existing download after GPU completion and resource fault checks,
before guest writeback. Resolve source statistics use the existing linear source.
The returned-Draw marker is not proof of GPU completion and does not read the
resolve destination. A presentation helper exists but is not integrated into
presentation by this patch. There are no added GPU readbacks, recorder flushes
or GPU waits; diagnostics still add bounded CPU work when enabled.

Native sanitizer fixtures pass 487 assertions with the variable unset, 487 with
zero and 511 when enabled. They cover alpha-independent RGB classification,
all logical samples, independent byte hashes, corner coverage, invalid spans
and the event budget. These are subset statistics: a uniform sample does not
prove a uniform full image, and an intended stencil initializer can render white.
The full HLE archive verifies all 53 binaries, and its 22 selected host suites
pass under local Wine. All 49 patches match 138 source paths and apply/reverse
cleanly. The source/build record establishes no device pixel, gameplay or FPS
result. See the
[native statistics qualification](evidence/bounded-frame-pixel-diagnostics-native-20261008.json)
and [HLE/source build checkpoint](evidence/bounded-frame-pixel-diagnostics-build-20261008.json).

## Bounded pixel diagnostics: patch0049 device checkpoint

A completed run of the freshly prepared patch0049 package reaches built-in JIT,
the Apple M2 Vulkan device and a three-image 768 × 432 swapchain. Its 25,056-line
log has no skipped-draw, AGC-error, `bad_alloc` or `FATAL` markers. All 56
read-back runtime files match the candidate manifest. The earlier source/build
record remains an independent checkpoint.

At producer event 1, all 8,192 inspected sample texels are initially black with
zero alpha. After the existing completed GPU transfer, their statistics report
one non-black, non-white RGB value with zero alpha. Every logical sample has the
same per-sample FNV64, `1fb732bb8622a325`. Events 2, 3, 8 and 16 have matching
before/after subset statistics; the resolve source matches the producer output
at each of the five sampled events. This establishes an observed change and
matching source values at the inspected positions. It does not establish the
intended game image, full-image uniformity or completed resolve destination.

The logged requested state uses a three-index rectangle list, all eight logical
samples, RGBA writes, full-target viewport/scissor, no culling and an
always/replace-zero stencil initializer. These fields do not prove full rectangle
coverage or the final masked Vulkan pipeline state. The resolve-return marker
does not inspect destination pixels or assert GPU completion.

The selected-source trace contains 26 present events, 27 blits and 95 accepted
draw rows, with its last present at 55.26 seconds. The three fresh scaled BMPs
at present ordinals 8, 16 and 24 again decode to 480 × 270: every one of each
capture's 129,600 pixels is white RGB with zero alpha. Their BMP and payload
hashes match the earlier source-comparison captures. Review of the 60-second
screenshot confirms a white game surface, black letterboxing and touch controls.
None of these trace rows is a displayed-frame or FPS measurement.

The baseline resolve destination remains unmeasured. The resolve and its subsequent
presentation consumer must therefore be checked separately before assigning a
cause. The early `Wine finished after 23.2s` message is followed by 8,376 runtime
log lines and is not an explicit guest exit. Runtime backups, configuration,
library and original manifest restore byte-identically; prior diagnostics are
also restored. Correct rendering, gameplay, audio, input response, save/load and
performance remain unqualified. Only sanitized statistics and hashes are public;
see the [completed device record](evidence/ipad-solitaire-bounded-pixel-diagnostics-20261008.json).

Later controls use the identical package. Copying targets per draw and
independently forcing synchronous draws produce uniform blue selected-source
captures. Per-draw transitions briefly expose an opaque green radial background,
then blue. A separate copied-target run with GPU target dumps checks every pixel
of the first resolve/display snapshots and finds matching logical colors after
RGBA/BGRA decoding; that readback perturbs execution and does not qualify the
white baseline's destination. These controls narrow the recorded runtime path,
with no default fix, playable menu or performance result. See the
[separate controlled-path record](evidence/ipad-solitaire-resident-path-controls-20261008.json).

A longer transition-only control observes the full title/selection menu in the
scaled source at 163.19 seconds and in the physical nominal 180-second screenshot.
Earlier source captures through 130.25 seconds stay blue. The trace changes target
and draw patterns after about 131 seconds and continues to 175.61 seconds, without
a logged skipped draw, AGC error, `bad_alloc` or `FATAL`. The early helper-end
message is followed by 20,839 runtime log lines; runtime and metadata restore.
This is a visible menu observation, with no interaction, gameplay or FPS claim.
A matched long baseline is needed before assigning the progress solely to the
transition switch. See the new [long menu checkpoint](evidence/ipad-solitaire-transition-menu-20261008.json);
the previous short-run records remain unchanged.

An independent original descriptor/layout GPU probe passes eight synthetic cases
and 10,336 exact channels through Wine/FEX/MoltenVK. Original/copy/copy-plus-SSBO-
override/explicit-rewrite cases cover two destination layouts. This ordinary
texture2D/separate-nearest-sampler/readonly-SSBO program does not execute AnyPS5
Draw/Recorder or independently prove sampler-handle identity. It logs guest exit
0 before a native Mach exception during JIT detach; clean native app exit is
unqualified. It does not establish a production descriptor fault or repair. See
the [separate probe record](evidence/ipad-descriptor-copy-probe-20261008.json).

## Explicit initial-descriptor experiment: patch0050

`APS5_EXPLICIT_DRAW_DESCRIPTORS=1` replays owned descriptor-info metadata before
the existing read-only snapshot overrides. Other values retain the descriptor-copy
path. Fresh set allocation, resource ownership, layouts and synchronization stay
unchanged. Metadata sanitizer checks and production syntax pass; the full build
verifies 50 PRX implementations and three DLLs, passes 22 host suites plus resource
and full graphics tests, and matches the 50-patch/139-path series.

The long device run proves replay of four bindings and four snapshots occurred.
All five scaled source captures and the reviewed nominal 180-second screen remain
white; 619 accepted draws and 86 present events continue to about 175 seconds.
No production descriptor fault or successful repair is proved. The experiment
remains default-off. Both this run and the menu-producing transition run contain
211 native Mach `UNHANDLED` markers, followed by runtime activity. Their presence
alone is not a fatal game-exit classification, and these runs are not described
as exception-free. Rollback is verified; no gameplay, input, audio or FPS claim
is made. See the [source/build/device record](evidence/ipad-solitaire-explicit-descriptor-experiment-20261008.json)
and the unchanged [original native metadata record](evidence/explicit-draw-descriptor-experiment-native-20261008.json).

## Close-recorded-pass experiment: patch0051

`APS5_CLOSE_DRAW_PASS=1` ends each recorded draw pass while preserving GENERAL
layouts, descriptor snapshots and asynchronous batches. Other values keep the
existing path. Production Windows syntax and the complete HLE/host rebuild pass;
the 51-patch series matches 139 paths. A single device marker confirms the option
was exercised, but all five scaled source captures and the nominal 180-second
physical screenshot remain white. Recorded work continues, and runtime/metadata
rollback verifies. This negative experiment stays default-off and establishes
neither a repair nor gameplay/FPS. See the
[source/build/device checkpoint](evidence/ipad-solitaire-close-draw-pass-experiment-20261008.json).

## Literal-size color texel copies: patch0052

ColorTargetLayout dispatches once per Tile/Detile surface for supported texel
sizes and uses literal-size copies in the logical sample loops. The optimized
Windows object loses imported variable-size `memcpy` calls in the two hot
functions; the prior-source negative control detects them. Existing swizzle,
sample order, padding, validation and GPU/write-tracking behavior remain.
Independent AMD AddrLib comparison covers 80 layouts, 2,650,990 addresses and 40
round trips, including unaligned spans and padding, under ASan/UBSan. The
original layout suite also passes. The complete 52-patch/139-path series applies,
matches and reverses; all 53 binaries, 22 host suites and two graphics suites
verify. See the [native record](evidence/fixed-texel-copies-native-20261008.json)
and [immutable build record](evidence/inline-color-texel-build-20261008.json).

The first controlled device run observes its menu source at 115.08 seconds rather
than the earlier 163.19-second checkpoint; first 180 trace counters also advance.
This is diagnostic content progression, not displayed FPS or a repeated matched
benchmark. Its first UI test overlaps the planned cleanup and fails before input.
The historical 49 attempt separately reports successful XCTest dispatch but no
visual response with the intended coordinate mapping unqualified. Neither is
classified as a game-input implementation failure. See the
[52 first device record](evidence/ipad-solitaire-inline-color-texel-menu-20261008.json)
and [historical 49 input record](evidence/ipad-solitaire-transition-menu-input-20261008.json).

The subsequent synchronized 52 run verifies hardware landscape orientation and
actual control centers, holds Right/A for three seconds, and retains the same
app/helper processes. Reviewed images show a menu transition after Right, Golf
dealing after A and a complete dealt board afterward. Runtime and metadata
restore. These two inputs and game entry are observed; individual card actions,
full gameplay, audio, saves, background recovery and sustained FPS remain
unqualified. The controlled transition path does not establish a default repair.
See the [interactive game-entry record](evidence/ipad-solitaire-inline-color-texel-input-20261008.json).

## Scoped bulk driver writes and HostWrite repairs: patch0053

Exact `APS5_BULK_DRIVER_WRITES=1` enables full `StoreOwnBytes` copies of at least
16 KiB only with contiguous registered readable/writable destination coverage
and valid tracking. A retained lease precedes the tracker lock and outlives the
final collect. Sparse/no-op `WriteChanged` and incomplete coverage retain their
legacy behavior. This opt-in avoids repeated shared-page write faults without
dropping alias invalidation, driver byte stamps or protection tracking.

The HostWrite API repairs are unconditional: identity records, preflight before
mutation, rollback of opened protection/counters, conservative dirty state after
rollback failure, and safe ending after reset/remap also serve existing callers
with the bulk option off. No exported ABI changes.

Actual local Windows shared mappings, the production tracker, native CRT and
exception recovery pass byte/guard/alias checks and prior/subsequent CPU/driver
classification. The unaligned full copy's handled faults drop from four to zero;
unset, `0` and `yes` retain four. Seven scope controls pass for every option,
including nested scopes, read-only refusal, partial opening/rollback failure and
raw remap. A higher native arena-base control also passes. The complete HLE build
verifies 53 binaries; 22 host suites plus opt-in resource/full graphics tests pass,
and the 53-patch/141-path series applies, matches and reverses.

These tests do not qualify a foreign CPU write racing inside the own-store
callback: the inherited postwalk labels new dirty pages as Driver. Hardware
improvement, production-speed measurement and new gameplay evidence remain
separate. The bulk optimization stays default-off. See the
[source/native/build record](evidence/scoped-bulk-driver-writes-build-20261008.json).

The separate controlled device record verifies one scoped-copy execution, a Golf
board after the menu sequence, and a waste-card change K♥→10♣ after Y. The board
and new waste persist through nominal 420 seconds, with the slow timer at 00:10.
Both UI sequences retain the same actual app/helper identities, and all runtime
and metadata bytes restore. One stock action is observed; tableau moves, a
complete game, audio, save/load and sustained FPS remain unqualified. Earlier
menu arrival/trace progress does not establish a speed gain, particularly because
the unconditional API repairs also differ from 52. A matched 53 bulk-off control
is needed for bulk-specific attribution. See the
[physical stock-action checkpoint](evidence/ipad-solitaire-scoped-bulk-driver-writes-20261008.json).

## Resolve snapshots and depth copies: patches0054–0056

Patch0054 enables pipeline reuse only for the explicitly tagged internal resolve
fragment on the canonical rectangle path and only for exact
`APS5_CACHE_COLOR_RESOLVE_PIPELINES=1`. The key retains the complete generated
fragment and TCS/TES words, with separate stage domains, alongside the existing
pipeline/device/layout/state identity. It does not assign guessed variant IDs to
ordinary shaders or admit zero-variant resource/recipe caches. Four sanitizer
configurations use the actual production pipeline cache with counted Vulkan
handles and a resource-layout adapter. Exact-one reuse, unknown-stage/tag
rejections, held-on-clear lifetime and unowned eviction are tested; execution on
the GPU and fence-bound resource lifetime are separate qualifications.

Patch0055 enables `APS5_RAW_RESOLVE_SNAPSHOT=1` only for the existing strict RGBA8,
R64KB_X, 2/4/8-sample source contract. `GuestMemory::Read` first flushes pending
work and copies the complete checked source into owned aligned backing; the
write stamp follows the successful copy. The resolve then uses that immutable
tiled snapshot. Selected pixel diagnostics may detile the captured backing;
the default path retains its detile/retile cycle. Existing validation, fault
checks, rectangle coverage and recorder ownership remain active. Twelve native
cases pass for each of unset, `0`, `1` and `yes`, with 198 assertions in the
exact-one run. Flush and generation effects are counted mocks, so these checks
do not prove actual GPU aliasing or tracker behavior at recycled host addresses.

Patch0056 specializes the existing depth/stencil Tile/Detile loops by raw texel
size once per surface. Literal-size 1/2/4-byte copies preserve alignment,
address equations, sample indexing and untouched tiled padding. Candidate and
prior source both pass the independent AMD AddrLib oracle under ASan/UBSan:
84 layouts, 35,028,036 address comparisons and 48 guarded misaligned round trips.
Optimized production MinGW object code removes the two hot imported `memcpy`
calls. This adds no guest depth consumption, sampled depth or HTILE support.

The combined 56-patch source applies and reverses across 142 paths. All 53 rebuilt
binary hashes match the archive, and 22 selected host suites plus resource and
full graphics validation pass. The first build's guest-memory test stops at an
arena reservation conflict using the inherited `0x200000000` default. Repeating
the identical source with the already qualified 53-checkpoint base
`0x7400000000` passes; no code repair or test disablement is counted as a fix.
Both new options remain default-off, while supported fixed-size depth copies
are always used. No device or performance acceptance follows from this build.
See the [combined source/build record](evidence/resolve-snapshot-depth-copy-build-20261009.json)
and [focused pipeline-cache record](evidence/color-resolve-pipeline-cache-native-20261009.json).

## GPU multisample color conversion: patch0057

Only exact `APS5_GPU_COLOR_SAMPLE_TILING=1` creates the per-device helper.
Supported uncompressed RGBA8 R64KB_X targets with equal 2/4/8 samples and fragments
use the reviewed AMD equations to convert between tiled bytes and packed logical
samples on the GPU. The helper rederives layout size/pitch and checks device,
descriptor, offset, storage-index and dispatch limits. Other metadata retains
the existing CPU path. Six immutable programs cover sample count and direction;
no guest content or address is cached. Each recording retains its own descriptors.

The existing synchronous batch orders detile, fragment sample upload, draw,
compute sample readback and retile with explicit producer/consumer barriers.
Retile leaves its destination seed's padding untouched. After the fence and
resource fault checks/writeback, `WriteChanged` commits only changed byte runs;
unchanged logical and padding bytes retain intervening CPU/alias stores. The
actual Windows shared-memory regression verifies ordered stores, stamps and
no-op behavior in four bulk-option configurations. It does not qualify racing
foreign CPU-write attribution during the commit callback.

The native Mac GPU probe uses actual helper, RenderTarget and ColorSampleTransfer
code. Independent AMD byte oracles, separate direction tests and poisoned
intermediates prevent skipped or mutually cancelling transfers from passing.
All 99 compute and three image-transfer chains pass, with all 144 descriptor
pools released. This covers the Mac's MoltenVK 1.4.2 path; full Draw/Wine/iPad
execution remains separate. At 1920×1080 with eight samples, two retained staging
backings add 127.5 MiB of allocation capacity, so device peak/RSS needs measurement.
No speed or FPS improvement is accepted here. See the
[focused source/native/Windows record](evidence/gpu-color-sample-tiling-native-20261009.json)
and [completed full HLE build and runtime-suite record](evidence/gpu-color-sample-tiling-build-20261009.json).

The exact RGBA8 and S8 device probes run original synthetic fixtures through
Wine/FEX/MoltenVK, using the production transfer code. Their buffer-alias cases
erase the upload source before readback; S8 checks also retain final-word padding
and constant D32 depth within the stated tolerance; bitwise depth identity and
arbitrary depth values are not tested. Both guests exit 0, but a native JIT-detach breakpoint
after completion leaves clean native app lifecycle unqualified. See the
[color device record](evidence/ipad-exact-color-sample-transfer-20261008.json) and
[stencil device record](evidence/ipad-exact-stencil-sample-transfer-20261008.json).

These tests do not qualify the complete draw/resolve/cache path or performance.
The experimental path currently synchronizes each draw and transfers every
color/S8 sample through CPU-visible buffers. Z use, consumed HTILE, compressed
stencil, sampled depth, EQAA and unsupported shader effects remain blocked.
Solitaire's fresh lifecycle-corrected package now has bounded device startup
evidence. The read-back manifest matches its receipt; the log selects the Apple
M2 GPU and creates a 768 × 432 swapchain with three images. There are no
`bad_alloc` or `FATAL` markers in that log, and the reviewed game surface remains
white. Two distinct rejection reasons each occur for two targets: the split path
requires a vertex/fragment pair, and a multisample raster state lacks
`MSAA_ENABLE`. The receipt's 72.52 seconds are test-operation duration, not an
accepted gameplay run. The early-detach `Wine finished after 23.5s` message is
followed by 2,686 runtime log lines and does not establish guest exit.
Configuration, library and runtime hashes are restored. See the
[fresh device startup record](evidence/ipad-solitaire-retained-export-lifecycle-20261008.json),
[lifecycle implementation checks](evidence/retained-guest-export-lifecycle-20261008.json)
and [canonical rectangle checks](evidence/grouped-msaa-canonical-rectlist-20261008.json).

That game device run uses the as-built 0044 HLE set and the lifecycle-corrected
relinker; patches0046/0047 have not been qualified by it. A separate later probe
uses production resolve fragment and rectangle stages in an isolated pipeline:
six 17 × 19 cases pass 7,752 channels, including 2,580 untouched destination
channels. Four cases additionally compare 5,168 channels to native Vulkan
resolves. Non-ties and untouched pixels match exactly; half-rounding ties permit
one UNORM unit. The guest exits 0 before a native `jit26_detach` breakpoint;
configuration and library are byte-restored, but clean native lifecycle is not
qualified. See the
[resolve GPU and current build record](evidence/ipad-fixed-color-resolve-20261008.json)
and [native resolve checks](evidence/gc10-fixed-color-resolve-native-20261008.json).

The new connected HLE archive includes all 47 patches, matches their source-series
hashes and verifies all 53 packaged files: 50 PRX plus three runtime DLLs.
All 22 selected host suites and resource/full graphics host suites pass.
A freshly prepared game package with this archive runs on the iPad and reaches
the three-image swapchain. Its previous pair-only and `MSAA_ENABLE` messages
are absent; one skipped draw reports `SPIR-V refers to an unknown type`.
The reviewed capture stays white with controls. Runtime activity continues after
helper completion, without a recorded guest exit. The bounded type-validator
repair is implemented in patch0048 and clears that logged error in the later run;
the isolated GPU probe does not qualify full game rendering.
The runtime, configuration and library are restored. See the
[current game device record](evidence/ipad-solitaire-rectlist-resolve-20261008.json).

The newer 48-patch package matches 137 source paths and applies/reverses cleanly.
All 50 PRX/three DLL hashes verify, and 22 selected host suites plus the resource
and full graphics suites pass. Its prepared game manifest passes the dependency
audit and matches the device receipt. The bounded app Build47 run creates the
Apple M2 swapchain, with zero skipped-draw, `AGC graphics:`, unknown-type,
`bad_alloc` or `FATAL` markers across 25,006 log lines. The reviewed capture
remains white with controls and black letterboxing. An additional 8,362 log
lines follow early-detach/helper completion, so no guest exit is inferred.
Configuration, library and manifest are byte-restored, and runtime hashes are
restored. Correct GPU image contents and their presentation route now require
diagnosis; no correct game-frame, gameplay or performance claim is made. See the
[latest 0048 device/build record](evidence/ipad-solitaire-opaque-descriptor-20261008.json).
The memory suite's first fixed reservation at 8 GiB collided under Wine;
its rerun passes at the tested 464–468 GiB lazy arena without removing assertions.
The selected host suites do not contain GPU tests. See the
[full-build and source record](evidence/solitaire-msaa-draw-source-20261008.json),
[implementation and remaining work](MSAA-EMULATION.md) and
[earlier component checkpoint](evidence/solitaire-msaa-renderer-integration-20261008.json).


The implementation follow-ups are documented in
[IMPLEMENTATION.md](IMPLEMENTATION.md): transactional GuestArena (now folded into AnyPS5 0001),
accurate unavailable SDK telemetry (now upstream), the complete static Vulkan
bridge (0006), and the isolated runtime profile/build integration (0007).
The native protection-repair follow-up (0017) retains the fault terminal and
recognizes only a checked writable repair on the faulting thread/range. Build 12
passes the same original 5000-store probe that terminated at 2000 on Build 11;
an intentionally unrepaired handler still terminates. Vulkan lifecycle admission
and drain (0018) parks GPU calls while inactive, preserves native results and
tracks live devices. Host dispatch/quiescence tests and actual iPhoneOS compilation
pass; device validation is recorded separately in IMPLEMENTATION.md.
The foundation measurements below remain historical evidence.

Apply with `scripts/apply-patches.sh`. Reverse with
`scripts/apply-patches.sh --reverse`. The submodules in git stay at the
upstream commits; the patches are the delta.

## GPU eight-sample stencil conversion: patch0058

Only exact `APS5_GPU_STENCIL_SAMPLE_TILING=1` creates the per-device helper;
unset, `0` and `yes` retain the existing CPU path. This replaces only S8
Detile/Tile for already qualified uncompressed SW_64KB_Z_X surfaces with eight
equal coverage/stored samples. The helper checks the exact AMD equation,
64-KiB alignment, canonical pitch, dimensions, device limits and storage offsets.
Other metadata keeps the existing CPU conversion and rejection rules.
The existing grouped-draw restrictions on Z, HTILE, shader effects and attachment
aliases remain active. No new stencil transfer pipeline cache or resident target
path is added.

Two immutable per-device compute programs cover detile and retile. Each XY/group
invocation owns a complete four-byte word; two groups preserve all eight S8
samples without shared-word writes. The original complete tiled seed initializes
the output, so logical GPU writes leave every padding word intact. Distinct
descriptor sets remain owned through the existing synchronous batch. Explicit
barriers order host seed, detile compute, fragment stencil upload, compute
readback, retile compute and host read. After the same fence and resource fault
checks/writeback, existing `WriteChanged` commits only bytes differing from the
original seed. Unchanged padding and unchanged logical bytes retain later ordered
CPU/alias stores. Racing writes to a GPU-changed logical byte remain unqualified.

The production helper's sanitizer contract passes 28 metadata fallback controls,
repeated descriptor ownership, exact barriers and allocation-failure cleanup.
The native Mac GPU probe passes 42 compute cases and six production
StencilSampleTransfer image chains against independent AMD AddrLib coordinates
and bytes: 21,313,064 coordinate comparisons, 64,608,968 checked logical output
bytes and 89,348,960 checked padding bytes, with all 256 S8 values represented.
Poisoned intermediate buffers and guards prevent omitted transfers from passing.
Both tiling programs are cached once; all 80 descriptor pools are released.

The image chains use an original synthetic D32S8 owner, two native four-sample
groups, and a depth reader. All 669,776 checked depth samples retain initialized
0.625 within 0.000001; arbitrary depth values and bitwise depth identity are not
tested. D16S8 has metadata admission checks only. Actual Draw/DepthSurface and
GuestMemory GPU integration, Wine/FEX/iPad execution and performance are separate
gates. At 1920 × 1080, one additional tiled GPU buffer retains 16.875 MiB during
the wait; this is an allocation calculation, not a measured device footprint.
The completed full HLE rebuild now has a separate record: all 58 patches match
150 preserved source paths, all 50 PRX and three runtime DLL hashes verify, and
22 CPU/API plus actual tracking, resource classification and full Graphics
host suites pass. Canonical private preparation verifies 137 files and the
dependency closure. Only three HLE binaries and two preparation logs differ
from 57; the game executable and five converted guest modules are unchanged.
These host/preparation results do not execute the complete iPad Draw path.
No speed, FPS or full gameplay acceptance is inferred. See the immutable
[focused source/native record](evidence/gpu-stencil-sample-tiling-native-20261009.json)
and the [completed build/preparation record](evidence/gpu-stencil-sample-tiling-build-20261009.json).

## Immutable stencil transfer programs: patch0059

Only exact `APS5_CACHE_STENCIL_TRANSFER_PIPELINES=1` creates the per-device
cache; its default is off. A cache miss builds the same immutable render pass,
descriptor/pipeline layouts and stencil upload/readback pipelines as before.
An eight-sample miss creates eight upload pipelines and one compute readback
pipeline. The key includes a per-cache device domain, combined depth/stencil
format, logical/native/group counts, fixed program schema and every ordered
sample-position float bit pattern. Existing image, extent, feature, format,
descriptor, workgroup and position checks run before lookup even on warm hits.

The mutex-protected LRU retains at most eight entries. Eviction and `Clear`
only drop cache references; each transfer holds a shared immutable bundle
through its caller's existing GPU fence. Active transfers may retain evicted
bundles beyond those eight entries. Image views, framebuffers, descriptor sets,
pools and buffers remain owned per transfer. The eight-sample upload still
performs all 64 bitplane raster draws, with unchanged shaders, sample locations,
barriers, tiling, Z behavior and synchronous completion. Device idle and recorder
retirement precede cache destruction, while the Vulkan device still exists.

The sanitizer contract checks invalid capacities 0/9, eight invalid warm
contexts, ordered and bit-exact position changes, format/sample/device identity,
active ownership after eviction/clear, and 19 construction-failure/retry cases.
The unchanged legacy transfer suite passes six layouts and 234 rejection/unwind
cases. The native Mac GPU probe exercises the production cache and transfer:
three misses create 27 stencil programs; three hits create zero. One live
transfer completes correctly after cache clear before actual submit. All six
D32S8 image chains retain exact S8 bytes, padding and 669,776 initialized 0.625
Z samples within 0.000001. Independent AMD goldens and poisoned intermediates
retain the original transfer checks.

These native chains use an original synthetic image owner rather than actual
Draw/DepthSurface/GuestMemory integration. D16S8 and other tuple controls use
counted Vulkan mocks. The immutable
[source/native checkpoint](evidence/stencil-transfer-program-cache-native-20261009.json)
preserves its publication-time limits. A separate
[full build checkpoint](evidence/stencil-transfer-program-cache-build-20261009.json)
now verifies all 59 patches against 150 frozen source paths, 53 archive binaries,
22 CPU/API suites and three additional actual tracking/resource/Graphics suites.
The frozen 22-suite log map remains separate from the final 25-log verification.
Private preparation verifies 137 files and six complete guest dependency sets;
only three HLE binaries and two preparation logs differ from 58, with the game
executable and five converted guest modules unchanged. Physical iPad cache integration, pipeline
memory and matched speed/FPS remain unqualified by this build record. No
performance gain, accepted gameplay or sound quality is inferred.

A separate 360-second iPad run with the Build 48 host reports actual immutable
stencil bundle reuse with matching ordered positions. Golf selection and its
complete board are visible; only menu Right and start A are sent. The unchanged
strict display reader accepts all 337 rows and reports 2784 completion intervals
over 284.8849 seconds after warmup. These native completions and one reuse marker
do not accept cache hit rate, physical frame uniqueness, speed, legal gameplay
or audio. Runtime rollback independently verifies all 56 files and exact
configuration/library/manifest bytes; both owned processes are absent afterward.
See the [separate device record](evidence/ipad-solitaire-stencil-transfer-program-cache-20261009.json).

## Whole-row display telemetry: Madeira0048, 2026-10-09

| Patch | Implemented behavior | Qualification |
|---|---|---|
| Madeira0048 | Format each existing display/native JSON row completely, then emit it through one direct append write | Production-header ASan/UBSan fixtures pass full-width bounds, exact-one gating, concurrent append writes and failure controls. Build 48 is built, signed and installed on the iPad. Its bounded run with the 58-patch HLE passes the unchanged whole-log display parser and visibly verifies one legal Golf move. Sound quality, game speed and 60 FPS remain unqualified. [Build evidence](evidence/display-telemetry-whole-row-20261009.json), [device evidence](evidence/ipad-solitaire-build48-whole-row-telemetry-20261009.json). |

The existing emitter split its display histogram and native counters across
multiple `fprintf`/`fputs` calls. Madeira redirects stderr directly into a regular
`O_APPEND` log file; native `write`/`dprintf` diagnostics bypass the stdio `FILE`
lock and can enter between those calls. Madeira0048 builds each complete row in
a bounded 7,445-byte stack buffer before one `write` syscall. A negative
`EINTR` result retries; a positive short write never receives a tail repair.
Formatting overflow emits no row. The exact `APS5_PERF_REPORT=1` gate, schema,
one-second cadence and strict display parser remain unchanged. Pipe atomicity is
not qualified.

Synthetic production-header fixtures check all 257 histogram buckets at
`UINT64_MAX`, maximum-width metadata and a 5,969-byte longest row. One thousand
display and one thousand native rows remain intact amid 4,000 raw writes and
4,000 `dprintf` writes through separate append descriptors. ASan/UBSan, exact
gate values and EINTR/short-write/error/overflow controls pass. These are format
and file-transport checks, with mocked Vulkan results; they measure no GPU or
device performance.

The build recompiles only `vulkan.o`. All 89 other object members remain
byte-identical; the archive symbol index is regenerated. The isolated app copy
reuses FEX, PE, MoltenVK and HLE binaries. All 170 embedded PE files in
`arm64ec-windows` and 488 original Build47 source/archive files verify unchanged;
the four required system DLLs retain verified ARM64EC code maps. Build48 retains
optimized Debug/JIT settings, the bundle ID and JIT helper. Its signature passes
`codesign --verify --deep --strict`; signed entitlements match Build47's retained
signing inputs, and the cached profile covers the app, target device and both
memory permissions. No provisioning update or GitHub Actions ran.

Installation, JIT activation, device row integrity and runtime behavior remain
separate checks. Historical malformed device telemetry remains invalid for
strict display summaries; this patch does not repair or reinterpret old logs.

The subsequent physical run installs Build 48, activates and detaches JIT, and
uses the same 58-patch HLE. All 682 display rows pass the unchanged strict reader
without repairs. Its warmup-adjusted span reports 6198 intervals over 646.6213
seconds, or 9.5852 native display completions per second. A visible Q♠ to K♣
Golf move exposes 5♦, followed by two stock responses. These observations do not
accept game speed, audio or constant 60 FPS. The dated build JSON preserves its
earlier pre-installation status; the
[separate device record](evidence/ipad-solitaire-build48-whole-row-telemetry-20261009.json)
contains the later installation, actions, telemetry and verified runtime rollback.

## Bounded postmix audio capture: Madeira0049, 2026-10-09

Madeira0049 adds an explicitly enabled diagnostic at the existing stereo output,
after mixing and clamping. It records at most 480,000 float32 frames at 48 kHz
and their callback timestamps. The default path remains unchanged. Setup
preallocates and touches its bounded arrays before starting RemoteIO; the render
callback performs bounded copies and lock-free publication, with no allocation,
file I/O, logging or blocking wait. A separate worker writes private WAV/JSON
files, preserving existing files and removing only its own incomplete exports.

Exact `MADEIRA_AUDIO_CAPTURE_POSTMIX=1` enables the one-shot capture.
`MADEIRA_AUDIO_CAPTURE_SKIP_FRAMES` accepts a strict unsigned decimal from zero
through 28,800,000; invalid values reject setup. The skip counts rendered audio
frames, not game frames. The worker deadline includes the skipped audio duration.
It cannot export or free a buffer while a producer owns it; a permanently stalled
producer can therefore delay export. The implementation neither changes the
mixer nor repairs an audio defect.

Synthetic checks compile the production helper and extracted original/current
render callback. Six gate values, callback byte parity, full capture bounds,
producer ownership, timestamp metadata, export failures and file collisions
pass ASan/UBSan. The actual iPhoneOS translation unit compiles; the isolated
Build49 app links and passes deep/strict signature checks. Only the audio object
in the native ntdll archive changes; the other 36 members, native48 win32u,
FEX, PE and MoltenVK inputs are preserved. See the dated
[source/build checkpoint](evidence/audio-postmix-capture-host-build-20261009.json).

A separate temporary iPad run captures ten continuous seconds of postmix audio
in a menu-only Solitaire session. Reviewed screenshots at nominal 220 and
240 seconds show the complete Canfield selection menu. This does not establish
continuous visual output between captures. The entire captured PCM repeats
exactly every 1,024 frames, including
partial edge records; all 468 complete periods are byte-identical. Callback
epochs and sample timestamps advance continuously, and the recording has no
nonfinite samples or full-scale clipping. This localizes a concrete repeated
period at the postmix boundary; it does not establish which earlier component
causes it or accept audible quality. No game input is sent in this run. Native48,
the original runtime, configuration, library, manifest and prior diagnostics
are restored and verified. See the separate
[device/audio record](evidence/ipad-solitaire-audio-period-replay-20261009.json).
Captured media and raw diagnostics remain private and outside Git.

## Native x64 suspension and wait results: 2026-10-08

Madeira `0037-target-published-x64-suspend.patch` and Wine
`0003-target-published-x64-suspend.patch` add an opt-in same-task AMD64
SIGUSR1 suspension handshake. The target publishes its reconstructed guest
context; native EC contexts are distinguished from translated code through the
alias map and EC bitmap. Native syscall completion records preserve actual
return values when the guest exception handler resumes the original call.
`APS5_X64_SIGNAL_SUSPEND=1` enables the path before Wine initializes;
`APS5_X64_SIGNAL_TRACE=1` adds detailed diagnostics. Both default off.

AnyPS5 `0030-native-wait-exception-contracts.patch` extends the real exception
test to 441 deliveries, including ten native event timeouts and ten signaled
waits. It retains every original delivery, stack and invalid-target assertion.
Madeira `0038-target-suspend-build-version.patch` records app Build 35.
See [source and device qualification](UPSTREAM-REFRESH.md#target-published-x64-suspension-build-35)
and [result hashes](evidence/ipad-target-published-suspend-20261008.json).
Solitaire still faults in its graphics worker with normal memory tracking;
the native change is not promoted to a global default.

## Asynchronous AVX context preservation: 2026-10-08

AnyPS5 `0031-preserve-async-avx-state.patch` captures the upper halves of all
16 YMM registers at the first redirected guest instruction, before C++ or a
handler can clear them. The guest context exposes the standard non-compacted
AVX state area, copies back handler edits, and initializes the upper halves when
the handler clears its AVX state bit. A final assembly tail call restores that
state immediately before `NtContinue`, retaining native wait-result handling.
Feature and loader checks run before suspending the target. The opt-in
`APS5_PRESERVE_ASYNC_AVX=1` requires an advertised usable AVX feature; default-off
and unsupported-CPU routes execute no new AVX instructions.

The full synthetic test retains the original 441 delivery assertions and adds
102 deliveries with all 16 live registers, deliberate handler clobbering, context
edits and AVX reset. It exits 0 on Build 35 with both native suspension and this
HLE path enabled. The local host passes its 441 cases but does not advertise
AVX, so its vector cases explicitly skip and do not qualify AVX restoration.
The combined private Solitaire run still fails; all production files and
settings are restored. See the [device record](evidence/ipad-async-avx-context-20261008.json).

## Asynchronous integer flags: 2026-10-08

AnyPS5 `0032-preserve-async-integer-flags.patch` and Madeira
`0039-query-reconstructed-guest-flags.patch` repair an independently reproduced
loss of parity and auxiliary-carry flags. The original device context and
resumed code both reported `0x8c1` instead of `0x8d5`, despite all 15 general
registers being correct. The local x86 Wine comparison retained the flags.

The opt-in `APS5_PRESERVE_ASYNC_FLAGS=1` uses private thread-information class
`0x7fff4150` to obtain the missing flags from a suspended same-process target.
A versioned 24-byte response is accepted only when its guest RIP/RSP match the
normal context and the PC is translated guest code. Native EC waits and stale
snapshots keep their existing continuation path. The native bridge reads the
pinned FEX state layout through safe memory peeks; it changes no FEX source.
`scripts/check-async-flags-layout.py` compiles a project-owned checker against
the actual dependency headers before the normal Unix-library build. A changed
layout fails the build rather than silently reading different fields.

The final guest resume stub restores flags with `popfq`, restores RSP with
`lea`, then jumps to the requested RIP without clobbering general or vector
registers. Scratch storage is below the SysV red zone and outside saved AVX
state. The handler's edits survive; DF is cleared before entering C++ and
restored for interrupted guest code. Both native suspension and this HLE option
remain default-off. Madeira `0040-async-flags-build-version.patch` records Build 37.

Two final device runs pass 553 deliveries with AVX enabled and 451 without it.
Ten integer-state cases check all 15 general registers, seven condition/direction
flags, both directions of handler flag edits, register edits, RIP/RSP and callback
DF requirements. All original native-wait and exception assertions remain.
Solitaire still shows white output and faults through a null shader-object
vtable. Its rejected 8-sample draw and device rendering remain unresolved.
See the [qualification record](evidence/ipad-async-integer-flags-20261008.json).

## Current local refresh: 2026-10-07

The [recorded pins](UPSTREAMS.md) are built in isolated local checkouts. All
22 AnyPS5, 18 Madeira, two FEX and two Wine patches apply to pristine pinned
sources, match the built source and reverse successfully. The four optional
MoltenVK patches pass apply/reverse checks but were not used in Build 18.

The native iPhoneOS runtime and ARM64EC ntdll/FEX/Vulkan components were rebuilt,
linked into the development-signed menu app and verified against embedded hashes.
Build 18 is installed on the M2 iPad. All 11 HLE host contracts pass using the
qualified WinLibs compiler under local macOS Wine; the portable JSON and offline
API contracts also pass ASan/UBSan. This is not a native-Windows GPU reference.

The hardware audio-period regression reproduces the old callback starvation and
passes the new sizing at 44.1, 48 and 96 kHz. The iPad run reports 21.333 ms;
physical audio behavior and route changes remain unqualified. Solitaire is
installed with a complete dependency closure, but its bounded launch reached
Unity initialization without a game image. See the
[Build 18 record](evidence/ipad-m2-menu-build18-solitaire-install.json) and
[runtime measurement details](RUNTIME-PERFORMANCE.md).

## Menu lifecycle and serialized shader headers: 2026-10-08

Madeira 0028 restores explicit activation and resignation of the Vulkan gate
from the menu frontend, including its initial state when SwiftUI appears after
UIKit's first activation notification. Build 19 creates the device and three
swapchain images on the iPad; the native gate's quiescence/dispatch tests pass.
The bounded Solitaire launch then exposes an AGC shader-header alignment failure,
also reproduced under local macOS Wine. See the [Build 19 record](evidence/ipad-m2-menu-build19-solitaire-startup.json).

AnyPS5 0022 uses the dword alignment of serialized AGC shader headers and nested
user-data pointer tables, preserves the 96-byte header and field offsets, and
relocates packed pointer fields with byte copies. Code alignment and mapped-range
checks remain enforced. Its opt-in unreadable-range trace now reports the rejected
address, range, alignment and caller. The production creation/relocation test
checks headers at an address congruent to four modulo eight, null relative fields,
program-address register patching, and rejection of misaligned shader code.

The header/relocation change passes four portable ASan/UBSan contract groups
and all 12 qualified WinLibs Windows contracts under local macOS Wine. On the
iPad, the updated package's 138 files were hash-verified, and shader registration
now passes. Startup next aborts at `sceKernelRaiseException` with signal 30:
Unity installs a handler and needs real target-thread/context delivery for GC.
This call is not replaced by a successful no-op.

AnyPS5 0023 admits PA_SC_MODE_CNTL_1 bit 17, which partitions primitives across
AMD shader engines; Vulkan handles hardware distribution of the full draw.
Logical raster controls such as pixel killing, sample iteration and out-of-order
rasterization remain rejected. The focused `agc_driver_graphics_tests --state-only`
run passes; the full graphics suite still fails a separate fixed-function
interpolation shader-recompiler case. The focused result does not qualify the
whole renderer. See the [runtime repair record](evidence/ipad-m2-solitaire-header-state.json).

## ARM64 atomic access direction: 2026-10-08

Madeira 0029 classifies CAS/CASP and LSE read-modify-write instructions as write
faults before Wine/FEX exception delivery, including acquire forms that the old
bit-22 load/store shortcut classified as reads. It leaves ordinary loads,
load-exclusive instructions and LDAPR outside this override. It changes access
classification only: mapping ownership, permissions, dirty tracking and the
fault terminal remain enforced by the existing handlers.

Build 21 passes an independent x86-64 protection probe on the M2 iPad through
Wine/FEX: successful and failed CMPXCHG16B comparisons, XADD, XCHG and CMPXCHG
produce write faults on read-only pages, while a plain load from PAGE_NOACCESS
produces a read fault. The production instruction classifier also passes
ASan/UBSan host tests. The temporarily added probe entry and 512-MB JIT-pool
configuration are removed and the originals restored byte-for-byte.
[Build 21 record](evidence/ipad-m2-menu-build21-atomic-direction.json).

A private cooperative-SIGUSR1 experiment advances Solitaire past Unity GC
initialization but does not qualify arbitrary asynchronous guest signals. With
the atomic correction, a later null-vtable dereference and MoltenVK PerVertexKHR
compilation errors remain. The experimental signal sources are not part of the
production patch series; visible Solitaire gameplay is still unqualified.

## Historical foundation evidence

The following Linux and hosted-runner records describe earlier pins. Historical
patch numbers and unavailable components are not the status of the current
2026-10-07 runtime. GitHub Actions remain disabled; all current validation and
builds run locally.

### Verified on the original Linux VM

- `tools/vk-requirements` rebuilt from the current source and run against
  lavapipe (`/usr/share/vulkan/icd.d/lvp_icd.json`, Mesa 25.2.8, LLVM
  20.1.2). Exit 0. `summary hard_fail=0 optional_missing=3
  selected=llvmpipe (LLVM 20.1.2, 256 bits)`. The three optional misses
  are fragment shader barycentric, `VK_EXT_image_view_min_lod`, and
  `depthBounds`. `VK_KHR_swapchain` is reported as the extension only; no
  surface was created. `VK_EXT_metal_surface` is absent on this host.
  Stock and portability instance enumerations both see one device.
  `VK_KHR_portability_subset` is not advertised, so the portability-subset
  device path was not exercised.
- `patches/madeira/0003-winevulkan-moltenvk.patch` contains
  `moltenvk_static_loader.c`. `tools/checks/test_moltenvk_loader` compiles
  that file and checks the static-link fallback: a missing
  `libMoltenVK.dylib` resolves `vkGetInstanceProcAddr` and
  `vkGetDeviceProcAddr` from the process, and does not resolve other
  symbols. That is the loader policy. It is not a MoltenVK build and not
  an iOS link.
- AnyPS5 Linux Release build (`scripts/m0-build-anyps5.sh`) finished and
  linked the relinker. The 17 synthetic tests in
  `core/relinker/relinker/tests/` all passed against that binary. No title
  ELF was used.
- Synthetic PE under Ubuntu Wine 9 (`scripts/m0-synthetic-pe-wine.sh`).
  The Linux relinker turned `test_optional_plt.py`'s fixture into
  `sample.exe` and `sample-intel.exe` (`--windows` and
  `--windows --to-intel`). Both are `IMAGE_SUBSYSTEM_WINDOWS_CUI` and both
  exited 42. Lavapipe was selected (`VK_DRIVER_FILES` =
  `lvp_icd.json`). The PE does not call Vulkan, so this does not exercise
  the GPU path. Wine's prefix init printed a harmless
  `rundll32.exe` `c0000135`.
- Windows `relinker.exe` from Ubuntu MinGW-w64 GCC 13 posix
  (`build/anyps5-mingw/core/relinker/relinker.exe`, PE32+ console, 3.8 MiB).
  Under the same Wine it relinked the same fixture to a PE (subsystem 3).
  That PE printed `Transferring control to ELF entry point` and exited 42.
  `libc.prx` and `libSceAgcDriver.prx` did not link. See the MinGW block
  below.
- Pinned FEX `ios-port-2607` cross-built for aarch64 Linux
  (`scripts/m1-build-fex-aarch64.sh`, Clang `--target=aarch64-linux-gnu`,
  `-DTUNE_CPU=cortex-a78`, jemalloc glibc hook off). `Bin/FEX` and
  `Bin/FEXServer` are aarch64 PIE executables.
  `scripts/m1-wine-fex-arm64.sh` with no arguments ran a nostdlib x86-64
  guest (`mov $42, %rdi; mov $60, %rax; syscall`) under
  `qemu-aarch64-static` and exited 42. `APS5_GUEST_ARENA_LAZY=1` and
  `APS5_GUEST_ARENA_SIZE=0x100000000` were set; that guest does not read
  them. A glibc static hello died with `Fatal glibc error: Cannot allocate
  TLS block` (exit 127). `qemu-aarch64-static` plus host `wine` plus the
  synthetic PE segfaulted inside qemu (`uncaught target signal 11`) before
  any guest print. binfmt_misc is not mounted here; the script copies FEX
  and wraps `FEXServer` in a shell that re-enters qemu. This is a double
  emulator and not a timing result. The aarch64 binary includes
  `patches/fex/0001` through `0004` (rpmalloc POSIX log,
  `InitializeAllocator` declaration, iOS compile-block logs kept under
  `FEX_IOS_HOST`, and `VirtualQuery` compiled only on Windows).
- `scripts/check-fexbridge-avx.sh`: host exits were default=0, `MADEIRA_FEX_AVX=0` → 1,
  `MADEIRA_FEX_AVX=1` → 0. The same TU cross-compiled and under qemu
  defaulted to AVX on. `FEXBridge.mm` itself was not compiled.

## Compiled inside the Linux AnyPS5 build

- `patches/anyps5/0002-vulkan-portability.patch` is in
  `VulkanDevice.cpp.o` and in `libSceAgcDriver.prx`. The object file
  contains the strings `VK_KHR_portability_enumeration` and
  `VK_KHR_portability_subset`. `libSceAgcDriver` is `EXCLUDE_FROM_ALL`;
  the default `m0` build does not emit it, and a follow-up
  `cmake --build build/anyps5 --target libSceAgcDriver` did. Nothing
  created a `VkDevice` from that library on this run. The patch enables
  the two extensions only when the ICD advertises them.
- `patches/anyps5/0001-guest-arena-lazy.patch`: `GuestArena.cpp` compiled
  into `libc.prx` on Linux. The Windows constructor, including
  `ReadArenaLimits` and the lazy `Reserve` loop, is under `#ifdef _WIN32`,
  so the Release Linux object does not contain the `APS5_GUEST_ARENA_*`
  strings (the unused static parser is optimized out). The Linux
  constructor still compiles and leaves the arena unavailable.

## Compiled on a Linux host, not an iOS or Windows library link

- Madeira's Wine fork (`scripts/build-wine-vulkan-linux.sh`), configured
  `--enable-archs=x86_64` out of tree. `SONAME_LIBVULKAN` is
  `libvulkan.so.1`. Linked on this VM: `dlls/ntdll/ntdll.so` (4,130,936
  bytes), `dlls/win32u/win32u.so` (12,068,824 bytes), and
  `dlls/winevulkan/winevulkan.so` (6,997,864 bytes). `winevulkan.so`
  NEEDs `ntdll.so`, `win32u.so`, and `libc.so.6`. `win32u/vulkan.o`
  contains the string `libvulkan.so.1` (dlopen, not a DT_NEEDED). The PE
  `winevulkan.dll` and `vulkan-1.dll` still link. The unix libraries were
  not executed and did not create a `VkDevice`.
  `patches/wine/0001-linux-winevulkan-guards.patch` keeps the Apple
  `clock_gettime_nsec_np` / `mach_absolute_time` / QoS path under
  `#ifdef __APPLE__` and uses `CLOCK_MONOTONIC` plus a no-op eco apply
  on Linux. `ios_srcwatch_arm`, `ios_srcwatch_arm_geom`, and
  `winios_dump_srcbits` are weak definitions in `dibdrv/bitblt.c`
  (`nm` shows `W` on `dlls/win32u/dibdrv/bitblt.o`). A missing weak
  reference is not enough for Apple ld. The Apple link is what
  `winevulkan-ios` runs; the weak definitions are not measured there
  yet. `vulkan_ios.c` and `vulkan_metal_ios.c` were not compiled.
- `patches/anyps5/0001-guest-arena-lazy.patch` inside the MinGW libc
  objects: `GuestArena.cpp.obj` contains `APS5_GUEST_ARENA_BASE`,
  `APS5_GUEST_ARENA_SIZE`, `APS5_GUEST_ARENA_CHUNK`, and
  `APS5_GUEST_ARENA_LAZY`. That object is SjLj and is not in a linked
  `libc.prx`. `patches/anyps5/0003-mingw11-getthreaddescription.patch`
  supplies the prototype mingw-w64 11 headers omit;
  `CrashReport.cpp` compiled after that. The import lib already had the
  symbol.

## Compile-checked only as a translation unit, not linked into a PE

- A separate `g++ -c` of `GuestArena.cpp` as a Linux TU
  (`-Wno-unused-function`, because the parser is only called from the
  Windows constructor) keeps the `APS5_GUEST_ARENA_*` strings.
- MinGW `x86_64-w64-mingw32-g++ -c` of the same file type-checks the
  Windows constructor, the chunk reserve loop, and the env parser.
  `WindowsMappings.hpp` is included. That object contains the
  `APS5_GUEST_ARENA_*` strings. No `VirtualAlloc` call was executed.
  Pre-existing `-Wcast-function-type` warnings in `WindowsMappings.hpp`
  (GetProcAddress casts) are upstream, not from this patch, and the
  compile is not `-Werror`.

## Reviewed against the pinned sources, not compiled

- The rest of `0003` (`vulkan_ios.c`, `vulkan_metal_ios.c`, the
  `build.sh` / `driver_ios.c` edits) includes Wine unix headers and is
  built by Madeira's iOS `xcrun` script. Those TUs were not compiled here.
- `patches/madeira/0001-fexbridge-avx.patch` edits `FEXBridge.mm`. That
  file includes Mach and FEX headers and is Objective-C++. It was not
  compiled. The behavior it encodes: `MADEIRA_FEX_AVX=0` disables AVX;
  any other value, including unset, enables it; `SupportsSVE128` and
  `SupportsSVE256` stay false. See [ARCHITECTURE.md](ARCHITECTURE.md) for
  why SVE is not required and why `xtajit64` is a second switch.
- `patches/madeira/0002-extended-virtual-addressing.patch` adds one plist
  key. It does not sign a profile and does not change the address map.

## Blocked on this Linux VM

- Windows HLE libraries (`libc.prx`, `libSceAgcDriver.prx`) with the
  compilers installed here. Ubuntu GCC 13 posix plus AnyPS5's
  `-fno-asynchronous-unwind-tables` emits SjLj, and this libgcc has no
  SjLj runtime. llvm-mingw Clang cannot compile `__builtin_sysv_va_list`.
  `scripts/m0-build-anyps5-winlibs.sh` downloads WinLibs GCC 15.2.0
  posix-seh (`15.2.0posix-14.0.0-ucrt-r7`) and is what
  `.github/workflows/windows.yml` runs. This VM has not produced those
  PRX files. The GitHub `winlibs` job on `f1d3f06` did; paths are in
  the hosted-runner section. Byte sizes were not printed on `f1d3f06`.
  They were printed on `4b6d5a4`.
- Wine under aarch64 FEX on this qemu-user. The nostdlib guest is the run
  that returned 42. Host Wine segfaults in qemu before the PE prints
  anything. On GitHub `ubuntu-24.04-arm` (`a54b497`) native FEX ran that
  same nostdlib guest to exit 42, and `wine64` 9.0 was installed from
  ubuntu-base. The synthetic PE did not start: Wine exited 1 because
  `/tmp` was not owned by the runner user.

## Untested, and not claimed

- iOS app link of MoltenVK into `libwin32u_unix.a`.
- `vkCreateMetalSurfaceEXT` against a real `CAMetalLayer`.
- Lazy GuestArena under Wine's `MEM_RESERVE_PLACEHOLDER` implementation.
  AnyPS5 already uses placeholder reservations; Wine on iOS may reject
  them. That would be an M4 failure to record, not something this patch
  papers over.
- AVX2 correctness for PS5 code on 128-bit FEX IR.
- Whether a 512 GB iOS map can hold the default 448 GiB arena even with
  the entitlement. Prefer lazy mode and a smaller
  `APS5_GUEST_ARENA_SIZE` until a device log says otherwise.
- AnyPS5's Windows PE actually loading `vulkan-1.dll` from Madeira's
  arm64ec farm. `build/wine-pe/build-modules.sh` grows that farm when
  `MADEIRA_WITH_VULKAN=1`. The script was not run (it needs llvm-mingw
  and a macOS or cross setup Madeira documents).

## Slim configuration

`docs/SLIM.md`. `APS5_SLIM=1` on `scripts/m0-build-anyps5.sh` passes
`-DAPS5_SLIM=ON`: `libSceAgcDriver` loads `libvulkan.so.1` with `dlopen`
and does not link SDL. The Linux PRX went from 8,874,056 bytes (3,396
`SDL_` symbols) to 6,741,112 bytes (no `SDL_` symbols). Portability
extension strings are still in the slim binary. No `VkDevice` was
created. `APS5_SLIM=1 scripts/build-wine-vulkan-linux.sh` configures a
separate tree with OpenGL, Wayland, FreeType, GnuTLS, and the other
host libraries in that script turned off. `SONAME_LIBVULKAN` stays
`libvulkan.so.1`. `winevulkan.dll` is the same size as the default tree.
The slim tree was not rebuilt after `winevulkan.so` started linking;
the `.so` measured above is the default tree.
SDL remains in `libSceVideoOut` and FMOD. DXMT and `madeira-d3d12` are
not built.

## Hosted runners

On `54e5815` the Linux job passed: patches, lavapipe `hard_fail=0`, the
Linux AnyPS5 build including `libSceAgcDriver`, the 17 relinker tests,
and the synthetic PE under Wine. The `synthetic-pe` job passed again on
`2d7c2b8` and uploaded `sample.exe`.

On `2d7c2b8` the `winevulkan` job passed. A fresh configure (FreeType
and fontconfig off) found `-lvulkan` as `libvulkan.so.1` and linked
`ntdll.so`, `win32u.so`, and `winevulkan.so`, each an x86-64 ELF.
`win32u.so` NEEDs `ntdll.so`, `libm.so.6`, and `libc.so.6`.
`win32u/vulkan.o` contains `libvulkan.so.1`. The libraries were not
executed. Sizes were not printed on that runner; the byte sizes above
are still the local VM link.

On `a54b497` the `winevulkan` job passed again. `synthetic-pe` passed
again.

`moltenvk` on `macos-15` (`a54b497`) is the capability log. Stock
`vkCreateInstance` returned `VK_ERROR_INCOMPATIBLE_DRIVER` (-9).
`stock-anyps5-device-count` is 0. With
`VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` the count is 1.
`VK_EXT_metal_surface` is present. The device is `Apple Paravirtual
device`, integrated, API 1.1.357, graphics+compute. This is the
runner's virtual GPU, not an iPad. Every `HARD` line passed, including
`VK_KHR_8bit_storage`, `storageBuffer8BitAccess`,
`VK_KHR_buffer_device_address`, `bufferDeviceAddress`, `shaderInt64`,
`VK_KHR_shader_float_controls`,
`shaderSignedZeroInfNanPreserveFloat32`,
`vertexPipelineStoresAndAtomics`, `fragmentStoresAndAtomics`,
`samplerAnisotropy`, and `textureCompressionBC`. `VK_KHR_swapchain` is
present; no surface was created. The device advertises
`VK_KHR_portability_subset`. Summary: `hard_fail=0`,
`optional_missing=7`, exit 0. The optional misses are mesh shader,
shader clock, `VK_EXT_depth_range_unrestricted`,
`VK_EXT_primitive_topology_list_restart`, `VK_EXT_image_view_min_lod`,
`shaderFloat64`, and `depthBounds`. No `VkDevice` was created by AnyPS5.

The same commit's `macos-14` MoltenVK package built, then the
portability `vkCreateInstance` aborted (exit 134) inside
`MVKPhysicalDevice::initMetalFeatures`: `AppleParavirtDevice` does not
implement `newArgumentEncoderWithLayout:`. No `HARD` lines. That runner
is dropped from the matrix. It is not the measurement above.

`fex-wine` on `a54b497` linked native FEX, installed `wine64` 9.0 from
ubuntu-base, and ran the nostdlib guest to exit 42. `/usr/lib/wine/wine64`
then exited 1 with `wine: '/tmp' is not owned by you, refusing to
create a configuration directory there`. The PE did not start. The
prefix is now `/home/fex/prefix`, owned by the runner user. That run is
not recorded yet.

`winevulkan-ios` on `macos-14` stopped in Wine configure: `aarch64 PE
cross-compiler not found`. No iOS translation unit was compiled. The
script now downloads llvm-mingw `20260421` (`aarch64-w64-mingw32-clang`)
and the job runs on `macos-15`.

`madeira-simulator` on `macos-14` reached `xcodebuild` with Xcode 15.4
and failed in Swift: `BGContinuedProcessingTask` is not in that SDK
(`SteamDownloadBackground.swift`, `JITPairing.swift`). The job now
selects Xcode 26 or later on `macos-15`. That build is not recorded yet.

`linux` and `winlibs` on `2d7c2b8` and `a54b497` were cancelled before
a runner was assigned. The check annotation is `The job was not acquired
by Runner of type hosted even after multiple attempts`, at 15 minutes,
and the next push had not happened yet. Each workflow had one
`pull_request` run. `cancel-in-progress` did not fire those two
cancels. The concurrency group was still `*-${{ github.ref }}`, which is
`refs/pull/1/merge` for every push to this PR, so a newer push cancels
a workflow that is still pending in that group. The group is now the
commit SHA, and `cancel-in-progress` is false. If `windows-latest` or
`ubuntu-24.04` is cancelled before it starts, the same steps run on
`windows-2022` or `ubuntu-22.04`.

On `f1d3f06` the primary `linux` and `winevulkan` jobs passed, and
`linux-fallback` was skipped. The `winlibs` job passed and
`winlibs-fallback` was skipped. GCC is `g++.exe (MinGW-W64
x86_64-ucrt-posix-seh, built by Brecht Sanders, r7) 15.2.0`. CMake is
`/c/Program Files/CMake/bin/cmake.exe`. `APS5_SLIM=ON`. The link lines
are `core/libs/libs/unpatched/libc.prx` and
`core/libs/libs/unpatched/libSceAgcDriver.prx` under
`build/anyps5-winlibs`. The script printed both paths and then exited
0. It did not print byte sizes. `libkernel.prx` was linked as a
dependency of those targets. Nothing ran the PRX files.

`synthetic-pe` on `f1d3f06` was cancelled with the same
`not acquired by Runner` annotation. `fex-wine` was skipped, so the
`/home/fex/prefix` PE run is still unrecorded. The PE job now retries
on `ubuntu-22.04` when the `ubuntu-24.04` job is cancelled.

`winevulkan-ios` on `f1d3f06` and again on `c161948` (`macos-15`,
Xcode 26.3, llvm-mingw `20260421`) configured Wine. Configure said
libvulkan and libMoltenVK development files were not found. `ntdll.so`
linked. `win32u.so` failed: Apple ld reported missing
`_ios_srcwatch_arm`, `_ios_srcwatch_arm_geom`, and
`_winios_dump_srcbits` from `dibdrv_PutImage`. `weak_import` on
`c161948` did not change that. The three symbols are now weak
definitions in `bitblt.c`. That link is not recorded yet. The iOS
`-c` steps have not run.

`madeira-simulator` on `f1d3f06` selected Xcode 26 and failed at
`FEXBridge.mm:13` because `FEXCore/Config/Config.h` was not on the
search path. On `c161948` the script symlinked Madeira's nested FEX
path at the pinned tree and ran CMake. CMake identified AppleClang
17 and `/usr/bin/cc`, then failed: `string no output variable
specified` and `Unsupported processor type` (empty
`CMAKE_SYSTEM_PROCESSOR`). The script now passes `arm64`, the
iphoneos SDK path, and `xcrun` clang. `libdxmt_combined.a` is not built from llvm-ios. The simulator script
compiles `scripts/slim-dxmt-stub.c` into that archive (no-op symbols).
`x86_64-vcruntime` is Microsoft's redistributable
(`tools/fetch-vcruntime.md`); no Madeira script builds it, and the
slim path leaves the folder empty.

`fex-wine` on `c161948` (`ubuntu-24.04-arm`) linked native FEX, the
nostdlib guest exited 42, and `wine64` 9.0 was installed. The PE
exited 1: `wine: chdir to /home/fex/prefix : No such file or
directory`. The prefix directory is now created before that `chdir`.
That run is not recorded yet.

`linux` and `winevulkan` on `c161948` were cancelled before a runner
(`not acquired by Runner`, 15 minutes). `linux-fallback` was skipped
in the same second, so the `needs` condition never started
`ubuntu-22.04`. Those two jobs now also start on `ubuntu-22.04`
without waiting. `winlibs` on `c161948` got a runner and then failed:
`The hosted runner lost communication with the server`. That is not a
compile error. The `f1d3f06` PRX paths above still stand. A
`windows-2022` job runs when that result is not success.

On `4b6d5a4` these jobs passed: `linux` and `winevulkan` on
`ubuntu-24.04`, `winevulkan-ubuntu-22`, `moltenvk` on `macos-15`
(same portability `hard_fail=0` result), `synthetic-pe`, and
`winlibs`. The `winlibs` log printed `libc.prx` at 875965 bytes and
`libSceAgcDriver.prx` at 7068172 bytes, both under
`build/anyps5-winlibs/core/libs/libs/unpatched/`, `APS5_SLIM=ON`.
The PRX files were not executed. `winlibs-fallback` was skipped.

`winevulkan-ios` on `4b6d5a4` (`macos-15`, Xcode 26.3, Apple clang
17.0.0) linked `winevulkan.so` as a Mach-O 64-bit arm64 shared
library, and the iOS SDK `-c` steps produced
`build/ios-drafts/moltenvk_static_loader.o`, `vulkan_metal_ios.o`,
and `vulkan_ios.o`. Those objects were not executed. No `VkDevice`
was created.

`linux-ubuntu-22` failed in `make -C tools/vk-requirements`. Ubuntu
22.04's Vulkan headers do not declare
`VkPhysicalDeviceMeshShaderFeaturesEXT`,
`VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR`,
`VK_EXT_MESH_SHADER_EXTENSION_NAME`,
`VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME`,
`VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME`, or
`VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`. The `ubuntu-24.04`
job compiled the same file and passed. The tool now skips the two
optional feature structs when the header does not name them, and
supplies the portability names when they are missing. That compile
is not yet a hosted-runner result. On this VM, with current headers,
lavapipe still reports `hard_fail=0`.

`fex-wine` on `4b6d5a4` ran the nostdlib guest to exit 42 again.
`wine64` then exited 1: `wine: chdir to /home/fex/prefix : No such
file or directory`. The script had created that directory inside the
rootfs. FEX passes `chdir` and `mkdir` to the host, so the rootfs
directory is not the one Wine changes into. The prefix is now
`$HOME/fex-prefix` on the host, and the PE is copied to both
`$HOME/sample.exe` and the same path inside the rootfs. That run is
not recorded yet.

`madeira-simulator` on `4b6d5a4` configured FEX with the iphoneos SDK
and `xcrun` clang (the empty-processor failure is gone) and then
stopped. `External/unordered_dense`, `External/xxhash`,
`External/fmt`, and `External/range-v3` were not checked out, and
`Scripts/aarch64_fit_native.py` could not import `packaging`.
`NeedDisabledSVE.py` did not find `/proc/cpuinfo`; cmake continued
with "Platform has bugged SVE". The DXMT stub did not run. The script
now checks out those four FEX submodules and sets `TUNE_CPU=none` so
the native CPU probe is not used for an iOS cross build. That
configure is not recorded yet. The app target still lists
`libntdll_unix.a`, `libwin32u_unix.a`, `libwineserver.a`,
`libavformat.a`, and `libmadeira_rppairing.a`, which this script does
not build.

On `dba67bd` the Ubuntu 22.04 capability tool compiled and ran.
lavapipe (LLVM 15.0.7) API 1.3.255, `hard_fail=0`,
`optional_missing=5` (mesh shader, fragment shader barycentric,
`VK_EXT_image_view_min_lod`, `VK_KHR_maintenance8`, `depthBounds`).
`VK_KHR_portability_enumeration` was absent. The same step then
failed to compile `moltenvk_static_loader.c`: `RTLD_DEFAULT` is
undeclared in that glibc unless `_GNU_SOURCE` is set. The loader
now defines `RTLD_DEFAULT` as `((void *)0)` when the header does
not.

On `2e74b7d` that loader compiled. `linux-ubuntu-22` then failed in
the MinGW `GuestArena.cpp` step. Ubuntu 22.04's mingw-w64 reports
`std::mutex` does not name a type in `WindowsMappings.hpp`, and
`MEM_RESERVE_PLACEHOLDER`, `MEM_REPLACE_PLACEHOLDER`,
`MEM_PRESERVE_PLACEHOLDER`, and `MEM_COALESCE_PLACEHOLDERS` are
undeclared. The `ubuntu-24.04` job compiles the same TU and passes.
`scripts/check-linux.sh` now compiles a probe with `<windows.h>` and
`<mutex>` and skips the MinGW TU when that probe fails. That skip is
not a hosted-runner result yet. The rest of the 22.04 AnyPS5 build
has not been reached.

`fex-wine` on `dba67bd` ran the nostdlib guest to exit 42. The host
prefix `/home/runner/fex-prefix` existed, and a guest `ls` of it
exited 0. `wine64` then printed `wine: could not load kernel32.dll,
status c0000135` and exited 53. Ubuntu stores that DLL at
`/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/kernel32.dll`.

On `2e74b7d` the script found that file under the rootfs and set
`WINEDLLPATH=/usr/lib/x86_64-linux-gnu/wine`. The nostdlib guest
still exited 42, the guest `ls` of the host prefix still exited 0,
and Wine still printed `could not load kernel32.dll, status
c0000135` twice and exited 53. `WINEDEBUG=-all` hid the search.
wineserver opens that path from a second process. The script now
bind-mounts that Wine directory and `/usr/share/wine` onto the host
at the same paths, unregisters qemu's x86 binfmt handlers after the
chroot install, and runs Wine with `WINEDEBUG=+module,+file`. That
run is not recorded yet.

`madeira-simulator` on `dba67bd` configured and built the iOS FEX
archives (`libFEXCore.a` 4,867,464 bytes, `libFEXCore_Base.a`
238,360 bytes) and compiled the slim `libdxmt_combined.a`.
`xcodebuild` then failed in `MadeiraJITHelper`: StikJIT's
`arm64-apple-ios.private.swiftinterface` was emitted by Swift 6.4
(`Swift::Sendable`), and Xcode 26.3 reports `expected '{' in struct`
at that colon.

On `2e74b7d` the script rewrote those `::` qualifiers to `.`.
`xcodebuild` then reported `'StikJIT' is not a member type of enum
'StikJIT.StikJIT'` and `failed to build module 'StikJIT'`: the SDK
was built with Apple Swift 6.4 (swiftlang-6.4.0.34.1) and the
compiler is Apple Swift 6.2.4 (swiftlang-6.2.4.1.4). The unsigned
CI build now drops the Madeira target's dependency on
`MadeiraJITHelper` and leaves the appex out of the embed phase.
`StikJITHelper.swift` in the app does not import that module.
`libntdll_unix.a`, `libwin32u_unix.a`, `libwineserver.a`, the FFmpeg
archives, and `libmadeira_rppairing.a` are still absent.
`build/wineserver/build.sh` exits with `No base libwineserver.a
found` on a clean tree. That `xcodebuild` is not recorded yet.

## Device boot

The signed runtime has booted on an iPad Air 13-inch M2. The PS5 build of
Dreaming Sarah launches from the app library and is playable in a basic
sense. See the [README](../README.md) and
[IMPLEMENTATION.md](IMPLEMENTATION.md). The plist key in
`patches/madeira/0002` still does not grant extended virtual addressing by
itself; the provisioning profile has to include it. No game dump belongs in
this repository.

## Conservative scalar wave branches: 2026-10-08

AnyPS5 0027 replaces a wave-mask branch's subgroup vote with its already sensed
Boolean only when a bounded proof establishes uniform scalar draw data and pure
operations. Vertex inputs, lane IDs, phis, unknown reads, cyclic expressions and
varying resource addresses retain the collective path. Capability analysis and
branch emission use the same proof; unsupported GPU feature checks stay active.

Sixteen synthetic emitter/capability cases and the existing wide-subgroup test
pass under local x86 Wine. Fourteen complete private device shader captures
replay and pass `spirv-val --target-env vulkan1.3` before and after the change.
Exactly one captured vertex shader changes: its unnecessary ballot and subgroup
capabilities disappear. Thirteen other complete shaders remain byte-identical.
One additional capture is incomplete and is not counted as a successful replay.
The driver builds and its prepared private overlay passes import/NID checks;
the iPad candidate has not been installed or tested. See the
[qualification record](evidence/solitaire-uniform-vertex-branch.json).

## Zero-invariant stencil transfer: patch0060

Exact `APS5_ZERO_STENCIL_INVARIANT=1` enables this default-off experiment.
It admits only D32S8 with eight logical samples in two native four-sample
groups, a zero full original S8 snapshot including tiled padding, and both
faces set to ALWAYS, compare/write masks 255, reference 0 and KEEP/ZERO/REPLACE
operations. Depth consumers and fragment depth/stencil exports are excluded.
Writable admission and the existing shader, resource and alias guards remain.
D16S8 and other unqualified states retain the general stencil transfer.

Every admitted draw clears only S8, including on a reused depth image, with
the exact per-group sample-location barriers. Z, actual geometry, discard,
color draws and all eight logical samples stay intact. The helper creates no
programs, buffers or descriptors. The existing fence, fault checks and resource
writeback still precede identity `WriteChanged(original, original)`, preserving
pending-writer ordering, writable validation, tracking and newer CPU/alias bytes.

Native MoltenVK comparisons pass 48 pairs and 48 missing-clear controls on
repeatedly poisoned 17×19 and 73×41 images, with exact color/S8 and independently
initialized nonzero Z checks. Both face directions and discard are observed.
The actual Windows fixture passes with bulk unset/0/1/yes. Its initial global
generation equality assumption was corrected: two clean collection walks
advance collection epochs without adding written stamps. No production tracker
change was needed. The unchanged 0059 contract and independent critical review
pass. These checks use a synthetic GPU image/pipeline owner and deterministic
CPU interleaving; their publication-time limits remain in the immutable
[source/native record](evidence/zero-stencil-invariant-native-20261009.json).

The separate [completed build checkpoint](evidence/zero-stencil-invariant-build-20261009.json)
independently reapplies and reverses all 60 patches against 152 frozen source
paths and verifies 53 HLE/runtime binaries, 22 CPU/API logs, three additional
tracking/resource/Graphics suites and their build command. Private preparation
verifies 137 files, six guest dependency sets, 47 explicit HLE providers and
actual native import/forwarder and guest NID closure. Only three HLE binaries
and two preparation logs differ from 0059; the game and five guest modules
retain identical bytes and matched lifecycle tools. A later 720-second iPad run
logs the actual clear and visibly verifies stock draws, a legal 2♣ onto A♥ move
exposing 9♣, and B undo. Its unchanged strict parser accepts all 678 rows and
reports 11.222 completion intervals/s over 700.2 seconds, with missed vblanks and
unbounded percentile overflow above 25.6 ms. Different deals/routes prevent a
matched speed comparison. Audio, save/load, lifecycle, full gameplay and 60 FPS
remain unqualified. Runtime and metadata restore. See the [separate controls
record](evidence/ipad-solitaire-zero-stencil-invariant-controls-20261009.json); the
[earlier OS-automation-blocked attempt](evidence/ipad-solitaire-zero-stencil-invariant-first-run-20261009.json)
remains a separate historical run. No speed gain is inferred.

## GPU color snapshot seeding: patch0061

Exact `APS5_GPU_COLOR_SAMPLE_STAGING_COPY=1` replaces one additional CPU copy
in the qualified GPU color sample-transfer path. `GuestMemory::Read` fills an
owned coherent baseline buffer directly. A full GPU copy seeds a separate
result buffer, including tiled padding, before the existing compute/raster
transfer and actual draw. The baseline is never a GPU destination. The existing
fence, writable-range admission, resource writeback and changed-byte guest commit
remain; unqualified layouts and the unset switch retain the previous path.

For the captured eight-sample 1920 × 1080 layout this removes a 66,846,720-byte
CPU `memcpy`. It does not omit the original guest read, GPU readback or fence.
Submission/unwind order preserves both buffers until recorded or pending GPU
work can no longer use them. Allocation failure remains explicit. The existing
bounded buffer pool can retain the new baseline usage class alongside older
allocations, so identical payload size is not proof of identical physical memory.

Production-helper ownership contracts, 60 native raster comparisons and 15
omitted-copy negative controls pass. The native probe uses explicit allocator
and guest-reader adapters; it does not execute the entire production Draw or
BufferPool. Four actual Windows guest-memory modes verify alias, padding and
protection behavior. See the
[focused source/native record](evidence/color-sample-staging-copy-native-20261009.json).
The complete 61-patch/154-path stack, 53 HLE binaries, 25 host suites and private
137-file preparation pass separately in the
[build checkpoint](evidence/color-sample-staging-copy-build-20261009.json).

A separate 720-second iPad run logs actual snapshot seeding and shows Golf's
full board. Stock draw, stock undo and column selection respond visibly. Two A
attempts produce no visible tableau change; no legal move is qualified in this
run. The strict reader accepts all 675 display rows, reporting 8,388 completion
intervals over 699.449 seconds with 33,580 missed vblanks. The final native
footprint is 4,304 MiB, with a 4,365-MiB sampled telemetry peak. This mixed route is not a matched
speed comparison or proof of unique displayed game FPS, audio or resource
stability. The original runtime and metadata restore. See the
[separate device record](evidence/ipad-solitaire-color-sample-staging-copy-controls-20261009.json).

## Unobserved stencil-draw viewport depth: 2026-10-08

AnyPS5 0028 admits an out-of-range viewport depth on restricted hosts only when
that depth has no consumer: no depth test, write, bounds, bias or clamping, and a
single known fragment module with no `FragCoord`/`FragDepth` interface or depth
replacement. It substitutes [0,1] for that unused viewport range in both normal
and recipe draw paths. Clip coordinates/convention, XY, interpolation and stencil
state stay intact. Observable or unknown cases retain the original range and
normal Vulkan rejection. Supported native ranges are unchanged.

The captured stencil draw uses [-1,1], depth testing/writing disabled, and a
matched fragment shader with no depth-coordinate builtin. Its host metadata replay
changes from a depth-range validation failure to an accepted [0,1] range. Empty
scratch reservations satisfy color-address range checks; no captured image data
or GPU work is replayed. The eligibility and depth/stencil state suites pass.
The full graphics suite still fails an interpolation contract at PC 0; an earlier
full run also failed interpolation there. This does not qualify the whole renderer.
The candidate driver and dependency audit pass, with device testing still blocked
by the locked iPad. See the [record](evidence/solitaire-unobserved-stencil-depth.json).
