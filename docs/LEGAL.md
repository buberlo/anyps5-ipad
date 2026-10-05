# Legal and licensing

This is an interoperability research prototype. It does not contain, and
must not gain:

- PS5 or other game dumps, pkg files, or extracted assets
- firmware, BIOS, or system software images
- encryption keys, tokens, or decrypted executables
- proprietary SDK libraries

Users supply their own material and are responsible for having the right to
use it. AnyPS5's upstream README says the same thing about its own project.

## Upstream licenses

| Tree | Pin | License |
| --- | --- | --- |
| `upstreams/AnyPS5` | see [UPSTREAMS.md](UPSTREAMS.md) | GPL-2.0-only |
| `upstreams/Madeira` | same | GPL-3.0-or-later |
| `upstreams/FEX` | Madeira's gitlink | MIT |
| `upstreams/wine` | Madeira's `madeira-lgpl` gitlink | LGPL-2.1-or-later |
| `upstreams/MoltenVK` | Khronos `main` at the pin | Apache-2.0 |

Patches under `patches/<name>/` are derivative works of that upstream and
stay under its license. Do not merge AnyPS5's GPL-2.0-only sources into a
GPL-3.0 program. The two trees are submodules so they can be built and
patched separately. A Windows PE produced by AnyPS5 is data the user runs
under Madeira; producing it does not combine the two licenses into one
binary in this repository.

Madeira's `LICENSE-EXCEPTION.md` and `THIRD-PARTY-NOTICES.md` apply to
Madeira's own distribution. Read them before shipping a Madeira build.

## Files written in this repository

Scripts, docs, and the milestone notes are GPL-3.0-or-later so they can sit
next to Madeira. The capability tool and its loader test are MIT
(`SPDX-License-Identifier: MIT` in the sources). They do not link AnyPS5 or
Madeira; they talk to the Vulkan loader, or they compile one new Madeira C
file that has no Wine types in it.

Submodule checkouts are not relicensed by being pinned here.
