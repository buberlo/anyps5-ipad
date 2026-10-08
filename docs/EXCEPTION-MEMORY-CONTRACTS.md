# Exception and protected-memory CPU state

Asynchronous `sceKernelRaiseException` delivery and ordinary Windows access
violations use different paths. Passing the asynchronous tests did not qualify
write-watch handling. A protected store can enter libc's VEH through FEX's
ARM64EC exception dispatcher without preserving all x86 state in `CONTEXT`.

## Ordinary protected-store repair

On the M2 iPad, the independent protected-store probe found correct general
registers and lower XMM halves, but lost upper halves of all sixteen YMM
registers, parity/auxiliary flags, and the direction flag. An interrupted DF=1
could also enter C++ with DF still set. The default-path six-case probe reported
103 errors. Its handler deliberately clears vector registers so accidental
preservation cannot pass the test.

`APS5_VEH_CONTEXT_BRIDGE=1` enables a paired native Madeira/libc bridge. Both
components must be rebuilt; updating only the app or only libc is insufficient.
The default remains disabled while game compatibility is unresolved.

1. Before the native `NtContinueEx` hands an access violation to
   `KiUserExceptionDispatcher`, Madeira identifies the current thread's FEX
   frame. It requires the exception PC/SP to match the reconstructed guest
   PC/SP, rejects native EC code and copies the missing state.
2. A private `NtQueryInformationThread` class, `0x7fff4151`, exposes a versioned
   328-byte snapshot to that same thread, using the current-thread pseudo handle.
   Exception code, access direction, fault address, PC and SP must match the
   latest snapshot. The transport does not expose the native CPU-frame pointer.
3. libc fills the missing flags before its existing access-violation handler.
   Heap and write-watch handling, protection changes, and dirty tracking remain
   enabled. Before C++ runs, the native dispatcher clears DF while retaining its
   original value in the snapshot.
4. For a handled exception, libc prepares restoration with the snapshot token
   and the handler's final PC/SP/flags. Native `NtContinueEx` restores PF, AF,
   DF and the sixteen upper YMM halves immediately before resuming. Wine retains
   the normal general-register/lower-XMM context path.

The snapshot state is thread-local and bounded to eight slots. Prepared outer
continuations are not evicted by a nested capture. A continuation must still
belong to the current FEX CPU frame; a changed frame is rejected instead of
writing through an obsolete pointer. Once libc detects the native provider, a
handled x86 access violation without a matching snapshot or with a failed preparation terminates the process rather
than silently continuing with corrupt CPU state. Hosts without this provider
retain their ordinary Windows exception path. Native ARM64EC accesses also use
the standard context path: `RtlIsEcCode` classifies the context PC and original
exception address, including Wine's relocated CRT code. They must not restore
a stale x86 CPU frame. This is a private pinned-runtime interface, not a general Windows `CONTEXT` replacement.

`APS5_VEH_CONTEXT_TRACE=1` enables at most 64 capture/restore messages per thread.
It is disabled by default. Failure reports remain enabled.

## Verification

Build the synthetic probe with `scripts/build-runtime-probes.sh` after applying
patches, or use a Windows x64 GCC toolchain:

```sh
x86_64-w64-mingw32-gcc -std=gnu11 -O2 -Wall -Wextra -Werror -static-libgcc \
  -Iupstreams/Madeira/build/ntdll-unix \
  tools/runtime-probes/protected_store_state_probe.c \
  -o protected-store-state-probe.exe
```

On the paired iPad runtime, set `APS5_VEH_CONTEXT_BRIDGE=1` before Wine starts.
The expanded test checks 192 outer faults and 96 nested faults. It verifies all
fifteen general registers, seven arithmetic/direction flags, lower/upper halves
of all sixteen YMM registers, handler changes, DF=0 on callback entry, retried
store data, and 864 rejected malformed-length, wrong-token or invalid-flags
requests. Stores cover scalar, 128-bit and 256-bit `vmovdqu`, `vmovupd` and
`vmovups`, an RBX destination, and an indexed four-load 112-byte packet copy
with overlapping vector loads. Both zero and sixteen-byte destination offsets
are checked; all untouched bytes in the 16-KiB destination page must retain
their guard pattern. Page crossings require the separate mode below.
A host without AVX or the native bridge does not qualify those cases.

