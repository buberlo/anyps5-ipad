# 3D MiniGolf — PPSA03647

Version **01.000.000** now has a freshly prepared six-patch combined runtime
package against complete AnyPS5 main `d70b8998`. All twelve decrypted ELFs relink,
the dependency audit passes and all 170 package files are verified. **Native
Windows execution passes all eight isolated array-load prerequisites and stops
at another fragment-shader rejection; iPad execution of
this package remains unqualified.** The earlier Native55
package attempt reports the new runtime's unsupported `nullDescriptor`
requirement; its history and the preceding Native54 JSON allocator failure are
preserved below. Rendering and gameplay remain unqualified.
The target is a complete offline single-player course with touch input, sound
and save/load; there is no initial FPS acceptance threshold.

## MRT color-export candidate, 2026-10-10

The seventh request now passes the unchanged EXEC/CFG/whole-quad proof. The
new footprint models compressed and plain color exports to MRT0–7, including
enabled packed source pairs, final-export and valid-mask metadata. Raw encoding
and decoded operands must agree; depth-export restrictions remain intact.
No translator, interpolation or transfer semantics are weakened.

All 234 retained public test bodies are unchanged. Another 68 cases cover 28
accepted and 40 rejected forms. The actual graphics PRX passes all 302 public
cases and materializes all seven complete requests; the previous six SPIR-V
and binding-layout pairs remain byte-identical. Exports, native imports and
3,186 guest NIDs still close. Optimized and ASan/UBSan native checks pass 239
distinct namespace cases; three deliberate missing-read/metadata mutations
are detected. The actual-library experiment completes 37 commands and 17
result scopes, with both owned Wine prefixes closed.

The seventh SPIR-V translates to MSL with and without argument buffers. Both
variants create Metal 2.4 source libraries on Apple M3 Pro with fast math
disabled. This does not qualify a GPU pipeline, iPhoneOS compilation, game
rendering or iPad execution. The qualified repairs are saved in the canonical
source and cumulative patch0004. Their execution evidence belongs to the
isolated driver; the next native Windows game comparison and a fresh production
rebuild remain pending.
[MRT-export qualification](evidence/minigolf-mrt-export-candidate-20261010.json).

## Scalar-pair and array-load Windows comparison, 2026-10-10

The isolated driver passes all eight prerequisites on native Windows, including
234 public cases through the actual PRX. The retained tests pass 45 baseline
cases, 12 logging cases, 553 AVX-verified exception deliveries and 36 memory
checks. The Radeon 780M passes BDA/8-bit and BC1 texture readbacks. All 217
manifest files and 170 game-package files are verified; 168 game files remain
unchanged, with only the graphics PRX and manifest replaced.

MiniGolf exits after 7.180 seconds with `0xc0000409` at the interpolation guard
(`pc=0`). Its seventh distinct complete shader request is 1,999 bytes. Native
decoding reproduces the rejection: 14 instructions in one CFG block, with
eight unknown register footprints for compressed color exports to MRT7 through
MRT0. The other six instruction effects are known. The shader overwrites the
raw center inputs before exporting and uses no interpolation, implicit sampling
or whole-quad instructions. The existing translator supports these exports;
the missing proof concerns enabled source reads and valid-mask/done variants.
The swapchain
remains 837 × 471 with three images. A transient black window is observed, but
the process exits before a bounded frame capture; game rendering remains
unqualified. The owned game/reference processes and transfer service are closed.
This result does not qualify iPad execution or gameplay.
[Native Windows comparison](evidence/minigolf-array-load-candidate-20261010.json).
[Exact MRT-export diagnostic](evidence/minigolf-mrt-export-diagnostic-20261010.json).

## Lane-math Windows comparison, 2026-10-10

