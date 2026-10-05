# Slim chain

The use case is one AnyPS5-relinked x86-64 PE whose GPU path is Vulkan,
running inside Madeira on an iPad: FEX (ARM64EC) → Wine → winevulkan →
MoltenVK → Metal. This note says what that path actually loads, what can
stay out of the build, and what was changed behind `APS5_SLIM=1`.

Numbers below were measured on this x86-64 Linux VM on 2026-10-05.
Nothing here was measured on an iPad. A title ELF was not relinked, so
the PRX set a real game will `DT_NEEDED` is not known.

## What the images import

The relinker writes one import DLL. `WindowsImportBuilder` always emits
`KERNEL32.dll` and these 22 symbols:

`ExitProcess`, `FormatMessageA`, `FreeLibrary`, `GetCommandLineW`,
`GetFileAttributesA`, `GetLastError`, `GetModuleFileNameA`,
`GetModuleHandleA`, `GetProcAddress`, `GetStdHandle`,
`GetSystemDirectoryA`, `LoadLibraryExA`, `LocalFree`, `RaiseException`,
`VirtualAlloc`, `WideCharToMultiByte`, `WriteFile`, `lstrcatA`,
`lstrcmpA`, `lstrcmpiA`, `lstrcpyA`, `lstrlenA`.

`objdump -p` on both synthetic PEs (Linux relinker and MinGW
`relinker.exe` under Wine) shows that list and no other DLL. There is
no `user32`, `d3d11`, `opengl32`, or `vulkan-1` import on the PE.
Vulkan is loaded later, from `libSceAgcDriver`, by name.

That import table is the startup stub, not the Win32 surface of the
HLE libraries. On Windows, `libc` / `libkernel` also call `VirtualAlloc2`,
`MapViewOfFile3`, thread and wait APIs, and `GetProcAddress` on `ntdll`.
A shim that implements only those 22 exports will load the stub and then
fail inside the first PRX.

Linux PRX `NEEDED` entries. Ubuntu GCC 13 did not link the Windows
PRX. WinLibs on `f1d3f06` linked `libc.prx` and `libSceAgcDriver.prx`;
that log did not print sizes or `NEEDED` (see [PATCHES.md](PATCHES.md)):

| File | Size | NEEDED |
| --- | --- | --- |
| `libc.prx` | 537 KB | `libstdc++.so.6`, `libm`, `libc` |
| `libkernel.prx` | 578 KB | `libc.prx`, `libstdc++.so.6`, `libc` |
| `libSceAgcDriver.prx` | 8,874,056 bytes before `APS5_SLIM` | `libkernel.prx`, `libc.prx`, `libstdc++`, `libm`, `libgcc_s`, `libc` |

`libSceAgcDriver` does not `NEEDED` a Vulkan library. It opens
`vulkan-1.dll` or `libvulkan.so.1` at runtime. Before `APS5_SLIM` that
open went through SDL, and SDL was linked statically (`libSDL2.a` is
3.2 MB; the PRX contained 3,396 `SDL_` symbols, 807,221 bytes of sized
text).

Wine's own ICD, built from the Madeira fork on Linux:

| DLL | Imports |
| --- | --- |
| `vulkan-1.dll` (101 KB) | `kernel32.dll`, `user32.dll` |
| `winevulkan.dll` (1.9 MB) | `advapi32.dll`, `kernel32.dll`, `ntdll.dll`, `ucrtbase.dll` |

`user32` is why a Vulkan-only app still cannot delete the windowing DLL.
`wined3d` and `d3d11` are not in this import list.

GNU `time -v` on `wine` running the synthetic PE (prefix already created,
no PRX, no Vulkan call): max RSS 14,328 KB, 0.69 s wall, exit 42. That
is the stub process. It is not a title, not FEX, and not the iPad.

## Keep

- Host tool: the relinker. It does not ship in the app. Its unused-NID
  filter and `--to-intel` pass are host work.
