# AnyPS5 runtime patches

The active files in this directory are applied in lexical order to the pinned
AnyPS5 submodule. The full-main integration uses:

1. `0001-ipad-runtime-compatibility.patch`: reconciled iPad/Wine compatibility,
   title-facing runtime repairs, guarded graphics paths and regression tests.
2. `0002-bounded-exception-logging.patch`: direct stderr exception output in
   bounded chunks, preserving complete shader requests up to one MiB and marking
   larger messages explicitly. Includes short-write, failure, allocation and
   guard-page tests. Guest exception ABI and custom terminate handlers are unchanged.
3. `0003-valid-image-descriptor-policy.patch`: use native null image descriptors
   only when enabled by the actual device. Otherwise require valid guest image
   addresses and Vulkan image views before initial or replayed descriptor writes.
   Request version 15 and cache keys preserve the policy; versions 1–14 default
   conservatively to valid images. This does not emulate null images.
4. `0004-no-vintrp-exec-aware-input-liveness.patch`: for shaders without VINTRP,
   prove that declared barycentric inputs are overwritten before every use.
   The bounded must-analysis models admitted register widths, EXEC changes,
   saved masks, predecessors and loops in the original guest CFG. Unknown
   effects fail closed. Scalar-buffer x4/x8, full packed conversions, plain MAD
   and 2D/3D implicit-LOD samples retain read-before-write and saved-mask guards.
   Plain scalar-buffer x2, array/MSAA image loads, admitted lane arithmetic and
   MRT0–7 color exports model exact source/destination widths and metadata.
   WQM requires a single straight-line block, whole-quad definitions and
   same-epoch helper coordinates. Exact internal MinGW export exclusions preserve
   the existing DLL interface. Existing P1/P2 validation remains unchanged.
5. `0062-bounded-legacy-audioout-ingress-trace.patch`: unchanged, default-off
   bounded input/enqueue diagnostics.
6. `0065-audioout-frame-pacing.patch`: unchanged, opt-in legacy AudioOut cadence.

The full upstream `main` snapshot is `d70b89989473ba1f6ae13e44e079e67f1f8a44b0`.
Its new prepared shaders, typed descriptor heaps, runtime ABI checks and complete
instruction inventory are retained. The local patch changes are not a rollback
to the old shader architecture.

`legacy-6e037e98/` preserves the original 75-patch series and its hashes for
historical evidence. Build/apply scripts use `*.patch` directly in this directory
and never recurse into that archive. Do not apply the historical stack to the
new pin.

[Integration and qualification](../../docs/ANYPS5-MAIN-INTEGRATION.md) explains
source coverage, retained runtime behavior and the separate device acceptance.
The expanded source passes 302 public cases through an isolated actual PRX and
seven complete MiniGolf requests. All six retained SPIR-V/layout pairs remain
byte-identical. Its fresh full-runtime rebuild and physical GPU acceptance
remain pending; the preceding quad-mask snapshot has separate full-build
evidence. See [MiniGolf](../../docs/MINIGOLF.md).
