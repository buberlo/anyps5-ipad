# Apple upstream changes: applicability to madeira-anyps5

The subsequent [implementation and qualification](APPLE-UPSTREAM-INTEGRATION.md)
records the adopted changes. This page preserves the initial review's findings;
its nonmutating status describes that review, not the later integration.

Reviewed against live GitHub metadata on 2026-10-09 and the patched local
sources. This is an integration assessment, not a new build, installed runtime,
gameplay result or measured speedup. No runtime sources, dependency pins or iPad
files were changed during this review. Patch applicability checks ran locally;
metadata was retrieved from GitHub. No GitHub Actions were run.

The comparison uses AnyPS5 `6e037e9899efb7439913a6e1151b901a92c9c9c8`,
Madeira `48f976429c189f8396e23d251d8a82f43c705922`, MoltenVK
`67b2682699d7903606b97a0392061d85d27d49e6` and its SPIRV-Cross dependency
`aa217aeb6c9f0ace7a0ab233b28807edf45eb165`.

## Changes useful to the existing Wine/FEX path

| Change | Live state at review | Applicability and required checks |
|---|---|---|
| [AnyPS5 #927](https://github.com/boykopovar/AnyPS5/pull/927) | Merged on 2026-10-08; absent from our pin | The guest C personality export and unwind dispatch affect Windows too. Import those changes and their C cleanup regression with the next controlled AnyPS5 update. The macOS libc++/Mach-O exception code is a separate host path. |
| [AnyPS5 #1571](https://github.com/boykopovar/AnyPS5/pull/1571) | Open | Its one-line shader-IR change applies cleanly to our patched source. It protects the smallest normal result of `exp2(-126)` from Apple GPU undershoot and flushing. Test the production unary shader across the boundary, infinities and NaNs on the M2, retaining underflow semantics below the boundary. Requires an HLE rebuild and new private package. |
| [SPIRV-Cross #2704](https://github.com/KhronosGroup/SPIRV-Cross/pull/2704) | Open | All seven changed files apply cleanly to the checked-out dependency. Precise MSL multiplication and matrix accumulations preserve negative zero by using a negative-zero FMA addend. Test scalar/vector/matrix float and half results, including signed zero. Requires a reproducibly patched SPIRV-Cross, MoltenVK rebuild and native app rebuild; replacing HLE libraries alone cannot install this change. |
| [AnyPS5 #1580](https://github.com/boykopovar/AnyPS5/pull/1580) | Open | Permits storage-buffer layouts above ordinary descriptor limits when the device supports update-after-bind and the corresponding limits admit them. Device/context changes apply; resource management and tests conflict with our pin. Integrate with their newer descriptor validation and pool ownership dependencies, then test regular, admitted extended and rejected extended layouts. Qualify the actual M2 limits and GPU test before a title run. |
| [AnyPS5 #1565](https://github.com/boykopovar/AnyPS5/pull/1565) | Open | Makes the host-lane test explicitly skip its vertex draw when vertex subgroups are unsupported, while retaining the compute test. Its test file is absent from our pin. Bring it in with the newer test suite and report the skip honestly. It does not implement vertex subgroups or accelerate a game. |

The nonmutating patch checks establish source applicability only. They do not
prove shader behavior, descriptor lifetime, iPad compatibility or a frame-rate
improvement. None of these GPU candidates repairs the last qualified MiniGolf
startup failure in the JSON virtual allocator. That repair has separate host
qualification and an outstanding device execution check.

## Native ARM64 HLE with FEXCore

[arvindfroi's native runner](https://github.com/mugurc/AnyPS5/pull/4) is a useful
experimental reference. It shares our x86-to-ARM CPU translation and
Vulkan/MoltenVK graphics approach, but changes the host architecture:

```text
Current iPad: relinked Windows PE → Wine + FEX → Windows HLE → Wine Vulkan → MoltenVK
Native experiment: relinked ELF → in-process FEXCore → ARM64 HLE → Vulkan/MoltenVK
```

Native HLE could remove translation of substantial library code and avoid Wine
dispatch at those interfaces. That is a performance hypothesis requiring a
matched measurement. The published native results cover synthetic guest
programs and GPU tests, not an accepted MiniGolf session on iPad. See the
[author's initial CPU results](https://github.com/boykopovar/AnyPS5/issues/1#issuecomment-6063347492)
and [GPU results](https://github.com/boykopovar/AnyPS5/issues/1#issuecomment-6063949682).

The runner uses dynamically loaded native libraries, an AppKit main loop and a
1-GiB JIT pool. Our iOS app has signing/static-link requirements, UIKit lifecycle
and a qualified 512-MiB pool. Its documented missing guest signal handlers and
exception paths matter to Unity: PS5Util uses a guest signal handler and saved
guest stack state for GC safepoints. Switching MiniGolf to this runner would
discard currently working memory/exception integration before these contracts
are available.

Evaluate this route separately using the existing lifecycle, TLS, callbacks,
exceptions and protected-memory fixtures. Keep the working Wine path as the
reference. The runner deliberately selects an earlier MIT-licensed FEX fork
commit; a direct native integration also needs its own dependency/linking review
instead of reusing our current separately loaded Windows HLE packaging.

## Ahead-of-time ARM64 compilation

[AnyPS5 #1835](https://github.com/boykopovar/AnyPS5/pull/1835) is open and
experimental. It emits native ARM64 objects for explicitly described functions;
it is compiler-only. It does not provide a complete native PS5 runtime or a
runnable game package. TLS, guest C++ exceptions, aggregates/varargs, general
computed jumps, atomics, concurrent memory semantics and module lifecycle remain
open. Function signatures and indirect targets require explicit contracts.

The current PR also reports that its `hello_notify` and `gpu_cube` research
inputs reject at a later startup import under the stricter pointer rules. Its
native arithmetic/callback tests cannot establish complete Unity/IL2CPP game
translation. Use a small, hash-bound synthetic function trial before considering
selected hot functions; do not replace the MiniGolf runtime with it.

## Why Madeira #227 is excluded

[Madeira #227](https://github.com/willfaust/Madeira/pull/227) is open. It targets
unhinted 1–2-GiB `MEM_RESERVE`, `PAGE_READWRITE` requests after a cumulative
threshold. Our arena instead reserves fixed 256-MiB chunks with
`MEM_RESERVE | MEM_RESERVE_PLACEHOLDER` and `PAGE_NOACCESS`; it does not satisfy
the patch's predicates.

The thin allocator advertises the full request while physically reserving a
63-MiB prefix plus a 1-MiB guard by default. Later slots occupy the earlier
reservation's advertised tail. A sparse commit into the next slot's prefix can
therefore act on that other allocation; the guard protects contiguous growth
only. Query, protection and decommit cannot recover the intended owner from the
address alone. Its prefix-only tests do not exercise this aliasing.

This is unsuitable for a general guest arena. Retain real, nonoverlapping lazy
reservations. Reservation/commit high-water diagnostics may be useful if a
measured address-space problem appears, but there is no demonstrated MiniGolf
need or speedup from this scheme.

## Integration order

Preserve the current private MiniGolf candidate while finishing its startup
qualification. For the next controlled upstream build, take #927's cross-platform
exception change first, then the bounded #1571 and #2704 shader corrections.
Integrate #1580 after reconciling the newer descriptor APIs and lifetime rules.
Include #1565 as test coverage with explicit capability skips. These are
compatibility changes; qualify each using its real host/GPU path before
comparing game performance. Native FEXCore and AOT remain separate experiments.

Machine-readable status, exact reviewed heads and source-application results:
[review evidence](evidence/apple-upstream-review-20261009.json).