The repaired isolated driver passes 178 public cases through the actual loaded
PRX on native Windows. All eight prerequisites pass, retaining 45 baseline cases,
12 logging cases, native AVX/exception checks and Radeon 780M synthetic offscreen
readbacks. With a fresh test directory, title saves and shader cache, MiniGolf
then exits after 6.312 seconds with `0xc0000409` at the interpolation guard.
The new complete request is 2,463 bytes and differs from all five previous
requests. Native decoding finds 45 instructions in six reducible CFG blocks;
the remaining missing effects are scalar-buffer `DWORDx2`, 2D-array image load
with three address words, and MSAA-array image load with four. Neither implicit
sampling nor WQM appears in this request. The three missing register-effect
forms now pass native qualification: 56 new cases and 115 retained namespace
cases pass optimized and with ASan/UBSan; the complete 234-case fixture passes
syntax checking. Three deliberate omissions of the second scalar write, array
layer and MSAA sample are detected. The complete sixth request passes the
unchanged proof with all 45 instruction effects recognized. The actual loaded
PRX passes all 234 public cases, six 235-case imported runs and all six complete
shader materializations. The previous five SPIR-V and binding-layout outputs
remain byte-identical; exports, native imports and 3,186 guest NIDs still close.
This isolated one-TU experiment retains historical serializer/cache identity;
a fresh production rebuild remains pending. The subsequent native Windows game
comparison with this candidate is recorded above.
Seven independent CPU witnesses also pass optimized and with ASan/UBSan,
checking observable layer/sample selection, aliased address snapshots, scalar
mask invalidation, sparse channels and inactive or subsequently restored lanes.
All three deliberate effect omissions are detected in both modes. These tests
use an integer memory oracle; they do not qualify GPU descriptors or rendering.
The sixth SPIR-V translates to MSL in both argument-buffer modes and creates
Metal 2.4 source libraries on Apple M3 Pro with fast math disabled. No GPU
pipeline, command submission or iPhoneOS compilation is qualified by that check.
The swapchain is still 837 × 471
with three images, and quality settings are unchanged. The process exits before
the first bounded window capture; no rendered game image is established.
Both the owned game/collector and peer-only transfer service are closed.
[Qualification and Windows checkpoint](evidence/minigolf-lane-math-candidate-20261010.json).
[Sixth-request native qualification](evidence/minigolf-array-load-candidate-20261010.json).

## Volume-sampler Windows comparison and next shader, 2026-10-10

The isolated volume-sampler driver passes all eight native Windows prerequisites:
144 public cases through the actual PRX, 45 retained baseline cases, 12 logging
cases, four CPU/exception targets and the Radeon 780M offscreen probe. Native
Windows verifies AVX and 553 exception deliveries; this does not erase the
separate local-Wine AVX limitation. The original package remains unchanged;
this comparison uses a fresh test directory, shader cache and title saves.

MiniGolf exits after 6.455 seconds with `0xc0000409`, before the first bounded
window capture. Its swapchain remains 837 × 471 with three images. The new
2,975-byte Serializer15 request is distinct from the preceding 3D-sampler
request. Both the imported fixture and complete-request replay reproduce its
interpolation-guard rejection locally; all 144 existing public cases still pass.
The complete request roundtrips exactly and owned Wine cleanup is confirmed.

The native decoder finds 76 instructions in twelve reducible CFG blocks with
a scalar loop and EXEC-mask branches. Twelve instruction footprints are missing:
three occurrences each of `VCmpNleF32`, `VLogF32`, `VExpF32` and `VMadakF32`.
This shader contains image loads, with no implicit `ImageSample` or `S_WQM_B64`.
The narrow repair therefore concerns register-read/write effects and VCC identity;
it does not require relaxing the whole-quad derivative or multi-block WQM guards.
The driver used in that comparison still rejects this shader; no repaired
gameplay is claimed.
[Windows comparison and exact replay](evidence/minigolf-volume-windows-next-shader-20261010.json).

An isolated candidate now admits only the exact four register-effect forms above.
Its 34 new decoder cases pass optimized and with ASan/UBSan; the retained
81-case native suite also passes with the new header. The full fixture retains
144 cases and adds 34, for 178 cases. The complete private shader
passes native decoding and the unchanged CFG proof. Seven independent CPU
witnesses check read-before-write, inactive-lane preservation and both saved
VCC identities; deliberately omitted MADAK reads and stale VCC-high identities
are detected. Existing shader arithmetic is unchanged.
The first actual PRX attempt builds successfully but stops before runtime tests
at the export-ABI gate: the compiler emits an internal unary `Footprint` lambda,
which MinGW automatically exports, adding one symbol and shifting later ordinals.
Native imports are unchanged. That failed attempt and its successful owned
cleanup remain recorded. A separate read-only review proves that none of the
package's 5,115 native import entries uses this internal helper. Excluding exactly
that automatic export restores all 3,692 names, ordinals and forwarders; native
imports remain identical. A fresh isolated link then passes 178 public cases,
five imported 179-case runs and five full Prepare/Capture/Materialize replays,
Graphics/DiskCache regressions and prior-runtime positive/negative controls.
All 3,186 guest NIDs remain resolved across 62 consumers and 50 providers;
both owned Wine prefixes are closed. The previous four SPIR-V modules and binding
layouts are byte-identical. The fifth module translates to iOS MSL 2.4 in both
argument-buffer modes. The offline Metal frontend is unavailable. A separate
bounded native helper successfully creates both fifth-request source libraries
through the macOS Metal runtime on the M3 Pro, using Metal 2.4 and
`fastMath=false`; each exposes `main0`. This creates no render pipeline or GPU
commands. Offline AIR, iPhoneOS compilation and iPad GPU behavior remain
unqualified. This isolates the CPU/PRX repair; it is not
a full production rebuild or a repaired-game rendering result.
[Isolated candidate evidence](evidence/minigolf-lane-math-candidate-20261010.json).

