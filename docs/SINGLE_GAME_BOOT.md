# Dreaming Sarah direct launch

Madeira patch `0019-dreaming-sarah-direct-boot.patch` replaces the library and
setup screens with one startup transaction. A normal launch of
`com.buberlo.anyps5ipad` selects
`Documents/wine/drive_c/DreamingSarah-PPSA02929-01_000_000/game.exe`.
The private game package remains outside the app binary, Git and CI. Updating
this same bundle preserves its Documents container and stored device pairing.

## Automatic JIT and connection

The embedded `AnyPS5Tunnel.appex` uses the same local packet reflection as the
private Blur Windows host. This code is based on LocalDevVPN; its complete notice
is bundled as `legal/LOCALDEVVPN-NOTICE.txt`. The tunnel routes only the local
`10.7.0.1` peer, with local address `10.7.1.1`. Internet traffic retains its normal
route. The provider handles packets and status messages, never JIT or credentials.

If the process already has usable JIT, it proceeds to the existing pool and
Wine/FEX startup. Otherwise the app reuses or creates its own VPN profile,
connects it, asks the embedded provider for its interface name and index, and
verifies that the route to lockdownd uses that exact interface and local address.
A bounded TCP connect sends no protocol payload. A connected VPN status or an
accepting proxy on a different route does not satisfy readiness.

Madeira's existing separate classic JIT helper receives the already imported
pairing credential from the Keychain and the original Madeira debugger script.
This preserves the pinned runtime and the helper architecture also used by
[current Madeira](https://github.com/willfaust/Madeira/blob/main/docs/JIT.md).
It does not change the FEX, Wine, MoltenVK or StikJIT revisions.
The host waits for actual debugger readiness, allocates the tested JIT pool,
detaches, waits for the helper's final result, and disconnects its own tunnel
before starting Wine. Duplicate or obsolete callbacks cannot start another
process. Startup failures show a short error and a retry action; failures after
launch require a new app process because the pool is consumed once per run.

The first creation of this app's VPN profile requires iOS's system permission.
An already approved profile is reused on subsequent starts. A missing/stale
pairing record, disabled Developer Mode or incompatible signing remains a real
setup failure. These OS/device requirements cannot be replaced with a fake JIT
success. The normal configured-device path needs no LocalDevVPN app, shortcut,
StikDebug app picker, Madeira menu or game-selection URL.

## Runtime scope

The app retains touch/controller input, the SDL → HWND → Wine → CAMetalLayer →
MoltenVK display path, the verified arena settings, early debugger detach,
Vulkan inactivity gate, file logs and crash diagnostics. It forces the existing
Dreaming Sarah interpolation option. Steam, library scanning, onboarding,
configuration catalog, installation UI, shortcut resources and D3D12 converter
resources are excluded from the app's build. Upstream license notices remain.

The game profile requests 1280 × 720, AVX and the complete 18-control gamepad
layout with frame generation disabled, replacing any reduced shared demo layout at each start. A compact FPS
counter samples successful native Vulkan presents once per second, resets across
foreground changes, and does not use the guest’s nominal 30 FPS or the DXMT
Frame 0 HUD. This is a present-rate indicator, not proof of sustained displayed
FPS. The app name is “Dreaming Sarah”; its icon uses the user-supplied title/eye logo
adapted to the square icon canvas. The icon
is stored in `assets/DreamingSarah.appiconset` and staged by
`scripts/stage-single-game-assets.sh` during an iPhoneOS build.
Public builds default to `com.buberlo.anyps5ipad`. The existing private device
installation retains `com.konradkern.anyps5ipad` to preserve its game container and
Keychain; set `APS5_BUNDLE_IDENTIFIER` to override the build default. The previous library JSON is neither read nor rewritten. Existing
configuration, game files and saves remain in the same container.

## Verification

On a patched macOS checkout:

```sh
python3 tools/checks/test_single_game_boot.py
python3 tools/checks/test_embedded_tunnel_route.py
```

These compile the production Swift transaction and route probe, then exercise
permission/foreground ordering, failures, retries, stale callbacks, one launch,
exact-route rejection, real local TCP acceptance/refusal and zero payload bytes.
They do not prove physical-device JIT, frames or gameplay. The iPhoneOS CI runs
both tests alongside the complete unsigned app build, including both extensions.
A signed device build additionally requires packet-tunnel capability on the app
and its extension, plus the existing memory and development entitlements.
