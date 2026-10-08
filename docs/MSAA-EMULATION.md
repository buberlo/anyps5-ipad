# Eight-sample rendering: component tests pass, game image remains white

Solitaire's captured state requests eight raster, exposed and color-fragment
samples. The M2 iPad exposes native attachment sample counts of 1, 2 and 4.
The freshly relinked game's latest device run reaches Vulkan and a swapchain,
but its reviewed surface remains white. Patches0040–0044 connect a conservative
grouped-sample draw path in
source: sample state and pipelines, individual RGBA8/S8 transfers, sampled
texture routing, and guest-memory writeback. It is experimental and default-off;
setting `APS5_ENABLE_SAMPLE_GROUPS=1` opts in. To disable it, remove the variable;
the current implementation checks its presence. The complete Windows HLE package
is built and host-tested; a fresh Solitaire candidate passes its 47-dependency
HLE audit. Patch0045 fixes retained guest-export initialization and has separate
device startup evidence. Patch0046's canonical rectangle guard is checked
statically. Patch0047 adds restricted fixed-function color resolve; its production
fragment and rectangle stages pass an isolated iPad GPU probe. Patch0048 fixes
opaque descriptor inspection. All 48 patches are now in a rebuilt 50-PRX package
with 22 selected host test suites passing; resource/full graphics host suites
also pass. The latest freshly prepared game run has no logged draw/AGC error,
but the reviewed game surface stays white. Correct game frames remain unqualified.

Independent x86-64 programs through the installed Wine/FEX/MoltenVK stack pass
exact production-code RGBA8 and S8 transfer tests on the M2 iPad. These establish
individual sample preservation for their synthetic fixtures, with separate
depth-preservation checks in the S8 test. They do not establish correct game
draws, resolves, visible presentation, gameplay or performance. Increasing a
pipeline's sample count alone cannot repair those remaining semantics.

## Earlier lifecycle45 game startup

The bounded Build47 run uses a newly prepared package from the lifecycle-corrected
relinker, alongside the separately built 0040–0044 HLE set. Its read-back manifest
matches the receipt hash and resolves all 47 explicit HLE dependencies. Built-in
StikJIT enables successfully; ordinary memory tracking, multiblock translation,
AVX and the paired asynchronous context repairs remain active. The log selects
the Apple M2 GPU and creates a 768 × 432 swapchain with three images. No
`bad_alloc` or `FATAL` marker appears in this bounded log. This establishes
startup past the former guest-library allocation failure, not general allocator
correctness or playable rendering.

The reviewed 60-second capture shows a white game surface, black letterboxing
and touch controls. There are exactly two distinct graphics rejection reasons,
each recorded for two color targets, for four skipped-draw records in total:

- `split draw currently requires exactly a vertex/fragment shader pair`;
- `multisample rasterization requires PA_SC_MODE_CNTL_0 MSAA_ENABLE`.

The first blocks internally generated VS/TCS/TES/PS rectangle stages. The second
requires a separate, correctly identified resolve/raster-state implementation;
removing the guard does not supply that behavior. The receipt's 72.52 seconds
measure the bounded test operation, not a proven gameplay duration.

The UI logs early detach and `Wine finished after 23.5s`. Another 2,686 log lines
follow, including VideoOut, audio-buffer and protected-write activity. That
helper/detach completion is not evidence of game exit or clean guest teardown.
The receipt records restoration of the runtime hashes and byte preservation of
configuration and library files. See the
[fresh lifecycle/startup record](evidence/ipad-solitaire-retained-export-lifecycle-20261008.json).

## Reviewed 0046/0047 game device checkpoint

The later Build47 game run uses a freshly prepared package with all 47 HLE
patches and the lifecycle-corrected relinker. Its read-back manifest matches the
receipt, all 47 explicit HLE dependencies resolve, and normal memory tracking
and the paired context repairs remain active. The Apple M2 device creates the
same 768 × 432 three-image swapchain without a logged `bad_alloc` or `FATAL`.