- Runtime PRX for a Vulkan title: `libc.prx`, `libkernel.prx`,
  `libSceAgcDriver.prx`, plus whatever the title's `DT_NEEDED` names.
  `libSceVideoOut` is the presentation/input module (SDL window, pad,
  keyboard, mouse today). `libSceSysmodule` is how titles pull other
  modules by id.
- FEX as `xtajit64.dll` (5.44 MB in Madeira's farm). AVX on the 128-bit
  path, SVE off. The Linux `Bin/FEX` (3.7 MB) and `FEXServer` (452 KB)
  are the qemu smoke tools, not the iPad binary.
- Wine: `ntdll`, `kernel32`, `kernelbase`, `user32`, `win32u`,
  `winevulkan`, `vulkan-1`, and the CRT pieces those import
  (`ucrtbase`, and the VC runtime Madeira already vendors).
- MoltenVK, with `VK_KHR_portability_enumeration` and, when advertised,
  `VK_KHR_portability_subset` (patch `anyps5/0002`).
- StikDebug/JIT and the extended-virtual-addressing entitlement. The
  448 GiB guest arena does not fit in a 63 GB map.

## Drop, or do not build

### D3D, DXMT, madeira-d3d12

AnyPS5 does not speak D3D. DXMT is not initialized in this repo (the
gitlink directory is empty). `madeira-d3d12` is 4.2 MB on disk and about
22k lines, and it is not built here.

Madeira's checked-in ARM64EC farm is 104 MB on disk, 167 files. These
graphics DLLs are in that farm and are not imported by the AnyPS5 PE or
by `vulkan-1.dll`:

| DLL | Size |
| --- | --- |
| `d3d11.dll` | 5.54 MB |
| `wined3d.dll` | 3.67 MB |
| `opengl32.dll` | 2.16 MB |
| `dxgi.dll` | 1.74 MB |
| `d3d10core.dll` | 1.41 MB |

About 14.5 MB. Leaving them out of a Vulkan-only farm is the right
default. Do not delete them from a tree that still launches D3D games.
`user32` on a device might still delay-load `wined3d` for a path this
VM cannot execute. That is a device check, not a reason to keep
building DXMT for this chain.

`scripts/m3-madeira-simulator.sh` does not run `build/dxmt-ios/build.sh`.
It compiles `scripts/slim-dxmt-stub.c` into `libdxmt_combined.a` so the
app link has the present-count, GPU-meter, vsync, fence, and
`madeira_d3d12_canary_*` symbols the Swift UI calls. Those functions
return empty results. `madeira_set_eco` stays in ntdll, not in this
stub.

On `dba67bd` the simulator job compiled this stub (`slim
libdxmt_combined.a`) after the iOS FEX archives linked.
`xcodebuild` then failed while parsing StikJIT's Swift 6.4
interface, before the app link. On `2e74b7d` rewriting `::` to `.`
still failed: Xcode 26.3 is Swift 6.2.4 and the framework was built
with Swift 6.4. The unsigned job now builds the Madeira app without
`MadeiraJITHelper`. On `15978d4` that compile succeeded and `ld`
stopped with `library 'wineserver' not found`. The follow-up compiles
`libntdll_unix.a`, `libwin32u_unix.a`, and `libwineserver.a` from the
pinned Wine fork, and the four FFmpeg archives from the tracked
7.1.1 tarball. `libmadeira_rppairing.a` is iOS 27 on-device pairing
for Built-in StikJIT. The slim app links a stub that refuses to pair.
That link has not been measured on a hosted runner yet.

`x86_64-vcruntime` is not built by any Madeira script.
`tools/fetch-vcruntime.md` tells the user to extract Microsoft's
`VC_redist.x64.exe`. The slim PE imports `kernel32` only, so the
simulator script creates that folder and does not download the
redistributable.

### Other Wine subsystems

Wine has about 730 `dlls/` directories. The slim host configure
(`APS5_SLIM=1 scripts/build-wine-vulkan-linux.sh`) keeps Vulkan and
turns off Win16, OpenGL, Wayland, FreeType, fontconfig, GnuTLS, udev,
usb, v4l2, SANE, OpenCL, FFmpeg, PCSC, Kerberos, GSSAPI, NetAPI, and
CAPI, on top of the script's existing `--without-x`, ALSA, Pulse,
GStreamer, DBus, SDL, CUPS, OSS, gphoto, and pcap. The resulting
`config.h` has `SONAME_LIBVULKAN` as `"libvulkan.so.1"` and leaves
`SONAME_LIBGL`, `SONAME_LIBFREETYPE`, `SONAME_LIBGNUTLS`, and
`HAVE_UDEV` undefined. `winevulkan.dll` and `vulkan-1.dll` came out the
same size as the non-slim tree (1,918,252 and 102,892 bytes). The
saving is the rest of a Wine build, not those two files.

Do not install Gecko or Mono. They are not in the farm.

`shell32.dll` is 10.5 MB and `windowscodecs.dll` is 7.5 MB. The stub PE
does not import them. Wine's own startup often does. They stay until a
device load trace says otherwise.

`build/wine-pe/build-ntdll.sh` passes `--enable-winegstreamer`. The
Vulkan path does not need it. Turning that off is a Madeira iOS
configure change and was not done here, because this VM cannot finish
the iOS unix link (`sync.c` calls Mach).

Madeira's docs still build `wineserver` and also say the app does not
use the desktop wineserver model. The archive stays until a device run
shows it is unused. The unsigned CI build compiles it from
`wine/server` plus the iOS replacements, because the clone has no
prebuilt `libwineserver.a`.

### FEX

`scripts/m1-build-fex-aarch64.sh` is already the slim configuration:
`BUILD_THUNKS=OFF`, `BUILD_FEXCONFIG=OFF`, no i386/WOW64 target.
`ThunkLibs/` is about 32k lines of Linux host thunks (GL, Vulkan, and
similar). The iPad path is Wine calling MoltenVK, not FEX thunks.
WOW64 (`docs/WOW64.md`, `xtajit.dll`) is for 32-bit Windows programs.
This PE is x86-64. Do not build it for this chain.

The Linux FEX binary still contains the x32 syscall tables. There is no
upstream switch that drops them, and they are not worth a fork patch at
3.7 MB stripped. `FEXServer` and a squashfs rootfs are the Linux
interpreter's problem. `xtajit64` does not mount a rootfs.

### MoltenVK

Build the ICD. Leave `MVK_BUILD_SHADER_CONVERTER_TOOL` off (it defaults
off). `MVK_EXCLUDE_SPIRV_TOOLS=ON` only drops debug SPIR-V disassembly.
`MVK_EXCLUDE_CEREAL=ON` disables pipeline-cache serialization; AnyPS5's
driver caches pipelines, so cereal stays until a profile says the cache
is unused. `MVK_USE_METAL_PRIVATE_API` stays off until the M3 capability
log says a hard feature needs it. Do not strip 8-bit storage, int64,
buffer-device address, or BC compression ahead of that log.

### AnyPS5 modules

`core/libs/prx` has 111 modules. 97 are stub-like (`Export.cpp` /
`Unimplemented.cpp`, about 720 KB of source). 14 have real bodies; the
large ones are `libSceAgcDriver`, `libc`, `libkernel`, `libSceFont`,
`libSceAgc`, and `libSceVideoOut`. The default CMake build marks every
PRX except `libc` `EXCLUDE_FROM_ALL`, so `scripts/m0-build-anyps5.sh`
does not compile the farm. A title only needs the PRX named from its
ELF. Audio (`libfmod`, `libSceAudioOut`, `libSceNgs2`), video decode,
Np, and dialogs stay out until that ELF says otherwise.

### SDL

SDL is not required to open the Vulkan ICD. `APS5_SLIM=1` loads
`vulkan-1.dll` / `libvulkan.so.1` with `LoadLibraryA` / `dlopen` and
does not link `SDL2-static` into `libSceAgcDriver`.

SDL is still required for the desktop stand-ins of video-out, pad, and
FMOD. On the iPad the surface should be Madeira's HWND and its
`CAMetalLayer`, which is what `vulkan_metal_ios.c` is drafted to do.
Replacing `libSceVideoOut`'s `SDL_Window` with that HWND is not done.
Until it is, a title that calls `sceVideoOut` still needs that module,
and that module still uses SDL.

Measured on the Linux `libSceAgcDriver.prx`, same tree, portability
strings present in the slim binary:

| Build | Size | `SDL_` symbols |
| --- | --- | --- |
| default | 8,874,056 | 3,396 |
| `APS5_SLIM=ON` | 6,741,112 | 0 |

The slim PRX references `dlopen` and contains `libvulkan.so.1`,
`VK_KHR_portability_enumeration`, and `VK_KHR_portability_subset`.
Tests and `libSceVideoOut` still link SDL. No `VkDevice` was created
from the slim library.

## Wine is the loader

Replacing Wine with a 22-function `KERNEL32` shim, or running the Linux
ELF under in-process FEX, does not match how this iOS port executes
code. Games run as ARM64EC PEs inside `xtajit64`. The guest arena, the
PRX loader (`LoadLibraryExA` / `GetProcAddress`), and SEH are Windows.
The Linux FEX interpreter additionally wants `FEXServer` and a rootfs,
which is the wrong shape for the iPad process. A custom loader would
have to reimplement ntdll's thread, TEB, and ARM64EC machinery. That is
a new project. It is not a configure flag.

## Minimal configuration

Host relink and the Vulkan driver, without SDL in `libSceAgcDriver`:

```sh
APS5_SLIM=1 scripts/m0-build-anyps5.sh
```

That passes `-DAPS5_SLIM=ON` and builds `libSceAgcDriver`. Unset, the
script keeps the SDL loader.

FEX for the qemu smoke (thunks and FEXConfig already off):

```sh
scripts/m1-build-fex-aarch64.sh
```

Wine Vulkan objects with the extra host libraries off:

```sh
APS5_SLIM=1 scripts/build-wine-vulkan-linux.sh
```

Output is `build/wine-linux-slim`. The default script still uses
`build/wine-linux`. The default tree now links `winevulkan.so` (see
[PATCHES.md](PATCHES.md)). The slim tree has not been rebuilt since
that link started working. A fresh default configure also passes
`--without-freetype` and `--without-fontconfig`, because the GitHub
`ubuntu-24.04` configure stopped without FreeType headers. The local
Makefile already existed, so the measured `.so` was not rebuilt with
those flags.

iOS farm, when a Mac is available: build `ntdll`, `kernel32`,
`kernelbase`, `user32`, `win32u`, `winevulkan`, `vulkan-1`, `xtajit64`,
and the CRT DLLs the imports above name. Do not build DXMT,
`madeira-d3d12`, `wined3d`, `opengl32`, or the WOW64 farm. Pass
`SKIP_DXMT=1` to `build/wine-i386/build.sh` if that farm is built at
all. Copy MoltenVK into the app and keep `MADEIRA_WITH_VULKAN=1`.

Runtime:

```sh
APS5_GUEST_ARENA_LAZY=1
APS5_GUEST_ARENA_SIZE=0x100000000
MADEIRA_FEX_AVX=1
```

## Risks

- A real ELF's `DT_NEEDED` can pull font, audio, or Np modules this
  list calls optional. The synthetic PE used `--skip-sce-module`.
- Dropping `shell32` or `wined3d` from the iOS farm can fail in Wine's
  own startup even though the PE does not import them.
- `APS5_SLIM` does not remove SDL from video-out or FMOD. A game that
  opens a window still links that code on the desktop build.
- GnuTLS is off in the slim Wine configure. PS5 HTTP/SSL HLE is
  AnyPS5 code (`libSceSsl`, `libSceHttp`), not Wine's schannel. Those
  PRX were not linked or run.
- Pipeline caches and MoltenVK cereal were not profiled.
- iPad RSS, IPA size, and a full Madeira build time were not measured.