## Earlier 3D texture sampler diagnostic, 2026-10-10

The isolated quad-mask driver passes all eight prerequisites on native Windows,
including 121 cases through the actual loaded PRX and asynchronous AVX-context
checks. The physical Radeon 780M Vulkan probe passes device creation, buffer
device address, 8-bit storage readback and BC1 texture sampling readback. These
synthetic GPU checks do not establish correct game rendering.

The verified 170-file game package opens a black window and exits after
6.275 seconds with `0xc0000409` at the interpolation guard. Its swapchain is
837 × 471 with three requested and three actual images. The bounded collector
returns complete logs and no images; its own successful exit is distinct from
the game's failure. Resolution and interpolation settings are unchanged.

The complete new 2,111-byte Serializer15 request roundtrips exactly and
reproduces the rejection through the qualified actual driver in local Wine.
The imported public fixture passes all 121 existing cases before rejecting
that request. Its decoder identifies one missing footprint: plain implicit-LOD
`IMAGE_SAMPLE` with a 3D image and three coordinate VGPRs at byte PC 48. The
other fourteen instruction footprints are already admitted. The guard reports
shader-start PC 0; this does not identify the unsupported instruction itself.

The current no-VINTRP liveness proof admits only the two-coordinate 2D form.
The repair must validate the exact 3D encoding and all three address words,
including helper-lane reads used for implicit derivatives. Merely changing the
dimension check would leave the third coordinate outside that derivative
proof. An isolated candidate now passes 81 decoder/CPU-model cases in both
optimized and ASan/UBSan runs, a separate geometric dependency model, and a
control that deliberately omits the third helper-coordinate check. The complete
144-case fixture and all four complete private requests now pass through the
actual loaded candidate PRX, including prepare, capture and materialization.
The previous quad PRX rejects the new positive case and fourth complete request.
The original run's sole failure was its strict cleanup classification: the
compiler server was already absent. A separate immutable reconciliation verifies
both owned prefixes' unlocked server locks, absent sockets and process closure,
and retains every original input, artifact and successful test log. The failed
original receipt is preserved. The candidate has not been promoted into
production sources. Its subsequent native Windows comparison is recorded
above; correct game rendering remains unqualified.
The complete production build of the preceding quad-mask source is sealed:
47 libraries, 53 passing host cases, 121 actual-PRX cases and three full private
requests. Local Wine's internal AVX-context subcase remains explicitly skipped.
Neither work establishes visible game frames, iPad execution, audio, save/load
or playable gameplay.
[Windows and exact-driver diagnostic evidence](evidence/minigolf-3d-sampler-diagnostic-20261010.json).
[Isolated 3D sampler CPU evidence](evidence/minigolf-3d-sampler-candidate-20261010.json).
[Actual 3D PRX and cleanup reconciliation](evidence/minigolf-3d-sampler-actual-prx-20261010.json).
[Fresh quad-mask production runtime](evidence/minigolf-quad-mask-full-production-20261010.json).

The fourth actual materialized shader also translates to iOS MSL 2.4 with
Argument Buffers both disabled and enabled, retaining a `texture3d` resource.
The earlier three shaders are byte-identical, so their six verified translation
results are reused. The selected Xcode's Metal frontend remains unavailable;
no AIR, MoltenVK pipeline or GPU execution is established by these translations.
[3D MSL preflight](evidence/minigolf-3d-sampler-msl-20261010.json).

## Further textured shader, 2026-10-10

The separate repair now passes **121 public cases through an actual loaded
`libSceAgcDriver.prx`**, all three complete retained MiniGolf requests, and the
graphics and shader-cache regression programs. Prepare, capture and
materialization all run through that PRX. The previous driver rejects the new
positive case and the complete textured request, as expected.

The proof accounts for the exact eight-word scalar-buffer load, non-fused MAD,
plain 2D implicit-LOD sample and `S_WQM_B64 EXEC, EXEC`. Full writes made while
the entire quad is active can cover the original entry mask; helper-coordinate
reads additionally require definitions in the same unchanged quad epoch.
Unknown masks, partial writes, descriptor or saved-EXEC aliases and unmodelled
effects remain rejected. WQM is limited to one straight-line guest CFG block;
divergent or reconverging blocks do not inherit this proof.

The raw and NID-patched driver preserve export names, ordinals and forwarders;
all 3,692 raw exports and native imports match the baseline. Two exactly named
internal proof helpers are excluded from automatic export. Only one translator
object is replaced in the experimental archive; this is not a full production
rebuild. A synthetic test's saved-EXEC registers initially overlapped its sampler
descriptor. Both affected fixtures now use a separate register pair, with an
explicit decoder check for descriptor preservation.