The previous pair-only and `MSAA_ENABLE` rejection messages are absent from
this bounded log. It contains one skipped-draw record and one rejection reason:
`SPIR-V refers to an unknown type`. The remaining lead is a conservative type
validator that omits the resolve fragment's direct `OpTypeImage` declaration;
Patch0048 implements the bounded inspector repair and clears that logged error
in the later bounded device run below. Absence of earlier
guards does not prove that either complete draw/resolve path has finished.

The separately reviewed 35-second capture remains white with touch controls.
Runtime activity continues after early-detach/helper completion; no guest exit
code is observed. The receipt records runtime restoration and byte preservation
of the library/configuration; the original and restored manifests also match
byte-for-byte. The actual-resource classification and full graphics host suites
now pass with explicit Vulkan adapters/mocks. These tests and the independent
resolve GPU probe do not establish playable game output. See the
[current game device record](evidence/ipad-solitaire-rectlist-resolve-20261008.json).

## Opaque descriptor inspector repair: patch0048

The shader inspector had not registered `OpTypeImage`, `OpTypeSampler` or
`OpTypeSampledImage`. A direct UniformConstant image pointer therefore requested
an unknown pointee before descriptor validation. Declaration order was not the
cause: the scan gathers all declarations before inspecting variables.

Patch0048 registers the opaque types with instruction-size checks and then
checks separate sampler, sampled image and storage image roles against actual
binding metadata. It admits a bare descriptor or one fixed array only when the
declared count matches. Image dimension, array/multisample shape, sampled versus
storage classification and numerical scalar types remain checked. Missing types,
nested/runtime arrays, metadata mismatches and combined image/sampler globals
remain rejected; recognizing their type does not add a combined runtime binding.

The production inspector passes ten accepted and 56 rejected synthetic cases
under ASan/UBSan. Six original fixtures and 18 factory-generated modules pass
independent SPIR-V validation. The previous inspector's exact unknown-type failure
is reproduced by the same resolve factory, and the corrected production inspector
accepts it. The 48-patch HLE package builds with 50 PRX implementations and three
runtime DLLs; all 22 selected host suites and resource/full graphics host suites
pass. The original inspector record makes no GPU/game claim. See
[opaque descriptor qualification](evidence/opaque-descriptor-shader-validation-20261008.json).

### Latest 0048 game device result

The closed Build47 app run uses a newly prepared package with all 48 HLE
patches and the lifecycle-corrected relinker. Its read-back manifest matches the
receipt and identifies the verified new HLE archive; all 47 explicit HLE
dependencies resolve. Built-in JIT, normal memory tracking and the paired
exception-context repairs remain active. The Apple M2 GPU creates the same
768 × 432 three-image swapchain.

Across 25,006 log lines there are zero `[gpu] skipped draw`, `AGC graphics:`,
unknown-type, `bad_alloc` or `FATAL` matches. The reviewed 60-second capture still
shows a white game surface, touch controls and black letterboxing. A log with no
rejection does not establish that GPU draws/resolves completed correctly or
that the presented image contains correct game pixels. The next investigation
must inspect the production rendered sample/resolve contents and their route
to the swapchain and display.

The early-detach/helper message `Wine finished after 23.3s` is followed by
8,362 log lines, including VideoOut and audio activity; no guest exit code is
observed. Original/restored configuration, library and manifest bytes match,
and the receipt records restored runtime hashes. No playable scene, correct
audio or frame-rate result is accepted. See the
[latest device and build record](evidence/ipad-solitaire-opaque-descriptor-20261008.json).

## Independent physical iPad component results

An independent x86-64 Vulkan program runs through the installed Wine/FEX stack
on the M2 iPad, with rendering through MoltenVK/Metal. Its capability queries
report programmable sample locations for 2 and 4 samples, a 1×1 location grid,
four fractional coordinate bits, a coordinate range of 0 through 15/16, and
variable positions. These bounds match the captured game pattern's precision.
All four pixels of its captured 2×2 quad use the same eight positions.

`tools/gpu-probe/msaa_split_probe.c` splits eight positions into two groups of
four. It renders the same synthetic triangle using each group's actual native
four-sample positions. Each pass resolves to a single-sample `R32_SFLOAT`
image, and the test reads both resolves back. An integer CPU edge test evaluates
coverage at every one of the eight positions; the fixture has no sample exactly
on an edge. A second shader mode outputs a gradient interpolated at the pixel
center, which must remain unchanged between the passes.

