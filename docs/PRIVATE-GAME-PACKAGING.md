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

```sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/prepare-private-game.py \
    --dump PPSA02929-app0 \
    --hle build/windows-runtime-artifact/hle-runtime.zip \
    --output build/dreaming-sarah-runtime
```

The preparer selects decrypted ELF files by magic. A `.esbak` copy is used only
when it is an ELF; conflicting decrypted originals/backups cause an error.
Original files are immutable. Relinking processes all bundled modules and checks
syscalls; no deprecated skip switches are enabled. Assets are copied into
`app0/`, including the runtime VFS `~INDEX`; this is a game asset, not a
dumper sidecar. The converted bundled `libc` remains a guest module.

The script recursively packages real native dependencies, NID-patches fresh HLE
copies, validates their PE imports/forwarders and checks the generated guest NIDs
against the exact handles searched by the relinker. The Windows TLS resolver is
the one explicit generated symbol. Missing dependencies leave the package in
`not_ready_missing_dependencies` and return exit code 2. Omitting `--hle` also
produces a useful inventory and relinked assets with this explicitly incomplete
status; it never claims readiness. Inputs, binaries, assets, build provenance and
tool hashes are retained in `private-game-manifest.json`.

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