All three actual materialized shaders translate to iOS MSL 2.4 with Argument
Buffers both disabled and enabled. The selected Xcode lacks the separate Metal
Toolchain, so no Metal frontend compilation occurred. This check also does not
create a MoltenVK pipeline or execute GPU work. A separate diagnostic changes
only the retained requests' host subgroup size from 64 to 32, after checking
their original serialization. All three still prepare and materialize through
the actual PRX, producing byte-identical SPIR-V and unchanged resource layouts;
their six MSL translations also pass. This is a CPU target check, not a report
of physical Apple GPU features. The subsequent Windows comparison is recorded
above; physical iPad rendering remains pending. The qualified source and exact MinGW export
exclusions have now been integrated into patch0004. The fresh complete
production runtime build is now sealed with source-derived cache identity
`0xee2929b1aa766da2`; its 121 public and three full private checks pass through
the freshly built PRX. The recorded game comparison uses the preceding isolated
PRX; neither build is the later 3D extension.
[Isolated quad-mask compiler evidence](evidence/minigolf-quad-mask-proof-20261010.json).

A separate local iPad candidate package contains all 170 checked files. Its
168 unaffected files match the combined baseline; only the qualified driver
and installation manifest differ. Fresh native-import and guest-NID checks
cover all twelve guest PE files and 3,186 NIDs without unresolved dependencies.
This package is prepared only, with no device profile update or installation.

The preceding failure that motivated this repair is retained below.

The isolated scalar-buffer/packed-conversion driver passes all eight native
Windows prerequisites, including its 63 actual-driver shader cases. With a
fresh cache, MiniGolf then exits after 9.06 seconds with `0xc0000409` at another
fixed-function interpolation guard, at byte PC 0. All 170 game files and 211
overlay payload files are checked. It exits before the first scheduled window
capture; no game picture or gameplay is established.

A local replay imports that exact `libSceAgcDriver.prx`, roundtrips the complete
2,079-byte Serializer15 request and reproduces the rejection. The pixel inputs
are perspective-center v0/v1 and position v2/v3, with no interpolators or VINTRP.
The proof lacks footprints for an eight-word scalar-buffer load, three-source
`V_MAD_F32`, plain 2D implicit-LOD sampling, and `S_WQM_B64 EXEC, EXEC`. MAD is
the decoder's non-fused operation; it must not be silently replaced with FMA.

WQM expands execution to helper lanes needed by texture derivatives. A repair
must prove that original barycentric inputs are unused in those lanes too;
an overwrite confined to the original active lanes is insufficient. The guard
remains enabled in the separately qualified CPU candidate. At that diagnostic
checkpoint, the scalar-color candidate had not been promoted. This replay performs CPU shader
preparation only. [Exact-driver diagnostic evidence](evidence/minigolf-quad-shader-diagnostic-20261010.json).

## Further scalar-color shader, 2026-10-10

The same combined package passes its native Windows CPU/AVX and physical
Radeon 780M Vulkan prerequisites. The game runs for 65.97 seconds, then exits
with `0xc0000409` at the fixed-function interpolation guard, this time at byte
PC 0. No owned foreground game window was captured; this is an execution and
diagnostic result, not a rendering or playability result.

The complete Serializer15 request contains a six-instruction constant-color
fragment shader with no VINTRP. Its four-word scalar-buffer load is followed by
two VOP3 packed F32-to-F16 conversions and a compressed color export. The current
proof does not model these two instruction forms, so it correctly rejects the
otherwise unsupported footprint.

An isolated candidate accounts for all four scalar definitions, including
saved-EXEC identity invalidation, and both packed-conversion source reads before
the full 32-bit destination write. Descriptor range/alignment, modifiers,
partial writes and inactive EXEC lanes remain guarded. It passes 63 public
compiler cases and both complete private requests through prepare, capture and
materialization. The separately named unchanged production translator rejects
the new scalar-color test as expected. This candidate is not yet promoted to
the production runtime and has no GPU or gameplay qualification.
[Isolated compiler evidence](evidence/minigolf-buffer-pack-proof-20261010.json).

The candidate also passes through an actual isolated `libSceAgcDriver.prx`,
including graphics and disk-cache regressions, all 63 public cases and both
private shaders. Its 3,692 export names and native imports match the baseline.
Only the translator object in the shader archive changes; the existing generated
cache version is retained for this experiment, so its game comparison requires
an empty, separate cache. This is not a complete production-runtime rebuild.
An independent ARM64 and ASan/UBSan model each checks 1,451,520 raw-input
comparisons, 72 descriptor snapshot cases and 15 finite-half references; seven
deliberately unsafe variants are rejected. These CPU proofs still do not
establish correct GPU output.

## Combined runtime prepared, 2026-10-10