The same binary passes with both its own synthetic pattern and the locally
captured Solitaire pattern. Each run checks 512 combined values and 1,024
individual group values, with zero differences above 0.00001. There are 33 and
34 partially covered pixels respectively. Both runs exit Wine with code 0 and
restore their original configuration and library byte-for-byte. See the
[device evidence](evidence/ipad-two-pass-msaa-20261008.json).

The original mode reuses its native target after each readback and computes
the eight-sample average on the CPU. An additional `--array` mode now keeps
both four-sample groups in separate layers of one native multisample image.
The attachment store operation preserves each layer after rendering. A compute
shader reads logical sample `i` from layer `i / 4`, native sample `i % 4`, stores
each value separately, and computes the eight-sample mean on the GPU. Render-pass
dependencies order attachment writes and subsequent shader reads; a final barrier
and fence order readback before CPU inspection.

Both patterns pass 4,096 individual sample checks and 512 GPU-mean checks with
zero differences above 0.00001, in addition to the original coverage and group
checks. Each pattern produces eight distinct CPU coverage signatures over the
fixture, so a permutation of samples cannot pass just because two samples cover
the same pixels. The same binary also passes the original mode again. Original
configuration and library files are restored byte-for-byte and the app's own
process cleanup is verified. See the
[sample-preservation evidence](evidence/ipad-msaa-array-preservation-20261008.json).

This qualifies GPU-resident sample preservation and shader read ordering for
the synthetic triangle in the independent probe. It does **not** render game
shaders, exercise guest transfers or qualify game performance. The floating-point
mean does not qualify guest-format rounding or sRGB.

An additional build now uses the actual production `RenderTarget` class from
`ColorRenderTarget.cpp` to own that multisample image and its views. Logical
eight-sample color is represented by two layers of a native four-sample image,
two 2D attachment views and one sampled 2D-array view. Single-sample targets
retain their ordinary image/view representation. Allocation checks format,
sample-count, extent, layer and resource-size limits and unwinds partial failure.

Both position patterns pass on the physical iPad again with this production
owner: 4,096 individual sample checks, 512 GPU-mean checks and all coverage/group
checks per run, with zero differences and Wine exit 0. This establishes the
production resource representation on that device. The probe still supplies its
own render pass, pipeline and descriptors and uses `R32_SFLOAT`, rather than the
game's RGBA8 target. It does not execute the renderer's residency, guest
upload/readback, depth/stencil or actual draw path. Device initialization changes
that query and enable optional Vulkan features are now part of the rebuilt HLE
package, but this probe does not qualify its game device initialization. See the
[production resource evidence](evidence/ipad-production-color-sample-groups-20261008.json).

Two later probes exercise the production transfer implementations with 17×19
surfaces and logical sample counts 2, 4 and 8. Each count runs with separate
buffers and with an aliased input/output buffer. After upload, the alias case
overwrites the buffer before GPU readback, so preserved CPU input cannot create
a false pass. Prefix/suffix guards are checked in both probes.

- `ColorSampleTransfer` uploads and reads all 9,044 `RGBA8_UNORM` sample words
  exactly, with zero errors. The eight-sample case uses two four-sample layers;
  no resolve or averaging occurs. See the
  [exact color device record](evidence/ipad-exact-color-sample-transfer-20261008.json).
- `StencilSampleTransfer` uploads and reads all 9,044 S8 bytes exactly, covering
  all 256 byte values and partial final-word padding. An independent depth
  readback confirms all 9,044 existing D32 values remain finite and within
  0.000001 of their initial 0.625 in the combined D32/S8 image. This does not
  test bitwise identity or arbitrary depth values. See the
  [exact stencil device record](evidence/ipad-exact-stencil-sample-transfer-20261008.json).

Both guests exit Wine with code 0, their own processes are subsequently cleaned
up, and their original configuration and library are restored byte-for-byte.
A native JIT-detach breakpoint is logged after guest completion; these results
do not prove a clean native app lifecycle. The probe manifests identify the
as-built source hashes. Neither probe executes the connected guest renderer,
game shaders or a swapchain, and neither measures production-size performance.

### Fixed-function color resolve: isolated GPU result

