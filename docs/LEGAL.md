# Legal and licensing

This is an interoperability research prototype. It is not affiliated with
Sony Interactive Entertainment or Apple. It does not contain, and
must not gain:

- PS5 or other game dumps, pkg files, or extracted assets
- firmware, BIOS, or system software images
- encryption keys, tokens, or decrypted executables
- proprietary SDK libraries

Users supply their own material and are responsible for having the right to
use it. AnyPS5's upstream README says the same thing about its own project.

The copyright holder for files written in this repository is buberlo.
Copyright (C) 2026 buberlo. Upstream authors keep the copyright in their
own projects. The findings behind the table below are quoted in
[NOTICE](../NOTICE).

## What is under which license

| Material | License |
| --- | --- |
| This repository's own files, outside `patches/` and `upstreams/`, unless a file has an `SPDX-License-Identifier` line | GPL-2.0-or-later ([LICENSE](../LICENSE)) |
| SPDX MIT probes listed in [NOTICE](../NOTICE) | MIT |
| `tools/checks/fexbridge_avx_decision.cpp` | GPL-3.0-or-later |
| `patches/anyps5/` and a patched AnyPS5 build | GPL-2.0-only |
| `patches/madeira/` and the Madeira-derived iPad app | GPL-3.0-or-later |
| `patches/fex/` | GPL-3.0-or-later (upstream FEX code in the fork stays MIT; rpmalloc stays 0BSD) |
| `patches/wine/` and the pinned `madeira-lgpl` tree | LGPL-2.1-or-later |
| `upstreams/MoltenVK` | Apache-2.0 |
| StikDebug, StikJIT | not vendored; AGPL-3.0 and MPL-2.0 in their own repositories |

One GPL version cannot cover the whole checkout. AnyPS5 is GPL-2.0-only, so
`patches/anyps5/` cannot be placed under GPL-3.0. Madeira is
GPL-3.0-or-later, so `patches/madeira/` cannot be placed under GPL-2.0-only.
GPL-2.0-only and GPL-3.0 cannot be combined into one program.

GPL-2.0-or-later is the license for this project's own files because those
files are the scripts used to build both programs. GPL-2 treats the scripts
that compile and install a program as part of its complete source. A
downstream distributor can convey them under GPL-2.0 with a patched AnyPS5
binary, or under GPL-3.0 with the iPad app. GPL-3.0-or-later scripts would
not satisfy the source obligation for the GPL-2.0-only program.

## How the built programs combine

The iPad Mach-O is a derivative of Madeira. The build statically links:

- Wine's unix libraries from the `madeira-lgpl` pin (LGPL-2.1-or-later)
- FEX (MIT upstream code plus GPL-3.0-or-later Madeira modifications)
- MoltenVK (Apache-2.0)

Apache-2.0 and MIT can be included in a GPL-3.0 work. LGPL-2.1-or-later can
be linked with that work; the LGPL notices and the ability to relink the
Wine libraries stay in force. The Wine patches stay LGPL-2.1-or-later.
The iPad app is conveyed under GPL-3.0-or-later because Madeira is.

AnyPS5 stays a separate program. The relinker and the Windows HLE libraries
are GPL-2.0-only. Wine loads the HLE as Windows modules, which is the same
boundary this tree already builds. Keep AnyPS5 object code in that program,
and keep the two patch series as separate works. The binary that statically
links Apache-2.0 MoltenVK is the GPL-3.0 app.

Madeira's Converter Exception applies only to copyright the Madeira authors
hold. buberlo's patches stay outside that exception. The graphics path in
this repository uses MoltenVK. A distributor who turns Madeira's D3D
converter path back on has to satisfy Madeira's `LICENSE-EXCEPTION.md` and
Apple's terms for that library.

Madeira's `THIRD-PARTY-NOTICES.md` applies to Madeira's own distribution.
Read it before shipping a Madeira build. Submodule checkouts are not
relicensed by being pinned here.
