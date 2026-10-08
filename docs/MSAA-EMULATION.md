# Eight-sample rendering: resource tests pass, game integration still pending

Solitaire's captured state requests eight raster, exposed and color-fragment
samples. The M2 iPad exposes native attachment sample counts of 1, 2 and 4.
The current draw path rejects multisampling. Color metadata decoding and CPU
transfers now preserve individual samples; the production color image owner can
allocate grouped native attachments. Depth/stencil layouts, grouped allocation
and a shader eligibility check are also implemented, with different levels of
qualification below. Pipelines, sampled guest descriptors and renderer cache
handoffs still need to connect these components. Increasing the pipeline sample
count alone cannot repair this path.

## Physical iPad result

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
that query and enable optional Vulkan features are syntax-checked source;
they are not yet part of a rebuilt game HLE package. See the
[production resource evidence](evidence/ipad-production-color-sample-groups-20261008.json).

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
GPU transfers still require independent qualification.

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
Current draw validation still rejects multisampling. The layout tests do not
justify enabling a draw without its remaining render-target, shader, depth,
metadata and synchronization semantics.

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
eight-sample D32 and S8 surfaces. It proves the selected AMD reference model;
the guest depth decoder and GPU transfers are not connected to it yet. See the
[depth layout record](evidence/solitaire-msaa-depth-layout-20261008.json).

Patch0039 extends the production depth resource owner with separate four-sample
layer views for eight-sample targets, full-layer initial depth/stencil clearing,
sample-count/device cache identity and failure rollback. Multisample depth
requires enabled programmable sample locations and the compatible-depth image
flag. ASan/UBSan tests capture the production commands with explicit Vulkan and
scheduling mocks: four configurations and 21 failure/rejection cases pass.
This is not a device depth/stencil test. Matching attachment/subpass sample
locations and transitions, sampled multisample depth descriptors and guest
upload/readback remain pending. See the
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
feedback safety, query counting or correct full draws. The check is not yet
enabled in the draw path. See the
[integration checkpoint](evidence/solitaire-msaa-renderer-integration-20261008.json).

On Build47, the same private Solitaire CPU candidate runs for 60 seconds with
normal memory tracking, multiblock enabled and MAXINST5000. Its three captures
remain white and both logged render targets reject eight-sample draws. The
closed log contains no prior terminal small-address graphics-worker exception;
ongoing protected writes are still handled. This is bounded startup evidence,
not interactive gameplay, audible correctness, a clean guest exit or FPS proof.
Actual pre-test runtime hashes, configuration and library are restored.
See [source and device evidence](evidence/solitaire-msaa-color-layout-20261008.json).

## Required renderer integration

The following work remains before this can replace a rejected game draw:

1. Connect the tested production two-layer color image and logical sample
   routing to render-target residency and shader image loads. Individual
   sample preservation passes the production-owner device probe, but guest
   renderer and guest resolve operations remain unqualified.
2. Extend sample/fragment decoding to depth and `GuestTextureResource`, connect
   the tested depth byte layouts and CPU color transfers to GPU import/export,
   and preserve cache/alias tracking. The independent AMD layout comparisons
   already pass; actual captured resource layouts require a separate check.
3. Apply the guest positions and sample masks to each subset. Preserve
   depth/stencil, blending, discard and center interpolation independently for
   each sample. Captured stencil testing makes color-only success insufficient.
4. Apply the static shader eligibility prerequisite and prove dynamic alias
   feedback freedom before repeating draws. Storage writes/atomics cannot be
   repeated arbitrarily. Occlusion/sample counters must retain guest counts.
5. Preserve full frame and cache lifetime: GPU work must finish before a sample
   target is reused, dirty pages and aliases must agree, and single-sample
   titles must keep their existing path.
6. Run the real relinked game with normal memory tracking and corrected
   exception state. Verify its menu, gameplay, touch, audio and saves, then
   measure performance. The current installed game still shows white output.

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
python3 scripts/check-msaa-depth-layout.py \
  --addrlib-source /path/to/pal/src/core/imported/addrlib \
  --addrlib-library /path/to/build/libaddrlib.a
```

These scripts download nothing. The compiler, shader tools and independently
built reference library must already be available. All 39 AnyPS5 patches apply
from the pin, match the checked source bytes and reverse back to the pin.

The macOS build requires `MOLTENVK_LIB` pointing to a pinned built dylib and
`scripts/build-msaa-split-probe.sh macos`. A macOS result does not qualify iPad
execution. Exit 77 denotes an unavailable prerequisite; a successful build or
capability query does not replace the checked pixel readback.