The later Build47 probe uses the production RGBA8 resolve fragment factory,
canonical rectangle TCS/TES and color sample upload in an independent Vulkan
pipeline. It executes full and partial synthetic rectangles for logical sample
counts 2, 4 and 8 on 17 × 19 targets. The eight-sample source retains two native
four-sample layers. The destination uses LOAD/STORE, so pixels outside the tested
rectangle and scissor retain their original values.

All six cases pass 7,752 channel comparisons, including 2,580 unchanged
destination channels. Four cases additionally compare 5,168 channels against
`vkCmdResolveImage` at 2/4 samples; the eight-sample result uses an independent
integer average. The mixed fixture includes 2,281 half-rounding ties, 2,891
non-tie means and, within those non-ties, 1,779 exact integer means. Half ties
allow a one-unit difference in RGBA8 UNORM; non-ties and untouched pixels must
match exactly. Zero errors means every value meets that contract, not bit-exact
AMD hardware tie rounding.

The log explicitly records Windows/Wine guest exit code 0 before a native
StikDebug-protocol breakpoint at `jit26_detach`. Clean native app lifecycle
therefore remains unqualified. The device-read-back executable matches its
as-built manifest; configuration and library are byte-restored. The independent
probe does not execute complete AGC decoding, production `Driver`/`ColorResolve`,
guest write-watch, game shaders or a swapchain. See the
[resolve device/build record](evidence/ipad-fixed-color-resolve-20261008.json).

The capability query additionally returns support for sample-rate interpolation
functions, although Wine does not enumerate `VK_KHR_portability_subset`. That
queried value alone is insufficient to enable such an extension or qualify
`InterpolateAtOffset`. The successful rendering test uses center interpolation
and `VK_EXT_sample_locations`; it does not use those extended interpolation
instructions. The distinction follows the
[Vulkan feature definition](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDevicePortabilitySubsetFeaturesKHR.html).

## Guest color/sample memory layout

AnyPS5 patch0034 extends the production `ColorTargetLayout` with a stored-sample
coordinate and 2-, 4- and 8-sample SW_64KB_R_X backing sizes. A linear transfer
buffer contains every sample of each pixel in order; tiling and detiling retain
the individual bytes and leave padding intact. Only uncompressed 2D color with
samples equal to fragments is covered. Other multisample tile modes, EQAA,
FMASK/DCC, array slices, mip tails and depth/stencil are outside this work.

