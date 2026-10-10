# Full AnyPS5 main integration

The integration adopts the complete AnyPS5 main branch at
[`d70b89989473ba1f6ae13e44e079e67f1f8a44b0`](https://github.com/boykopovar/AnyPS5/commit/d70b89989473ba1f6ae13e44e079e67f1f8a44b0),
434 commits after the previous `6e037e98` pin. It includes all upstream changes
through that commit, rather than only the shader-coverage entries or selected
Apple fixes. The existing Windows PE → Wine/FEX → Vulkan → MoltenVK → Metal
route remains the app's execution path. No GitHub Actions are used.

The upstream progress tool reports **1,166 of 1,166 shader instructions** for
this source, compared with 1,131 at the old patched pin. This measures the
project's instruction implementation inventory. It does not prove every shader,
GPU feature or game works correctly on an iPad. The ISA inventory and counting
rules are unchanged; the count is not increased by changing the denominator.

## Compatibility changes retained

The previous 75 local patches have been reconciled with the full upstream tree.
The qualified integration starts with a consolidated iPad/runtime patch and the
two unchanged AudioOut diagnostic/cadence patches. A subsequent patch0002 adds
bounded, complete exception output; its separate MiniGolf Windows comparison is
documented below. Patch0003 subsequently permits strictly valid image resources
on devices without native null-image descriptors. Patch0004 adds a bounded
EXEC-aware proof for unused barycentric inputs. The recorded six-patch baseline
qualifies their earlier versions together with the two AudioOut patches; the
expanded patch0004 now needs its own fresh complete rebuild. The old series
is preserved in
`patches/anyps5/legacy-6e037e98/` for historical receipts and code review; it is
not applied to the new pin.

- The new prepared-shader pipeline, typed runtime descriptor heaps, RuntimeAbi
  checks, pool ownership, recorder fences and snapshot storage remain intact.
  Storage-buffer update-after-bind still depends on actual features and limits,
  including compatibility with robust buffer access.
- Existing grouped MSAA behavior is carried through preparation, runtime mode
  selection, descriptor construction and SPIR-V emission. Logical sample counts
  remain distinct from native group counts. The switch remains opt-in; existing
  depth/stencil/dirty-tracking validation is retained.
- The initial qualified build serializes shader requests with version 14; the
  subsequent descriptor-policy repair uses version 15. The local disk cache uses
  format 21. Older request versions retain their conservative decoding path. Cache entries
  preserve the prepared MSAA policy, logical/native sample metadata and typed
  mode table. Old cache files are invalidated rather than silently reused.
- The upstream native/macOS exception work and nested exception waiters are
  retained alongside the Wine/FEX AVX/flags bridge and wait-exit reservation
  repair. No exception-delivery or memory guard is disabled to make the merge
  compile.
- Lazy guest-arena allocation, rollback, mapping ownership and the upstream
  private-mapping commit observer coexist. The 32-MiB native save-memory support,
  JSON virtual allocator dispatch and honest unavailable-service errors remain.
- Upstream's removal of guest-constructor suppression is adopted. All guest
  init/fini entries survive relinking. Explicit deferred start/stop wrappers for
  Unity modules retain argument forwarding and failure handling.
- Frame timing follows upstream's new asynchronous capture structure. The
  disabled path remains cheap, and runtime opt-in remains usable with timing
  logging disabled at compile time. The three-image swapchain, FPS display,
  fixed-function interpolation fallback and existing controls remain available.

Upstream macOS and exception infrastructure is retained. This integration does
not add a native ARM64 runner or ahead-of-time ARM64 translation to the iPad app.
Half-float interpolation uses the new upstream semantics; the existing
restricted float fallback does not blanket-replace those instructions.

## Historical six-patch baseline qualification, 2026-10-10

The combined Windows Release runtime builds **47 real PE PRX libraries** and
passes **42 selected plus ten additional test executables**, yielding **53
unique cases** under local Wine. The extra case is the explicit SaveData
memory-growth read-failure control. There are zero failures or CTest skips;
`guest_raise_exception` still has one internal asynchronous-AVX skip because
this Mac Wine host does not expose AVX. Its 451 delivered exceptions are tested.
The earlier native Windows AVX reference does not qualify this new binary set.

The fresh build compiles 630 project objects without copying older project
objects. All 1,771 source files and executable modes match the frozen checkout.
Every test binary and dependency is hashed before and after execution. A linker
registration error and two conservative shader-proof rejections are retained
with their failed outputs; their narrowly scoped fixes are followed by the
complete passing suite in the same fresh tree. No failed attempt is relabeled
as successful.

The final distribution contains 47 PRXs and three compiler DLLs. Export sealing
verifies all 50 payload members, preserves the immutable qualification receipt,
and checks that embedded provenance equals the final outer provenance without
its archive hash. Its acyclic sequence is:

```text
immutable pre-final build snapshots → qualification
  → final provenance containing qualification hash → archive
  → separate sealing receipt
```

The original three-patch foundation remains in its historical manifest below.
The [current combined manifest](evidence/anyps5-combined-runtime-20261010.json)
records all six patch hashes, library/test identities, source snapshot, actual
case results and final qualification/provenance/archive/sealing digests. It also
imports the current 45-public-plus-one-private staged compiler proof and the
independent semantic model; those are CPU checks, not physical shader execution.

A new private MiniGolf package uses this exact sealed runtime and matching tools.
All twelve ELFs relink, 3,186 guest NIDs resolve and 170 package files are verified;
the original dump remains unchanged. The package is **prepared, not device
executed**. See [preparation evidence](evidence/minigolf-combined-main-preparation-20261010.json).
Neither this build nor static shader coverage qualifies iPad/FEX execution,
Metal rendering, Solitaire regression, MiniGolf gameplay or an FPS improvement.

## Historical three-patch foundation, 2026-10-09

The new host relinker/NID tools build from the merged source. All **37 relinker
CTest cases pass**. Actual emitted Windows images also run under an isolated
Wine prefix: constructor/destructor fixtures and all five deferred lifecycle
scenarios pass. These are synthetic CPU/module tests, not a game run.

The Windows Release distribution now builds **47 actual x86-64 PE PRX
libraries**, including `libc`, `libkernel`, `libSceAgcDriver` and
`libSceVideoOut`. Every library has exports; the result is not a collection of
empty placeholder modules. **All 40 targets selected by the local HLE helper
and 10 additional runtime targets pass**, producing **53 positive runtime or
mock-Vulkan cases** under local Wine on macOS. The extra targets cover thread
keys/callback ABI, demangling, case-sensitive guest paths, dynamic loading,
unwind module information, HTTP2, offline imports and save-data search/mount
status. There are no CTest-level skips or failures.

The initial macOS/Wine qualification has **one internal skipped subcase**:
`guest_raise_exception` could not test asynchronous AVX context preservation
because that Wine host did not expose AVX, despite explicit opt-in. It verified
all 451 exception deliveries and integer state, including the nested-waiter
assertions. That historical result and its skip remain unchanged in the original
manifest.

A separate **native Windows CPU reference now passes all four current test
targets**, using the same executable and dependency bytes:
`windows_exception_tests`, `exception_personality_tests`,
`guest_raise_exception_tests` and `guest_memory_tests`. Each exits with code 0,
without a timeout or internal AVX skip. On Windows 11 with a Ryzen 9 7940HS, the
exception test verifies **553 deliveries** and **64 vector checks in each of
three modes** (`preserve`, `mutate`, `reset-avx`), with zero errors. The memory
test verifies **36 native CRT shared-write-tracking checks**. Binary/package and
returned stdout/stderr digests were independently rechecked. The separate
[Windows CPU manifest](evidence/anyps5-main-windows-cpu-20261009.json) records
this result without publishing raw logs or machine/network identifiers.
This closes the native Windows AVX comparison; iPad/FEX AVX delivery remains an
open device gate. It does not establish physical GPU rendering or gameplay.

The graphics cases check prepared descriptor ownership, write tracking, the
default-off/runtime-on timing paths and device-profile feature policy using CPU
fixtures and mock Vulkan. The MSAA disk-cache case exercises actual RDNA
`IMAGE_LOAD` programs, 2/4/8 samples, plain and array modes, prepared descriptor
materialization, and fresh-process disk loads. A separate negative control that
deliberately loses the saved sample-group policy fails as expected; it is not
included among the 53 positive cases. These tests do not execute shaders on a
physical GPU.

The migration updates retained ColorResolve code to upstream's
`materializeDrawStage` contract and corrects obsolete synthetic fixtures to the
new prepared/interpolation contracts. The production validation remains active.
After the final PRX build, the runtime, graphics and cache test binaries and
their local dependencies still matched the inputs used by the passing tests.
All 1,765 source files and executable modes matched the active patched
checkout. The public [qualification manifest](evidence/anyps5-main-integration-20261009.json)
records the upstream commit, the three as-built patch hashes, library/test hashes,
per-case log digests and the explicit skip without publishing raw diagnostics.

The installed app and private game packages remain separate from that historical
source qualification. The subsequent combined package is described above; a
matching device run is still required before claiming it fixes MiniGolf or
preserves Solitaire's gameplay. This milestone proves neither iPad/FEX execution, physical
GPU rendering, playable courses, nor an FPS improvement.
See [MiniGolf](MINIGOLF.md) and the historical
[Apple-only backport qualification](APPLE-UPSTREAM-INTEGRATION.md).

## Subsequent exception diagnostics

Patch0002 replaces the 1,023-character termination-message limit with direct
stderr writes in chunks of at most 4,096 bytes. Text is capped at one MiB, with
an explicit truncation marker for larger messages. Short writes, zero/error
returns and a bounded write-call count are handled without new heap allocation
or CRT stream locks. Synchronous operating-system writes have no wall-clock
deadline. Existing guest exception exports, `what()` behavior, custom terminate
handlers and final abort behavior are preserved.

The independent writer fixture passes twelve native Mac, sanitizer, local Wine
and native Windows cases. A rebuilt real `libc.prx` retains all 952 exports and
passes four native Windows CPU tests with AVX plus the physical GPU prerequisites.
Only the library and honest provenance manifests change in the fresh private
MiniGolf comparison. The game still fails at interpolation PC 56; the full
Serializer14 request is now captured and independently replays the same failure.
This diagnostic repair supplies a reproducible shader case, not a rendering fix.
The initial 47-library qualification above remains an immutable earlier build;
fresh combined packages must record the additional patch and rebuilt library.

## Subsequent image-descriptor policy

Patch0003 removes the unconditional requirement for native null-image descriptors.
The device profile freezes the feature actually enabled in the Vulkan creation
chain. Devices without that feature use `RequiresValidImages`: every selected
guest image descriptor must have a nonzero base address, and every consumed
Vulkan image write must contain a non-null image view. Initial descriptor writes
and captured write replay both enforce the policy. A null entry fails before
the Vulkan update with its stage, binding and element identified. No dummy
texture or null-image emulation is introduced.

Request serializer version 15 records this policy; versions 1–14 decode as
`RequiresValidImages`. Source, interface, context and disk-cache keys separate
the policies. Existing valid-image SPIR-V and heap materialization semantics are
unchanged. The isolated candidate passes the device-profile, descriptor-policy,
full graphics and full shader-cache suites under local Wine with mock Vulkan.
The production device-creation object also compiles. These checks do not prove
that MiniGolf binds only valid images or renders on Metal. All 47 PRXs are now rebuilt together in the combined qualification above; no
older ABI-dependent HLE binaries enter that package. Device verification remains
open.

## Subsequent unused barycentric inputs

The active stack also contains patch0004. In shaders without VINTRP it admits
declared barycentric input registers only after proving they are overwritten
before every read. The analysis starts from the original decoded guest CFG,
because the translator's supplied CFG has already been structurized. The
translation CFG and existing P1/P2 validation are unchanged.

The bounded must-analysis intersects all reachable predecessors to convergence,
tracks saved EXEC halves and scalar clobbers, and separates full-entry writes
from writes valid only within the current narrowed EXEC epoch. Operand reads
precede destination updates. Unsupported widths, cross-lane effects, indirect
flow, unknown instructions and unproved masks reject the shader. It does not
replace barycentric arithmetic with guessed values.

The current proof also accounts for enabled operands in uncompressed MRTZ
exports (depth and sample mask) and accepts only the exact `S_SENDMSG 7` ordered
boundary with no guest-register or EXEC effects. Other message variants,
payloads, unsupported exports and unproved reads remain rejected. It preserves
ordered-event reachability as well as exported values; equal final pixels alone
cannot justify a raw-dependent ordering boundary. The existing P1/P2 body remains
byte-identical.

**Forty-five public synthetic cases and the complete private MiniGolf shader**
pass actual preparation, capture, materialization and recompilation using the
fresh combined runtime. Prepared, captured and direct paths produce equivalent
final SPIR-V and bindings. The independent 64-lane concrete model accepts 640
program variants and rejects 640 in each of two runs (optimized native ARM64 and
ASan/UBSan), checking **921,600 raw-input comparisons per run**. No accepted
program depends on the initial raw inputs. It checks color/depth/sample-mask
bits and ordered-message event traces, rather than executing a GPU interlock.

Three isolated mutations are detected: falsely treating subset writes as full
entry writes, omitting enabled MRTZ reads, and omitting a raw comparison that
controls ordered-message reachability. The third control has equal final pixel
values but a concrete ordered-event dependency. Production source is unchanged
during these controls. This bounded model is not full RDNA conformance and does
not execute FEX, LLVM, floating-point evaluation or a physical GPU.
[Current proof evidence](evidence/anyps5-unused-barycentric-proof-20261010.json)
and the [historical 30-case proof](evidence/anyps5-unused-barycentric-proof-20261009.json)
remain separate. The combined HLE build is complete; actual device rendering is
still an open gate.

## Reproduction

The latest cumulative patch0004 also covers 3D sampling, lane arithmetic,
scalar-buffer pairs, array/MSAA loads and MRT0–7 color exports. Its isolated
actual-PRX qualification passes 302 public cases and seven complete shader
materializations, with all six retained SPIR-V/layout pairs byte-identical and
the 3,692-symbol ABI, native imports and 3,186 guest NIDs intact. The seventh
request's two MSL variants create macOS Metal source libraries. These changes
are now saved in the canonical source and patch stack. The preceding full
quad-mask build and historical baseline above are separate frozen artifacts;
they do not qualify the expanded source. A fresh complete build, generated
cache identity, native Windows game comparison and physical-iPad checks remain
open. [Latest qualification](evidence/minigolf-mrt-export-candidate-20261010.json).

Use the pinned checkout and apply only the top-level patch files:

```sh
scripts/apply-patches.sh --only anyps5
python3 tools/checks/check_patch_stack.py
python3 scripts/build-hle.py --wine /absolute/path/to/wine --jobs 2 \
  --build build/anyps5-main-winlibs --output build/hle-runtime/main.zip
```

The helper's default builds all available PRX targets; the qualified 47-library
distribution is a dependency subset, not a claim that every upstream HLE API is
implemented. Explicit target selection or `--from-hle` can constrain a
distribution to a validated title dependency set. The manifest lists the exact
47 libraries and 42 selected plus 10 additional test targets. The ten additional
programs are listed separately from the helper's selected suite. A fresh build
directory is required when moving
from the old resource architecture. Use matching relinker/NID tools and fresh
HLE copies when preparing each private title. Original dumps, title assets and
raw shader diagnostics remain outside Git.