The complete upstream plus six local patches now builds 47 HLE PRXs and passes
42 selected plus ten additional host test programs: 53 distinct cases with zero
failures or CTest skips. Local Wine cannot expose asynchronous AVX for one
internal subcase; this remains explicit. All project objects are rebuilt,
source bytes/modes and test dependencies are verified, and the 50-file HLE
payload is sealed against immutable qualification and final provenance.
[Combined runtime evidence](evidence/anyps5-combined-runtime-20261010.json).

The package combines bounded complete exception logs, `RequiresValidImages` for
drivers lacking native null-image descriptors, and the EXEC-aware no-VINTRP
proof. The latter passes 45 public staged compiler cases and the exact private
MiniGolf shader using the newly linked runtime. Enabled uncompressed MRTZ
reads and the exact ordered `S_SENDMSG 7` boundary are accounted for; unknown
side effects and actual null image entries still fail explicitly. An independent
64-lane CPU model checks 921,600 comparisons per run and detects three unsafe
mutations. These repairs do not establish execution of that shader on Metal.
[Compiler and semantic proof](evidence/anyps5-unused-barycentric-proof-20261010.json).

Fresh preparation uses matching relinker/NID tools and the sealed runtime. It
preserves the four explicit Unity lifecycle selections, relinks twelve ELFs,
resolves 3,186 unique guest NIDs and rehashes all 170 package files. There are no
missing HLE libraries, native imports or guest NIDs; all 127 original dump files
are unchanged. This new package is **prepared only**, awaiting a separately
successful import and fresh-process device run. Native55 remains the installed
host; the older imported package and launch failure below do not test this new
one. [Current preparation evidence](evidence/minigolf-combined-main-preparation-20261010.json).

The subsequent combined-package iPad import stages and reads back all 170 files,
then stops when the own app process reappears during library publication. The
receipt reports attempted publication, no successful return, and a rollback
blocked by the same closed-process guard. The staged folder is retained; this
is a failed import, with the final library state requiring a fresh readback.
The expired earlier baseline is not reused as current evidence. No gameplay
result is attributed to this attempt.

A subsequent read-only check closes the exact own-app process and reads the
library and global configuration again. Both match the original bytes, and the
older MiniGolf package remains selected. This resolves the library-state
uncertainty; it is not a fresh game-file baseline or a successful new import.
The staged combined package remains available for a separately qualified reuse.

## First full-main package and import, 2026-10-09

The [complete upstream integration](ANYPS5-MAIN-INTEGRATION.md) replaces the
earlier selective backports. All 47 real x86-64 HLE PRXs build, and 40 selected
plus ten additional host test targets pass. Their 53 positive cases run under
local Wine with CPU or mock-Vulkan checks. Wine does not expose AVX on this Mac,
so the asynchronous AVX-context subtest is explicitly unverified; native Windows
and FEX/iPad checks are tracked separately.

The matching native tools pass all 37 relinker CTest cases, including actual
emitted-PE constructor/finalizer and deferred-lifecycle fixtures. Fresh MiniGolf
preparation preserves the four explicit Unity module lifecycle selections and
checks **3,186 unique guest NIDs**, with zero missing libraries, native imports
or guest NIDs. All **170 package files**, including the manifest, are rehashed.
The original 127 dump files are unchanged; separately recorded Finder metadata
is the only added input file. None of the game files are committed.

Native55 is an existing signed host artifact. Its 382-file closure and 53 native
host input hashes are independently verified; the HLE package is qualified
separately. Native55 is installed in place with the existing bundle ID. The new
170-file game folder is imported and selected by the existing MiniGolf library
entry. Complete pre/post readbacks verify the new package and 981 files across
five protected older package directories; global configuration is unchanged.
Publication uses one same-directory AFC rename with exact library readback.
The first attempt stopped before publication after an unexpected app restart;
the recovery reused the staged files and closed one exact own-app process.
The restart's cause is unknown. The app and file-service session are closed
after publication. [iPad import evidence](evidence/minigolf-main-ipad-import-20261009.json).
The first device attempt uses the same conservative MiniGolf profile in a fresh
process, without Solitaire's optional stencil or sample-group assumptions.
[Historical preparation evidence](evidence/minigolf-main-preparation-20261009.json).

## First full-main iPad attempt, 2026-10-09

The normal library shortcut selects the exact imported package. Built-in JIT,
the JIT pool and helper detachment are observed, followed by selection of the
Apple M2 GPU. The first live log transfer fails with CoreDevice7000 and a closed
socket; the collector stops its own app/helper and obtains the final owned log.
That log contains `AGC graphics: shader runtime requires nullDescriptor`, with
no observed swapchain creation. The exact fatal-versus-cleanup chronology is
unqualified because this error is retrieved after intentional cleanup.

