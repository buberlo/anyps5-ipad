# Eight-sample rendering: tested building block, integration still pending

Solitaire's captured state requests eight raster, exposed and color-fragment
samples. The M2 iPad exposes native attachment sample counts of 1, 2 and 4.
The current AnyPS5 decoder rejects multisampling, and its render targets,
pipeline attachments and descriptor/transfer routing are single-sample. The
CPU color layout now supports separate stored samples; it is not yet connected
to those renderer paths. Increasing the
pipeline sample count alone cannot repair this path.

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
the synthetic triangle. It does **not** integrate AnyPS5, render game shaders,
implement guest layouts or qualify game performance. The floating-point
mean does not qualify guest-format rounding or sRGB.

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
This validates that AMD reference model; captured guest resources and actual
renderer transfers still require independent qualification.

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

1. Integrate the tested two-layer native four-sample image and logical sample
   routing into render-target residency and shader image loads. Individual
   sample preservation has passed the independent device probe; it has not
   passed the guest renderer or guest resolve operations.
2. Decode sample and fragment counts consistently in `State`, `ColorTarget`
   and `GuestTextureResource`. The standalone production color layout is checked
   against AddrLib; connect it to metadata and transfer routing, add depth byte
   sizes/swizzles, and extend import/export and alias tracking. Validate
   against an independent AMD address-layout reference.
3. Apply the guest positions and sample masks to each subset. Preserve
   depth/stencil, blending, discard and center interpolation independently for
   each sample. Captured stencil testing makes color-only success insufficient.
4. Handle shader side effects explicitly. Repeating vertex or fragment storage
   writes/atomics twice can change game behavior. Prove eligible shaders have
   no such effects or execute those effects once through an appropriate path.
   Occlusion/sample counters must retain the guest's counts.
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

The macOS build requires `MOLTENVK_LIB` pointing to a pinned built dylib and
`scripts/build-msaa-split-probe.sh macos`. A macOS result does not qualify iPad
execution. Exit 77 denotes an unavailable prerequisite; a successful build or
capability query does not replace the checked pixel readback.
