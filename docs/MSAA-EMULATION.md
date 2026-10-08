# Eight-sample rendering: tested building block, integration still pending

Solitaire's captured state requests eight raster, exposed and color-fragment
samples. The M2 iPad exposes native attachment sample counts of 1, 2 and 4.
The current AnyPS5 decoder rejects multisampling, and its render targets,
pipeline attachments and transfer layouts are single-sample. Increasing the
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

This proves a component of the proposed renderer. The test reuses its native
target after each readback and computes the eight-sample average on the CPU.
It does **not** preserve eight individual sample values for subsequent guest
reads, integrate AnyPS5, render game shaders or qualify game performance. Its
floating-point resolve also does not qualify guest-format rounding or sRGB.

The capability query additionally returns support for sample-rate interpolation
functions, although Wine does not enumerate `VK_KHR_portability_subset`. That
queried value alone is insufficient to enable such an extension or qualify
`InterpolateAtOffset`. The successful rendering test uses center interpolation
and `VK_EXT_sample_locations`; it does not use those extended interpolation
instructions. The distinction follows the
[Vulkan feature definition](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDevicePortabilitySubsetFeaturesKHR.html).

## Required renderer integration

The following work remains before this can replace a rejected game draw:

1. Keep both native four-sample targets resident. Prove shader reads of all
   eight individual samples, including their ordering, before implementing
   guest resolve operations. A native multisample array or two distinct images
   must be tested, rather than inferred from feature bits.
2. Decode sample and fragment counts consistently in `State`, `ColorTarget`
   and `GuestTextureResource`. Extend color/depth byte sizes, swizzle equations,
   import/export and alias tracking to include the sample coordinate. Validate
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
```

Run that PE on a Vulkan-capable Windows reference, or import the generated
directory into the iPad library and launch `probe.exe` through Madeira. The
program expects its shader files in its working directory. A second argument
may name a local 16-byte position file: eight distinct `(x,y)` byte pairs in
units of 1/16, each coordinate between 0 and 15. The captured game pattern stays
private; it is not shipped in this repository.

The macOS build requires `MOLTENVK_LIB` pointing to a pinned built dylib and
`scripts/build-msaa-split-probe.sh macos`. A macOS result does not qualify iPad
execution. Exit 77 denotes an unavailable prerequisite; a successful build or
capability query does not replace the checked pixel readback.