The new device and typed image-heap code require `nullDescriptor`; the pinned
MoltenVK source explicitly reports it as unsupported. The gate runs before guest
descriptor materialization, so this does not establish that MiniGolf accesses
null images. The subsequent patch0003 implements a `RequiresValidImages` policy:
every consumed guest image base and Vulkan image view is checked before initial
updates and cached replay. Its device-profile, descriptor, graphics and cache
suites pass locally; the device-creation code compiles. Actual null entries still
fail explicitly. This source repair has not yet been exercised on the iPad.
[Descriptor-policy evidence](evidence/anyps5-valid-image-policy-20261009.json).
No screenshots, footprint snapshots, picture, sound or gameplay are qualified
by this interrupted attempt. Global configuration, other library entries and
the Solitaire manifest remain unchanged; full protected-file inventories were
verified during import. [First-launch evidence](evidence/minigolf-main-ipad-first-launch-20261009.json).

## Matched native Windows reference, 2026-10-09

The exact 170-file package runs on the UM790's native Windows 11 installation
with the same conservative compatibility switches. Four CPU tests pass,
including 553 exception deliveries and 64 vectors in each of three AVX modes,
with no errors or internal skips. The physical Radeon 780M creates the Vulkan
device and passes buffer-device-address/8-bit and BC1 texture readback tests.

MiniGolf starts, initializes its 32-MiB save-memory slot and creates an
837×471 swapchain with three actual images. After 93.58 seconds it exits with
`0xc0000409`: the fixed-function interpolation validator rejects a fragment
shader at byte PC 56. It requires unmodified center I/J barycentrics and complete
P1/P2 sequences along a proven control-flow path. This is a concrete shared
shader-path blocker; the validator remains enabled while the shader is examined.
The exception logger truncates the serialized shader request at 1,023 characters,
so the retained request is an incomplete prefix, not a full replay fixture.

The game is not foreground at the two capture checks; no game-window image was
obtained. Neither those checks nor successful offscreen GPU tests establish
rendering or playability. Only the run's own child process trees are stopped,
and its private transfer service is closed. No security or network settings are
changed. [Windows reference evidence](evidence/minigolf-main-windows-reference-20261009.json).

## Complete shader diagnostic, 2026-10-09

A separate native Windows comparison replaces only `libc.prx` and provenance
manifests, retaining all 168 other game-package files. The bounded exception
writer passes twelve real Windows cases; the four CPU tests pass again with
553 exception deliveries and three sets of 64 AVX vectors. Physical GPU
prerequisites pass. The game still exits with `0xc0000409` at PC 56, after
11.13 seconds in this run. It exits before the first window capture, so this
comparison establishes no game picture or performance improvement.

The complete Serializer14 request is now captured: 3,108 base64 characters,
2,331 decoded bytes. An independent helper linked to the unchanged original
shader archive roundtrips its bytes and reproduces the same guard failure both
directly and through full recompilation. The actual pixel metadata declares
perspective-center v0/v1 and position v2/v3, with no interpolation attributes or
VINTRP instructions. Its six-block, 43-instruction loop saves, narrows and
restores the 64-bit EXEC mask. Ordinary vector writes therefore do not establish
that the original input is overwritten for every later active lane.

Patch0002 is now retained in source with the generic writer tests. Patch0004
adds the bounded EXEC-aware proof for shaders without VINTRP. Thirty public
synthetic cases and the exact private request pass actual preparation, capture
and materialization with equivalent final SPIR-V and bindings. The existing
P1/P2 guard stays unchanged, and unknown instruction effects remain rejected.
That initial 30-case compiler qualification is preserved independently of the
subsequent 45-case/current-runtime proof and completed combined 47-library build
above. Neither establishes GPU execution or game output.
[Historical proof evidence](evidence/anyps5-unused-barycentric-proof-20261009.json).
The private shader request and raw logs remain excluded
from Git. The comparison's own process trees and transfer service are closed;
existing packages, saves and system settings are unchanged.
[Complete diagnostic evidence](evidence/minigolf-complete-shader-diagnostic-20261009.json).

The following sections preserve earlier checkpoints. Their numbered patches
belong to the archived `legacy-6e037e98` series; the corresponding repairs are
reconciled in the initial three-patch integration for `d70b8998`; the subsequent
logging and shader/descriptor repairs have their separate qualifications above.

## Preparation checkpoint

The immutable input contains 127 files (1,331,695,275 bytes), uses Unity
2019.4.20f1/IL2CPP and supplies twelve decrypted ELF backups. All input hashes
still match the first inventory. Assets, converted executables, manifests and
raw diagnostics remain in ignored local storage.

- Fresh native relinker and NID-patcher tools were built from the patched source.
  The native host-tools suite passes 33 tests.
- Patch0067 handles omitted inert producer notes while retaining strict bounds
  for runtime segments, symbols, relocations and initializers. All twelve ELFs
  relink, preserving the eleven guest-module paths.