Run `protected-store-state-probe.exe --cross-page` to leave the first native
16-KiB page writable and protect the second. This mode places the destination
1, 4, 7, 8, 15, 16, 24 or 31 bytes before the boundary, using a position only
when the selected transfer crosses it. It checks all 32 KiB, including bytes
outside the transfer. Build 42 passes 153 cases with zero errors, 459 rejected
queries and Wine exit 0. The same binary's default mode also passes the original
192 cases and 96 nested faults. Crossing tests do not enable nesting or handler
mutations. See the [page-crossing evidence](evidence/ipad-cross-page-protected-stores-20261008.json).

Run `protected-store-state-probe.exe --non-temporal` for sixteen aligned
`vmovntdq` stores followed by `sfence`. Moving the destination in sixteen-byte
steps makes each of the sixteen register stores the first protected write in
turn. Two load modes retain full YMM values or use VEX XMM loads that zero their
upper halves. Three flag patterns produce 96 cases; each checks all fifteen
general registers, sixteen YMM registers, seven arithmetic/direction flags and
all 32 KiB of destination/guard memory. The handler deliberately executes
`vzeroall`. This mode requires AVX and returns 77 rather than qualifying a
skipped run. Nesting and handler mutations remain covered by the default mode.
See the [non-temporal evidence](evidence/ipad-non-temporal-protected-stores-20261008.json).

`GuestMemory.cpp` separately exercises the real libc handler with native CRT
`memcpy` and `memset`, using nine transfer sizes across protected 16-KiB pages,
both shared aliases, dirty collection, re-arming and release. All 36 cases pass
on the physical iPad with the bridge enabled; the ordinary guest-memory checks
remain enabled as well.

`scripts/check-async-flags-layout.py` compiles against the actual pinned FEX
headers before a native build. It verifies the frame prefix, AVX-high offset and
size, flag fields, RSP index, and the transport layout. No FEX implementation is
changed by this bridge. Future dependency updates must pass this check again.

The [paired bridge evidence](evidence/ipad-protected-store-state-20261008.json)
records the binaries, source hashes, initial positive/negative probes, and game
limits. The [expanded vector-store evidence](evidence/ipad-vector-protected-stores-20261008.json)
records the subsequent 192-case Build 40 run, with zero errors and Wine exit 0.
The host comparison passes six scalar cases but skips AVX; it does not qualify
the vector or native bridge path.

The separate asynchronous opt-in path uses `APS5_X64_SIGNAL_SUSPEND=1`,
`APS5_PRESERVE_ASYNC_AVX=1` and `APS5_PRESERVE_ASYNC_FLAGS=1`; its 1,004 device
deliveries are documented in the [asynchronous state record](evidence/ipad-async-integer-flags-20261008.json).
The full guest-memory suite remains a separate check of reservations, aliases,
protection, locking, write-watch and release. Neither suite replaces the other.

## Bounded same-process reads

A separate Build 40 probe passed seven valid `ReadProcessMemory` calls, then
faulted in native code on its first `PAGE_NOACCESS` source read. The Unix
`__TRY` copy did not recover this ARM64EC access fault. The failure also prevented
a post-mortem read across a game stack guard.

`APS5_SAFE_SELF_READ=1` enables an opt-in native path for current-process reads
whose source starts at or above 4 GiB. The destination's ordinary write-watch
probe remains enabled. Under the virtual-memory mutex, the path checks each
source page owned by Wine for committed/readable state and guards before using
`mach_vm_read_overwrite`. Foreign native allocations are checked by Mach. A
failed or short copy returns `STATUS_PARTIAL_COPY` with zero reported bytes.
The default remains off while game compatibility is unresolved.

The logical source check matters: an initial kernel-only implementation read
two decommitted pages through their still-accessible native backing and was
rejected. Build 42 passes all thirteen independent source-read cases with zero
errors, zero exception callbacks and Wine exit 0. The cases include native
16-KiB boundaries, read-only, no-access, guarded and decommitted sources. This
matches the documented requirement that inaccessible source ranges fail
([Microsoft ReadProcessMemory documentation](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory)).
See the [safe-read evidence](evidence/ipad-safe-self-read-20261008.json) and
`tools/runtime-probes/self_read_probe.c`.