The address equations are generated from the original AMD AddrLib in
[PAL c5e80007](https://github.com/GPUOpen-Drivers/pal/tree/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/imported/addrlib),
using the same non-RB+ Navi1x, 16-pipe, 256-byte interleave model as the existing
single-sample equations. A separately linked AddrLib computes independent sizes
and offsets: 80 layouts, 2,650,990 coordinate/sample comparisons and 40 complete
sample-buffer round trips pass with ASan/UBSan. The matrix covers element sizes
1/2/4/8/16 bytes, counts 1/2/4/8, odd extents, 4K and the maximum 16384-pixel
extent. No large surface is allocated for the maximum-size address checks.
This validates that AMD reference model; captured guest resource layouts and
their use in the connected renderer still require independent qualification.

The comparison also found an existing single-sample bug: for 8- and 16-byte
color elements, discarding coordinate bits at a macroblock boundary discarded
pipe XOR. For example, the 8-byte, 257×129 surface at (0,64) returned 196608
instead of the reference's 198656. The layout now uses the equation's coordinate
period, keeping those bits. Cached tables cover that small period rather than
the complete surface. The ordinary 32-bit scanout equations remain the same.

Run the production-code/reference comparison after separately building that
AddrLib checkpoint:

```sh
python3 scripts/check-msaa-color-layout.py \
  --addrlib-source /path/to/pal/src/core/imported/addrlib \
  --addrlib-library /path/to/build/libaddrlib.a
```

The script downloads nothing and uses no game data. The reference library's
build is separate; the tested production code and harness run under sanitizers.
Default draw validation still rejects multisampling. The experimental draw
path below adds its own format, shader, depth, metadata and synchronization
guards; layout tests alone do not justify enabling an arbitrary draw.

Patch0035 connects stored color fragment counts to `DecodeColorBuffer` and the
production CPU tile/detile transfer functions. The focused regression checks
994,590 distinguishable samples across slots 0 and 7 and sample counts 1/2/4/8,
including padding preservation, short buffers and unsupported metadata. It passes
ASan/UBSan using an explicit native mapped-memory adapter, and the same test
passes as a Windows PE under desktop Wine with the real `GuestMemory.cpp`.
The latter uses the recorded earlier libc/kernel build; it does not qualify
the iPad's guest-memory tracking. Two negative controls fail the previous
transfer implementation. EQAA, FMASK compression, multisample DCC/CMASK and
multisample mip/array transfers remain rejected. See the
[color transfer record](evidence/solitaire-msaa-color-state-transfer-20261008.json).

## Depth/stencil and safe repeated draws

Patch0036 adds raw D16, D32 and separate S8 sample layouts for SW_64KB_Z_X.
They require equal sample/fragment counts, canonical pitch, one mip/slice,
zero pipe/bank XOR and no HTILE interpretation. The independently linked AMD
reference passes 60 layouts, 34,817,526 address/sample comparisons and 24
complete round trips under ASan/UBSan. This includes every sample of 1920×1080
eight-sample D32 and S8 surfaces. It proves the selected AMD reference model.
Z-consuming guest draws are still unsupported. Patch0043 now connects the S8
layout to guest transfers; the raw depth layouts alone do not qualify guest Z
import or export. See the
[depth layout record](evidence/solitaire-msaa-depth-layout-20261008.json).

Patch0039 extends the production depth resource owner with separate four-sample
layer views for eight-sample targets, full-layer initial depth/stencil clearing,
sample-count/device cache identity and failure rollback. Multisample depth
requires enabled programmable sample locations and the compatible-depth image
flag. ASan/UBSan tests capture the production commands with explicit Vulkan and
scheduling mocks: four configurations and 21 failure/rejection cases pass.
This allocation test uses mocks. Patch0040 adds matching attachment/subpass
sample locations, while patches0043/0044 add S8 import/readback. The independent
S8 device probe above proves stencil transfer with an unchanged synthetic Z
plane, rather than game depth use. Sampled multisample depth descriptors and Z
import/export remain unsupported. See the
[depth allocation record](evidence/solitaire-msaa-depth-surfaces-20261008.json).

The first captured rejected draw writes all four RGBA components and clears
stencil through replacement operations over its geometry. A stencil-only or
unconditional full-target clear would change its behavior. Its shaders use
center interpolation, but repeating arbitrary shaders can duplicate writes,
atomics or other observable operations.

Patch0038 adds a conservative static prerequisite for repeating a VS/PS pair.
It refuses external writes, atomics, clocks, barriers, interlocks, subgroup
operations, sample built-ins and extended interpolation, and fails closed on
unsupported or unresolved operations. Thirty-one sanitizer cases pass,
including 24 independently compiled and validated synthetic shaders and seven
malformed modules. The captured first pair passes ordinary Vulkan SPIR-V
validation and this static check; that does not establish dynamic attachment
feedback safety, query counting or correct full draws. Patch0043 now requires
this check before repeating a grouped draw and additionally rejects dynamic
attachment/input aliases and overlapping output attachments. See the earlier
[integration checkpoint](evidence/solitaire-msaa-renderer-integration-20261008.json).

In the earlier Build47 checkpoint, the private Solitaire CPU candidate runs for 60 seconds with
normal memory tracking, multiblock enabled and MAXINST5000. Its three captures
remain white and both logged render targets reject eight-sample draws. The
closed log contains no prior terminal small-address graphics-worker exception;
ongoing protected writes are still handled. This is bounded startup evidence,
not interactive gameplay, audible correctness, a clean guest exit or FPS proof.
Actual pre-test runtime hashes, configuration and library are restored.
See [source and device evidence](evidence/solitaire-msaa-color-layout-20261008.json).

## Connected experimental renderer path

Patch0040 decodes logical/native sample counts, masks and guest sample positions
into pipeline state. Eight logical samples become two native four-sample
groups. Each group has its own pipeline/cache identity, attachment views and
sample-location state. Counts must agree across raster state and attachments;
the four captured 2×2 position banks must be identical, since the tested device
supports a 1×1 location grid. The initial 41 sanitizer state/pipeline cases now
pass with five additional portability-feature rejection cases, for 46 total.

Patch0041 supplies exact `RGBA8_UNORM` import/export. Upload uses fullscreen
fragment draws, a storage-buffer source and a native sample mask selecting each
sample. Readback uses a sampled multisample array and writes packed RGBA8 words
to a storage buffer. Immutable descriptor pools survive through GPU completion.
Three layout contracts and 89 rejection/unwind cases pass with ASan/UBSan;
the separate device probe qualifies the transfer itself.

Patch0042 decodes multisample guest texture descriptors and carries logical and
native counts through shader specialization and disk caches. Shader image loads
map logical sample `i` to array layer `i / nativeSamples` and native sample
`i % nativeSamples`; a logical array layer adds its group offset. Size queries
convert host group layers back to guest layers. Twenty-eight production SPIR-V
fixtures pass independent validation, and evaluation of their emitted integer
arithmetic checks 84 distinct sample addresses. Actual host texture support is
limited to one logical layer, even though the shader routing tests cover array
arithmetic. Depth textures, compressed color and multisample storage operations
are rejected.

Patch0043 connects those components in `Draw`, `ShaderResources` and `Texture`.
The first implementation synchronizes existing recorded work, creates fresh
sample snapshots, uploads every color and S8 sample, executes one draw per
group, reads all samples back, and waits for completion before writing changed
bytes to guest memory. CPU tiling preserves guest padding. It rejects shader
writes/atomics, attachment/input feedback and overlapping output attachments;
ordinary vertex/fragment pipelines and matching RGBA8 attachments are required.
Recorded draw recipes and the existing single-sample storage-texture reuse path
are bypassed for these draws. This prioritizes a complete lifetime and memory
contract over speed: repeated CPU transfers and a fence per draw are not a
performance optimization.

The same patch keeps supported portability-subset features enabled when querying
multisample arrays. It checks unsupported view swizzles, constant-alpha color
blend factors, triangle fans and separate stencil masks/references rather than
silently disabling features already used by ordinary rendering.

Patch0044 supplies exact S8 transfer without a shader-stencil-export extension.
It clears only stencil, then reconstructs each byte with eight bitplane draws
per logical sample, fragment discard, a native sample mask and one-bit stencil
write masks. Depth test/write are disabled and depth attachment contents use
LOAD/STORE. Compute readback fetches the stencil aspect and packs disjoint output
words, retaining unused bytes of a partial final word. Image barriers cover both
depth/stencil aspects with the matching per-layer sample locations. Six layout
contracts and 234 rejection/unwind cases pass with ASan/UBSan; the separate
device probe qualifies S8 preservation and unchanged synthetic D32 contents.

Patch0046 extends the ordinary pair prerequisite to rectangles generated by
`BuildRectListShaders`. It checks the original guest VS/PS with the unchanged
static guard, regenerates the internal TCS/TES using the actual device target,
and requires exact SPIR-V, all 20 descriptor-binding fields and all 19 result
metadata fields. Arbitrary guest tessellation and altered diagnostics remain
rejected. The generated stages validate finite axis-aligned geometry from the
actual vertex output; this does not prove fullscreen coverage.

The rectangle factory's host FaultBuffer requires completion and fault readback,
but is not a guest-memory write. `WritesGuestMemory` separates that diagnostic
from guest written ranges, copied writes, storage writes and unknown address
leases. It is used only after the canonical rectangle proof. Ordinary paths
retain `WritesMemory`; fences, barriers and mandatory `CheckFault` still execute
before attachment writeback. Three canonical accepts and 90 rejection cases
pass ASan/UBSan, with 24 independently validated SPIR-V modules; all 31 existing
pair-guard cases still pass. Production syntax checks pass. The actual-resource
test `agc_driver_graphics_tests.exe --resource-write-classification-only` and
the full graphics host suite subsequently pass under desktop Wine. Connected
game rectangle rendering remains unqualified. See the
[original static qualification](evidence/grouped-msaa-canonical-rectlist-20261008.json)
and [later host/device checkpoint](evidence/ipad-solitaire-rectlist-resolve-20261008.json).

Patch0047 decodes the restricted GC10 `CB_RESOLVE` operation before ordinary
`MSAA_ENABLE` raster rejection. Matching uncompressed RGBA8 views with 2/4/8
stored samples are averaged from source MRT0 into single-sample destination
MRT1. The path retains the actual guest VS and canonical rectangle stages,
with normal destination LOAD/STORE and scissor coverage; no fullscreen geometry
is substituted. A source snapshot precedes destination writes, including
overlapping storage. Unknown formats, EQAA, compression, unsupported depth/stencil
work and unsupported draw geometry remain rejected.

Its native sanitized checks cover 144 cases and 401,712 scalar channel values,
including rounding boundaries, production tiled transfers, overlapping storage
and rejection controls. Production factory SPIR-V validates independently.
The isolated iPad GPU result above qualifies its resolve fragment and rectangle
stages, rather than complete guest-command execution. The newly built HLE
includes this source, but connected game rendering remains unqualified. See
[native implementation checks](evidence/gc10-fixed-color-resolve-native-20261008.json)
and [device probe/current HLE build](evidence/ipad-fixed-color-resolve-20261008.json).

The opt-in path still admits only the already checked always-pass, full-mask
stencil initializer when a multisample depth/stencil attachment is present.
Fresh S8 import does not establish arbitrary guest depth/stencil use. Z tests,
Z writes, consumed HTILE, compressed stencil, sampled depth, EQAA, partial sample
masks, nonuniform sample banks, multisample mip/array color targets and
shader-side sample/interpolation effects remain blocked. Unused Z metadata can
remain bound for the captured stencil initializer because that draw does not
observe Z. No unsupported draw is accepted merely because a transfer probe passes.

## Remaining qualification

At the as-built 0044 checkpoint, the connected source builds a full 50-PRX
Windows HLE package with WinLibs GCC 15.2.0 UCRT posix-SEH. All 22 selected Windows host test suites pass under
desktop Wine, including exceptions, guest memory, compatibility APIs and audio
contracts. The first memory-suite attempt refused a reservation at 8 GiB because
of a host mapping collision. Repeating with the tested 464–468 GiB lazy arena
passes with every assertion retained. These suites contain no GPU graphics
tests. A fresh private Solitaire package is prepared with all 47 explicit HLE
dependencies resolved; that static audit is separate from execution. Build
provenance hashes match the patch set. See the
[source and full-build record](evidence/solitaire-msaa-draw-source-20261008.json).

The as-built 0044 checkpoint remains unchanged. A subsequent freshly relinked
0045 package has now passed bounded startup on the iPad with normal memory
tracking and corrected exception state. Its swapchain works, while the reviewed
game surface stays white and the two graphics guards above reject draws.
Patches0046/0047 are not part of that earlier device run. Their independent
resolve GPU probe and host graphics suites subsequently pass; their fresh
game run remains white with the unknown-type rejection described above.
Complete connected game qualification remains pending. The next rejected
state or shader is evidence for the next implementation step, not a reason to
remove a correctness guard.

The earlier 47-patch series applies forward, matches 137 checked source paths
and reverses to the pin. Its rebuilt archive contains 50 PRX implementations
and three Windows runtime DLLs; all 53 embedded file hashes verify and its
22 selected host test suites pass under desktop Wine. The separate resource
classification and full graphics suites also pass. A fresh game package has
run on the iPad, but its white surface and type-validation rejection leave
rendering unresolved. These build, probe and later device facts do not alter
the earlier as-built 0044 checkpoint. See the
[47-patch build record](evidence/ipad-fixed-color-resolve-20261008.json).

The newer 48-patch checkpoint also applies/reverses across 137 paths. All 53
files of its 50-PRX/three-DLL archive match their embedded hashes, and all 22
selected host suites plus resource/full graphics suites pass. Its latest game
run clears the previous logged rejections but remains white, as recorded above.
That source/build/run evidence is separate from correct game image or sustained
performance qualification.

Complete guest draw/resolve routing, resource-cache handoffs, alias coherency,
occlusion/sample counters and production-size transfer behavior remain
unqualified. Sampled shader fixtures have not executed on the iPad GPU through
the connected renderer. Game menu, gameplay, touch, audio, saves, background
recovery and sustained frame rate require separate device acceptance.

The proposed split preserves eight coverage positions; it must not silently
replace them with four samples or enable a draw whose other semantics remain
unsupported. The native sample-position API and its bounds are documented in
[the pipeline state](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineSampleLocationsStateCreateInfoEXT.html)
and [physical-device properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceSampleLocationsPropertiesEXT.html).

## Reproduce the synthetic experiment

```sh
scripts/build-msaa-split-probe.sh windows
cd build/msaa-split-probe/windows
./probe.exe
./probe.exe --array
```

Run that PE on a Vulkan-capable Windows reference, or import the generated
directory into the iPad library and launch `probe.exe` through Madeira. The
program expects its shader files in its working directory. An optional argument
may name a local 16-byte position file: eight distinct `(x,y)` byte pairs in
units of 1/16, each coordinate between 0 and 15. The captured game pattern stays
private; it is not shipped in this repository.

Use `probe.exe --array positions.bin` to check that pattern with persistent
samples and GPU resolve. `--array` requires a graphics/compute queue and a
sampled native four-sample array image. When the portability-subset extension
is enumerated, its `multisampleArrayImage` feature must be supported and is
explicitly enabled at device creation. Wine on the tested iPad filters the
extension name; the device execution proves this resource path independently
of the queried feature bit.

To build the same fixture with the actual production color image owner:

```sh
APS5_MSAA_PRODUCTION_TARGET=1 \
  APS5_MSAA_PROBE_BUILD="$PWD/build/msaa-production-color/windows" \
  scripts/build-msaa-split-probe.sh windows
```

Use `--array` when running this version. The image owner is production code;
the synthetic shaders and render passes remain the probe's own code. Native
contract checks run separately:

```sh
python3 scripts/check-msaa-color-state.py      # macOS mapped-memory adapter
python3 scripts/check-msaa-render-target.py    # explicit Vulkan adapter
python3 scripts/check-msaa-depth-surfaces.py   # command/scheduling mocks
python3 scripts/check-split-draw-shaders.py    # glslangValidator + spirv-val
python3 scripts/check-msaa-rectlist-shaders.py # canonical rectangle guard, no GPU work
python3 scripts/check-fixed-color-resolve.py  # resolve state/factory/scalar contracts
python3 scripts/check-opaque-descriptor-shaders.py # actual inspector and descriptor metadata
python3 scripts/check-msaa-state-pipelines.py  # sample state and pipelines
python3 scripts/check-color-sample-transfer.py # production color transfer contracts
python3 scripts/check-stencil-sample-transfer.py # production stencil contracts
python3 scripts/check-msaa-texture-routing.py  # recompiler SPIR-V and sample routing
python3 scripts/check-msaa-depth-layout.py \
  --addrlib-source /path/to/pal/src/core/imported/addrlib \
  --addrlib-library /path/to/build/libaddrlib.a
```

These scripts download nothing. The compiler, shader tools and independently
built reference library must already be available. At the as-built 0044
checkpoint, all 44 AnyPS5 patches apply from the pin, match the checked source
bytes across 125 paths and reverse back to the pin. The full-build record
contains those hashes; patches0045/0046 have their own later qualification records.

The separate earlier 47-patch checkpoint checks 137 paths and the rebuilt HLE
archive. Its record is linked above; source-series verification does not qualify
connected device rendering.

Build the independent exact transfer probes separately:

```sh
scripts/build-color-sample-transfer-probe.sh windows
scripts/build-stencil-sample-transfer-probe.sh windows
scripts/build-fixed-color-resolve-probe.sh windows
```

Run each generated `probe.exe` in its own directory on a Vulkan-capable Windows
reference or through Madeira on the iPad. These offscreen probes include their
production transfer shaders and original synthetic data; they use no game assets.

The macOS build requires `MOLTENVK_LIB` pointing to a pinned built dylib and
`scripts/build-msaa-split-probe.sh macos`. A macOS result does not qualify iPad
execution. Exit 77 denotes an unavailable prerequisite; a successful build or
capability query does not replace the checked pixel readback.
