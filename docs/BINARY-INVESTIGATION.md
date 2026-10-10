# Guest binary investigation with REA

[REA](https://github.com/morluto/rea) is an optional local investigation tool.
It gathers assembly, decompiler output, callers, callees, references and
control-flow information around a selected function. It is not a runtime
dependency of the app or game preparer.

Start with the **relinked x86-64 PE** whose hash matches the installed package.
Convert a reported guest address to its module RVA using that run's image map,
then use the corresponding preferred analysis address. An ARM64 JIT address is
not directly an x86-64 analysis address. Preserve the binary hash, module base,
RVA, provider/version and raw instruction evidence with each finding.

## Locally tested workflow

The 2026-10-09 trial uses REA 6.1.0, Ghidra 12.1.4 and Temurin JDK 21.0.8+9
on macOS ARM64. It analyzes a 116,224-byte source-owned lifecycle fixture;
no game bytes are needed for this qualification. Tools and detailed results
remain in ignored `build/toolchains/rea-local/`. No Codex configuration or
iPad installation changes are made.

The useful speed improvement is **keeping one analysis session open**:

- Separate function commands take 15.247 and 14.867 seconds in this fixture.
- In one production library session, the first query takes 13.297 seconds
  including lazy backend startup; the next two functions take 0.006 and 0.014
  seconds. The session, including close, takes 13.891 seconds inside the client;
  the complete command takes 14.549 seconds.
- This measures analysis startup overhead on a small synthetic module. It is
  not an overall project-speed, large-game analysis or game-FPS result.

Use one ephemeral session to investigate an error, its producer and its callers.
Export bounded dossiers and close afterward. REA's MCP route also supports
sessions; the trial uses the same production library factory without adding
a persistent server to Codex.

The tested Ghidra route **does not demonstrate snapshot cache acceleration**.
The function and instruction commands save evidence bundles with zero replay
entries. Repeating the function command starts analysis again. Do not assume
`--snapshot` removes that startup cost.

## Calling-convention boundary

Relinked files combine Windows loader glue with PS5 guest code. Automatic PE
analysis selects Ghidra's Windows compiler convention, whereas the tested guest
lifecycle functions use SysV: `RDI` carries a full 64-bit argument count, `RSI`
the payload pointer, and `RAX` the packed 64-bit result.

The trial's assembly preserves these facts, including the initializer's
indirect callback at `[RSI+8]`. Its automatic pseudocode instead declares
`__fastcall __anyps5_guest_start_v1(void)` and drops both arguments. Result width
and state-machine constants are recovered, but the inferred prototype is wrong.
Validate per-function conventions and instruction flow before implementing an
HLE API from pseudocode. One global convention does not qualify every function
in a mixed-ABI image.

REA 6.1.0's stock function-annotation API changes names and comments only; it
does not expose per-function prototype or calling-convention correction. Its
typed parameter and call-flow results therefore remain unqualified for this
guest ABI. A future adapter change needs its own mixed-ABI fixture tests.

## First use on a real startup failure

MiniGolf's JSON initialization caller is also navigable in the relinked PE.
REA's assembly confirms the allocator argument and file-buffer setting. Initial
import and caller analysis take 46.304 seconds in this module. A subsequent
query for a small virtual method is rejected because automatic analysis has
not discovered it. The session closes successfully and input bytes are unchanged.
This is a partial navigation result; it does not extend the synthetic timing
claim to every function in a game.

Sony dynamic symbols and relative virtual-table relocations need a separate
audit. Original ELF relocation records, source-computed NIDs and byte-identical
LLVM disassembly establish the missing allocation/release/notification slots.
The caller accesses its parameter through imported methods; its available stack
span does not establish the original class layout. Automatic pseudocode again
uses the Windows convention and cannot supply that ABI proof.

Use REA for a retained view of discovered callers and branches. Use the original
ELF/relocation evidence for Sony imports and indirect targets. A future loader
adapter that registers those targets and correct per-function conventions could
reduce manual work; that adapter is not implemented or qualified here.

## Scope and setup limits

Use REA for missing API contracts, callback payloads, error branches and CPU
wait/caller relationships. Existing source inspection and deterministic tests
remain the first choice for our own HLE implementations.

REA's [managed analysis guide](https://github.com/morluto/rea/blob/main/docs/managed-code-analysis.md)
explicitly excludes IL2CPP metadata decoding. This trial does not qualify Sony
ELF loading, PS5 shader translation, GPU correctness, iPad exception delivery or
runtime profiling. Those still require their respective tools and device tests.

The Mac setup requires a matching Ghidra native decompiler; REA does not build
it. The trial builds it from Ghidra's shipped source and applies a narrow local
installed-package adjustment: Swift receives an explicit `-module-cache-path`
inside its own temporary directory. The process ownership helper and its checks
remain intact. Local Unix-socket communication and process inspection need the
appropriate host permission. These details qualify the tested setup, not every
unmodified installation.

Keep game binaries and detailed findings local. Local analysis does not change
the data-handling rules for any agent receiving results. The [sanitized trial
record](evidence/rea-native-investigation-trial-20261009.json) contains only tool
and fixture identities, observations and timing limits.