Low guest aliases and remote process-handle reads retain their existing paths.
Invalid or racing destination buffers are not qualified or repaired by this
source-read change. It does not enable cross-process writes or change source
permissions, and it does not qualify gameplay.

A separate synthetic read-fault continuation checks a handler that changes RAX
and advances PC by three bytes rather than retrying the inaccessible load. Build
42 passes twelve scalar/AVX cases, preserving the other general registers,
all sixteen vector registers and seven flags. It rejects 36 malformed requests
and exits Wine with code 0. This qualifies that bounded continuation only, not
arbitrary instruction emulation. See the [read-continuation evidence](evidence/ipad-read-fault-continuation-20261008.json).

## Graphics qualification remains separate

The fixed-function interpolation path retains validation of supported RDNA I/J
programs. All 111 host guard cases and the prior fourteen complete private
SPIR-V replays pass. Unrecognized interpolation programs and unsupported sample
counts remain explicit errors. No new interpolation approximation is introduced
by the CPU-state bridge.

Solitaire still has an unresolved game-side null-pointer fault in
`UnityGfxDeviceWorker`; the Build 38 and Build 40 bridge runs read `0x30`
at game RVA `0x1461222`
and stayed on the launch screen. With bounded same-process reads enabled, a
private Build 42 diagnostic captures 1,680 stack bytes and fifteen readable heap
regions, safely rejects one unreadable region, and completes its report. The
game still faults on a shader-table pointer (`0x2`) at RVA `0x1426ac4`; the
35-second screenshot shows a white game surface. Raw game memory and shader
contents remain private. A subsequent bounded source-buffer capture proves that
all 112 copied packet bytes match their source in that run. It still faults at
RVA `0x1426827`: the non-null shader object has a null function table at the
fatal call. This narrows the next investigation to object creation/lifetime or
other writes, but does not identify their cause or qualify every packet copy.
See the [packet checkpoint](evidence/solitaire-build42-shader-packet-checkpoint.json).
An additional private observer records six shader constructions and 78 program
packets. The first 77 packets reference the captured objects; the final packet
contains another pointer, already present in its source buffer. All six observed
shader objects still have their expected function tables at the fatal call.
The full 112-byte copy matches again. This identifies a bad source packet,
without proving which writer introduced it. The private instruction observers
can change timing and are not production fixes. A separate observer of existing
write faults also finds guest libc non-temporal copies. The independent
non-temporal suite above now covers register-preserving continuations for its
own store sequence. See the [constructor/call checkpoint](evidence/solitaire-build42-shader-call-checkpoint.json).

A private black-box probe then loads the actual supplied copy export and calls
it with its own deterministic data. Its real HLE arena scratch allocation uses
separate native-page mappings, so protection changes are applied to each page
individually. All 208 size/alignment cases pass, including 302 handled write
faults, zero source/destination/guard differences and correct return pointers.
The own VEH deliberately destroys vector state and uses the paired native
query/prepare protocol. This tests neither overlapping buffers nor concurrent
writers; the separate real HLE dirty-tracking tests are still required.
The supplied library and raw diagnostics stay private. See the
[copy contract record](evidence/ipad-supplied-copy-contract-20261008.json).

A bounded run of the same game observer with scalar, vector, memcpy/set and
half-barrier software TSO enabled confirms those settings in the actual FEX
runtime, but still faults reading `0x30` at RVA `0x1461222`. The experiment does
not qualify a fix or a performance improvement, and its settings are restored.
See the [stricter TSO checkpoint](evidence/solitaire-build42-stricter-tso-checkpoint.json).
These results narrow the tested failure hypotheses; the packet writer remains
unidentified and arbitrary instruction sequences remain unqualified.
Earlier runs rejected an 8-sample draw on the
M2, whose queried Vulkan attachment counts are 1, 2 and 4. The CPU test repair
therefore does not establish rendered or playable Solitaire.