- The dump declares 46 HLE roots. `libSceKeyboard` is an additional native
  dependency of VideoOut, so the distribution contains 47 actual HLE libraries
  and three compiler DLLs. HMD, player-review and object-manager implementations
  are built; no empty replacement libraries are supplied.
- Patch0068 supplies the missing Audio3d object-attribute and flush exports.
  Object PCM/spatial rendering remains explicitly unsupported. This is not an
  audible-output result; see [patch behavior and limits](PATCHES.md).
- All 28 Windows host contracts pass through local Wine. The unchanged memory
  test failed at the host's 8-GiB base and passed at `0x7400000000`; the complete
  suite uses the latter explicitly. The failed address attempt is preserved.
- The final package audit checks 3,186 unique guest NIDs and has no missing
  libraries, native imports or guest NIDs. The immutable package manifest remains
  `prepared_unexecuted`; separate device records track installation and testing.

[Preparation and import record](evidence/minigolf-preparation-20261009.json).

## First runtime failure

The same package reproduces the SaveData startup null read in an isolated
Wine 11 macOS/Rosetta run. This is evidence of a shared startup problem rather
than an iPad-only GPU problem. The comparison was bounded and stopped its own
Wine prefix; it is not a Windows-GPU compatibility or gameplay result.

The failed package's generated bootstrap invokes every retained guest module's
initializer with zero arguments. SaveData's initializer instead parses a
module-start argument block, while that package's `sceKernelLoadStartModule` HLE
discards the caller's arguments. The null read therefore precedes gameplay and
does not establish a renderer failure.

Native54 stayed alive after the guest exited and returned to a library error
dialog. Consequently native process continuity is not counted as a successful
game start. No game FPS, render resolution, course, audio or save/load result
was obtained. The run closed only the project's own main app and JIT helper,
then released the shared device lease.

## Lifecycle repair checkpoint

Frozen patch0069 and promoted patch0070 address the startup contract in source.
The explicit candidate selection defers `Il2CppUserAssemblies.prx`,
`PSNCommon.prx`, `PSNCore.prx` and `SaveData.prx` until Unity's actual
`sceKernelLoadStartModule` call supplies its argument pointer. The recovered
producer supplies a 52-byte argument allocation containing a 16-byte interface
record and callback pointer. Il2Cpp is also linked from the main executable;
that linkage alone does not justify an earlier zero-argument module start.
Bundled libc and PS5Util retain eager initialization. All module mapping,
binding, TLS and ELF validation remain in place.

Bootstrap and kernel APIs use each image's shared versioned start/stop wrappers.
They preserve the full argument count and pointer, prevent duplicate callbacks,
and separate operational errors from the guest's exact return value. A failed
stop retains its result and prevents unload; finalizers that already ran cannot
roll back. Mutable state and executable wrappers occupy separate complete
16-KiB host pages. The default initialization policy remains unchanged for
packages without explicit lifecycle selections, and Solitaire's existing
package and settings are preserved.

The fresh native host-tools suite passes 34 tests. Five synthetic emitted-PE
Wine scenarios, retained-libc constructor/allocation ordering and three rejected
negative controls pass; kernel API contracts also pass native ASan/UBSan checks.
The matched HLE rebuild passes 29 Windows host contracts. A fresh twelve-ELF
package passes the complete dependency audit and is imported into Native54:
all 169 files match device readback. The existing MiniGolf card now points to
the new package through a single same-directory AFC rename. All 303 Solitaire
files, 169 files of the earlier MiniGolf package and global settings are unchanged.
The first corrected launch crosses the old startup failure and creates a
768×432 swapchain with three actual images. A transient device file-service
failure interrupts that collector after about fourteen seconds; it is not a
guest exit. A subsequent bounded retry remains live for 121 seconds with no
reported guest exit. Reviewed captures at 15, 60 and 120 seconds show Madeira's
startup screen rather than a game picture. Native footprint reaches a recorded
peak of 1,770 MiB; this short run does not establish stable resource use. The
collector stops only the app's own processes and releases the device lease.
Global settings, existing library fields and the Solitaire manifest remain
unchanged; the earlier import is the full Solitaire file verification.

