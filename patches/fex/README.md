# FEX-2610 on the iOS host

The gitlink names the official `FEX-2610` commit
`14c92681f4d62cf84d901460e0358de09c8847a7`. Apply this series in order with
`scripts/apply-patches.sh --only fex`.

`0000-ios-port-2610.patch` reconciles the Madeira iOS fork
`3bec2ac498bf78156ab47c0c194b0e8cb2849756` with the complete monthly release.
It retains dual RW/RX JIT mappings, the iOS arena/allocator hooks, TEB/TSD
access, code-pool migration, the 32-bit guest window and Mono bridge. It also
includes all former local patches 0002–0011: resolved-config diagnostics,
opt-in guest/block capture, per-compiler capture state, allocation declaration,
detach notification gating, SysV red-zone preservation and pending upper-AVX
state at protected guest faults. Their individual history remains in Git.

New release adaptations include the shared atomic code-buffer allocator,
direct architectural-state syscall API, Apple cache-line encoding, native
Darwin directory streams and entropy, and nonblocking file-read invalidation.
The latter removes locks held across ordinary reads, but Wine delivery into an
already executed, write-protected code page remains an explicitly failed device
case. It does not qualify all executable-page or asynchronous reads.
The iOS WOW64 module is an explicit adapter in `IosModule.inc`: the pinned Wine
uses the legacy CPU-area protocol, whereas the unchanged canonical module in
the other branch uses CHPE-v2. Both modules use the new FEX core/syscall API.
Compilation of that adapter does not qualify 32-bit applications on a device.

`0001-rpmalloc-ios-port.patch` uses the release's recorded rpmalloc commit
`09142d726429416bfa7b459151515fe3ab7622dd`. It reconciles the Madeira allocator
fork `812c2b9cf4310ffacf14e6b64066e78ab0c394b5`, including its iOS VA bands,
failure diagnostics, license and POSIX logging adapter. No unpublished merge
commit or floating branch is required to reproduce either checkout.

Validate the full overlapping series with `scripts/check-patch-series.py`.
`scripts/check-fex-darwin-utils.sh` tests the actual Apple directory and entropy
implementations on macOS with ASan/UBSan. `scripts/check-async-flags-layout.py`
checks the native/PE exception bridge ABI. Device evidence is separate under
`docs/evidence/`.
