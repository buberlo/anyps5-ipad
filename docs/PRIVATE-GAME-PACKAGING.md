# Private game preparation

Private dumps stay outside source control. The repository ignores root folders
matching `PPSA*-app0/`; all generated binaries, inventories and game reports live
under ignored `build/`. Never add a dump to CI artifacts or Git.

Run `scripts/windows-runtime-build.sh` locally on Windows in Git Bash to build
the pinned AnyPS5 implementations for the additional audio, dialog, save, user
and system APIs used by Dreaming Sarah. It exports
`build/windows-runtime-artifact/hle-runtime.zip`, containing only unpatched HLE
PRXs and compiler DLLs, with a hash manifest. It contains no game executable or
assets. Libraries use the qualified WinLibs SEH toolchain. Builds do not use
GitHub Actions.

If preparing on another host, copy that archive to the same local path there.
Then prepare a new local output directory:

The default Windows build covers the demo and Dreaming Sarah. For another title,
`APS5_HLE_TARGETS_FILE` can name additional existing upstream CMake library
targets, one per line. It accepts only library directories under
`upstreams/AnyPS5/core/libs/prx`, deduplicates them, and rejects unknown names or
shell/path syntax. This builds the actual upstream implementation; functions
that upstream leaves unimplemented still throw when called.

```sh
APS5_HLE_TARGETS_FILE=/path/to/private-hle-targets.txt \
    scripts/m0-build-anyps5-winlibs.sh --print-hle-targets
APS5_HLE_TARGETS_FILE=/path/to/private-hle-targets.txt \
    scripts/windows-runtime-build.sh
```

`APS5_CMAKE` can select a specific CMake executable when using portable build
tools. The dependency audit, rather than the number of built libraries, decides
whether a package is complete.

The Windows HLE builder initializes and patches only the AnyPS5 submodule before
validating library targets. A fresh Windows checkout does not need Madeira, FEX,
Wine or MoltenVK source trees for this build. Other builds can select a patch
series with `scripts/apply-patches.sh --only SERIES`; the default still applies
all series.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/prepare-private-game.py \
    --dump PPSA02929-app0 \
    --hle build/windows-runtime-artifact/hle-runtime.zip \
    --output build/dreaming-sarah-runtime
```

The preparer selects decrypted ELF files by magic. It also accepts a
`decrypted/` overlay with the same relative paths; encrypted originals remain
untouched. A `.esbak` copy is used only when it is an ELF; conflicting decrypted
originals, overlays or backups cause an error. Bundled modules in `sce_module`,
`sce_modules`, `prx`, `Media/Modules` and `Media/Plugins` retain their directory
layout when converted. Both `sce_module` spellings in one input are ambiguous
and rejected. Encrypted dummy modules under `fakelib/` and unconverted module
files are excluded from the runtime package.

Original files are immutable. Relinking processes all selected modules and checks
syscalls; no deprecated skip switches are enabled. Assets are copied into
`app0/`, including the runtime VFS `~INDEX`; this is a game asset, not a
dumper sidecar. The converted bundled `libc` remains a guest module.

Patch0067 handles an omitted producer `PT_NOTE` only when its flags, virtual
address and memory size are all zero. This metadata has no runtime mapping and
neither output writer consumes it. File bounds remain strict for mapped notes,
LOAD, DYNAMIC and TLS segments and for symbol, relocation and initialization
tables. No missing bytes are padded or invented. The synthetic relinker test
checks both Windows and Linux output, with negative controls for truncated
runtime segments and invalid imports, exports and initializers.

Runtime-started guest modules can be selected explicitly with repeated
`--defer-guest-lifecycle NAME` options. Each name must be an exact, unique
discovered PRX basename; paths, globs, duplicates and unknown modules are
rejected, as are the main executable, excluded modules and non-Windows relinking.
The preparer records this selection in the private manifest and
passes it to the matching relinker. Use it only after establishing the game's
real module-start call and arguments. A declared ELF dependency alone does not
prove that its module-start callback should run before the game's loader.
All module conversion, binding, TLS and ELF bounds checks remain in place;
the default preparation keeps the existing eager initialization policy. An eager
guest module depending on a selected deferred provider is rejected.

Use matched tools and HLE from patches0069–0070 for this option, then prepare a
new output directory. The generated guest PE's reserved versioned start/stop
exports share state between bootstrap and the kernel APIs. A selected lifecycle
receives the game's actual full-width argument count and argument pointer;
mapping the image alone does not start it. Successful callbacks run once, busy
callbacks return an operational error, and failed callbacks preserve their exact
result. Native/HLE images without guest lifecycle exports retain their ordinary
load behavior. The preparer does not supply a synthetic argument block or infer
module-start ownership from an import list.

The loader keeps an image alive while a callback runs and calls outside its
registry lock. Module-start results and loader/guard errors remain separate;
a returned load handle alone does not prove that the guest start result was zero.
A failed stop prevents unload and is cached without repeating finalizers.
Because reverse fini arrays run before the stop callback, a failed stop may have
partially finalized guest state; rollback and restart are not supported.

The lifecycle patches pass native and synthetic Wine contracts, including
constructor ordering, real payload forwarding, failure, concurrency and rejected
negative controls. Those checks do not establish a fresh HLE/package build or a
game/device result. See [patch behavior and validation](PATCHES.md#explicit-guest-module-lifecycle-anyps5-00690070).

The script recursively packages real native dependencies, NID-patches fresh HLE
copies, validates their PE imports/forwarders and checks the generated guest NIDs
against the exact handles searched by the relinker. The Windows TLS resolver is
the one explicit generated symbol. Missing dependencies leave the package in
`not_ready_missing_dependencies` and return exit code 2. Omitting `--hle` also
produces a useful inventory and relinked assets with this explicitly incomplete
status; it never claims readiness. Inputs, binaries, assets, build provenance and
tool hashes are retained in `private-game-manifest.json`.

The audit also reports each unresolved guest NID with its declared library and
referencing module where the ELF provides qualified symbol names. These are
diagnostic hints; only the relinker's import list controls readiness. An optional
local whitespace-separated NID/name catalog can be supplied with `--nid-catalog`.
Each name must reproduce the NID hash before it appears in the report. The tool
does not download a catalog or send game diagnostics anywhere. A symbol name or
library being identified does not satisfy a missing function import.

`prepared_unexecuted` means only that static dependency validation passed. It
does not prove Windows graphics, iPad startup or gameplay. Qualify the package
on a native Windows PC with a Vulkan GPU, using the existing probes, and test
that same package on the iPad with its qualified 464–468 GiB arena. Menu,
gameplay, touch, audio and save/load are separate results. On the M2 iPad, a
maintainer recording shows library launch, the menu, and basic touch-controlled
gameplay, including logo audio and music. Save/load, displayed FPS and formal
audio acceptance remain open. See the README device-results table. Shared iPad
actions must use `scripts/with-ipad-lease.py`.

Validation: `python3 tools/runtime-probes/test_private_game.py` covers malformed
ELF tables, original/backup selection, unsafe dependency paths, archive hashes
unexpected game data in an HLE archive, and preservation of the runtime
index while excluding dump executables and backups. The actual game is never used as a
public test fixture.