This demonstrates progress through initialization, without verifying a complete
game start, picture, audible output or gameplay. The Solitaire card/Undo
regression remains pending. See
[lifecycle implementation and limits](PATCHES.md#explicit-guest-module-lifecycle-anyps5-00690070)
and [packaging selection](PRIVATE-GAME-PACKAGING.md).

## Historical save-memory failure

At this earlier checkpoint, the corrected guest requests a 32-MiB save-memory
slot. Native SaveData rejects that request with `0x809f0000` because its cap is 16 MiB, while the other
SaveData implementation already admits 32 MiB. Patch0071 raises the native cap
to 32 MiB and retains upper bounds, atomic writes and per-user/slot isolation.
This is a bounded compatibility change, not a claim about the platform maximum.

Five local sanitizer checks pass, covering creation, tail access, growth with
existing data, reopening, isolation and write failures. Negative controls reject
the original cap and a removed upper bound. Those native checks use a private
compatibility copy for an existing `chrono::clock_cast` expression unsupported
by the Mac's libc++; the production timestamp code is unchanged. The actual
Windows HLE rebuild now passes all 33 host test targets, including growth and
the separate read-failure case; no CTest case is skipped. A fresh 169-file package
passes the complete dependency audit. Its compiled provider matches the tested
archive, and the original dump and both earlier packages remain unchanged.
The fresh package is now imported into Native54: all 169 files match device
readback. The same library card is retained through AFC rename, with Solitaire's
303 files, the first MiniGolf folder's 169 files and the prior lifecycle folder's
170 files unchanged. The extra prior file is included in the preservation
check. Global settings are unchanged. A fresh bounded iPad launch records
`setupMemory2(size=33554432)` returning zero, followed by the same user/slot's
`getMemory2` returning zero. This qualifies setup and readiness; gameplay
save/load remains open.

## Historical JSON allocator failure

After the save-memory repair, startup reports an unhandled `std::runtime_error`:
`sce::Json::MemAllocator virtual ABI is not implemented; use AllocParamRtti`,
followed by a fatal abort. This is the explicit non-null allocator rejection in
the JSON HLE setter. A swapchain is created, but all three reviewed screenshots
still show Madeira's startup overlay. Native continuity and absence of a guest
exit report do not turn this fatal trace into a successful launch.

The virtual-table contract is now recovered from the original ELF relocations
and unchanged converted instructions. The allocation/release methods forward
to guest libc; notification aborts on the title's allocation-failure code.
Only one of the twelve selected ELFs imports these initialization interfaces.
Its parameter is accessed exclusively through imported methods; the available
stack span does not establish the original class layout.

Patch0072 implements typed guest virtual calls and keeps each block paired with
its creating release function, allocator object and context. RTTI remains a
distinct policy. The existing 24-byte HLE parameter representation is retained;
the explicit setters select its internal allocator kind. Default allocation
does not replace a custom allocation failure. Three native sanitizer suites,
three rejected mutation controls and all 34 Windows host targets pass, including
reentrant callbacks and concurrent policy changes. That 34-target runtime
checkpoint has no qualified iPad clearance of this failure; the later full-main
attempts and newly prepared combined package are recorded separately above. [Detailed contract and limits](PATCHES.md).

[REA's tested investigation route](BINARY-INVESTIGATION.md) confirms the caller
and argument-setting instructions. Its automatic Windows calling convention is
incorrect for the guest, and its first indirect-method query is rejected.
Original ELF/NID/relocation inspection supplies the missing table evidence;
neither tool output alone establishes the complete SDK class ABI.

The collector closes only the project's own processes and releases the device
lease. Global settings, other library fields and the Solitaire manifest remain
unchanged; the immediately preceding import verifies the full protected folders.

## Device profile and remaining checks

The earlier first device run uses the existing Native54 host and a separate
package/working directory, the lazy 4-GiB arena at `0x7400000000`, 256-MiB chunks and the
unchanged global 512-MiB JIT pool. The profile includes automatic built-in JIT,
18 touch controls, FPS-only HUD, fixed-function interpolation, conservative draw
transitions and three swapchain images. Solitaire's sample-group, zero-stencil
and experimental transfer gates are absent.

Unity's original `boot.config` requests 3840-pixel output width. The profile's
1280×720 virtual monitor is a separate setting. The observed swapchain is
768×432 in that Native54 run; game render targets and Metal layer size remain
unmeasured. The first Native55 full-main attempt has no observed swapchain, and
the latest prepared combined package still requires a new device measurement.
A separate private 1080p variant is permitted only if the original has excessive
resource cost; it must retain verified rendering and have its actual resolution
recorded.

The device importer verifies a fresh package before appending its library entry,
backs up and compares Solitaire's full directory, and preserves global settings
and existing entries. It uses a bounded shared-iPad lease with rollback time;
it neither reinstalls the native app nor replaces Solitaire. A fresh native
process is required at every title/profile change.

Remaining acceptance: three library starts, a complete offline course, aiming
and shot strength, ball movement and scoring, touch release, audible music and
effects, save/load after restart, background/resume, and at least 15 minutes of
stable gameplay with FPS, memory and thermal observations. Shared runtime repairs
also require a Solitaire card/Undo regression. A successful preparation, live
process or visible menu does not satisfy these checks.

All builds and tests are local. No GitHub Actions are used.
