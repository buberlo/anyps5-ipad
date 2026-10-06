#!/usr/bin/env bash
# Build a standalone native iPad GPU probe. Sign only when an explicit profile
# and identity are supplied. This does not install or launch any app.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
out="${APS5_IPAD_PROBE_BUILD:-$root/build/ipad-probe}"
app="$out/AnyPS5GPUProbe.app"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
if [ ! -f "$headers/vulkan/vulkan.h" ] && [ -z "${VULKAN_HEADERS:-}" ]; then
    # The standalone iOS CI initializes MoltenVK, not AnyPS5's nested headers.
    headers="$root/upstreams/MoltenVK/External/Vulkan-Headers/include"
fi
[ -f "$headers/vulkan/vulkan.h" ] || { echo "Missing real Vulkan headers: $headers" >&2; exit 1; }
: "${MOLTENVK_IOS_LIB:?Set MOLTENVK_IOS_LIB to the pinned iPhoneOS libMoltenVK.a}"
sdk="$(xcrun --sdk iphoneos --show-sdk-path)"
mkdir -p "$app"
# Remove only stale signing metadata from this script's dedicated output app.
rm -rf "$app/_CodeSignature"
rm -f "$app/embedded.mobileprovision"
for shader in bda_bytes bc_sample; do
    glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/$shader.comp" -o "$app/$shader.spv"
    spirv-val --target-env vulkan1.1 "$app/$shader.spv"
done
flags=(-arch arm64 -isysroot "$sdk" -miphoneos-version-min=17.0 -O2 -Wall -Wextra -I"$headers")
xcrun clang "${flags[@]}" -std=c11 -c "$root/tools/gpu-probe/gpu_probe.c" -o "$out/gpu_probe.o"
xcrun clang "${flags[@]}" -fobjc-arc -c "$root/tools/ipad-probe/main.m" -o "$out/main.o"
xcrun clang++ "${flags[@]}" "$out/main.o" "$out/gpu_probe.o" "$MOLTENVK_IOS_LIB" \
    -framework UIKit -framework Foundation -framework Metal -framework QuartzCore \
    -framework IOSurface -framework CoreGraphics -lc++ -lz -o "$app/AnyPS5GPUProbe"
python3 - "$app" "$root" "$MOLTENVK_IOS_LIB" <<'PY'
import hashlib, json, pathlib, plistlib, subprocess, sys
app, root, molten = map(pathlib.Path, sys.argv[1:])
with (app / "Info.plist").open("wb") as stream:
    plistlib.dump({"CFBundleIdentifier":"com.konradkern.anyps5ipad.probe",
        "CFBundleName":"AnyPS5 GPU Probe", "CFBundleDisplayName":"AnyPS5 GPU Probe",
        "CFBundleExecutable":"AnyPS5GPUProbe", "CFBundlePackageType":"APPL",
        "CFBundleVersion":"1", "CFBundleShortVersionString":"0.1",
        "MinimumOSVersion":"17.0", "UIDeviceFamily":[2], "LSRequiresIPhoneOS":True,
        "UIFileSharingEnabled":True, "LSSupportsOpeningDocumentsInPlace":True,
        "UIApplicationSceneManifest":{"UIApplicationSupportsMultipleScenes":False,
            "UISceneConfigurations":{"UIWindowSceneSessionRoleApplication":[{
                "UISceneConfigurationName":"GPU Probe", "UISceneDelegateClassName":"ProbeDelegate"}]}},
        "UILaunchScreen":{}, "UISupportedInterfaceOrientations":["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"]}, stream)
manifest = {"schema":1, "stage":"native_gpu", "commit":subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"], text=True).strip(),
    "source_dirty":bool(subprocess.check_output(["git","-C",str(root),"status","--porcelain","--ignore-submodules=all"], text=True).strip()),
    "source_sha256":{name:hashlib.sha256((root / name).read_bytes()).hexdigest() for name in (
        "tools/ipad-probe/main.m", "tools/gpu-probe/gpu_probe.c", "tools/gpu-probe/gpu_probe.h",
        "tools/gpu-probe/bda_bytes.comp", "tools/gpu-probe/bc_sample.comp", "scripts/build-ipad-probe.sh")},
    "moltenvk_sha256":hashlib.sha256(molten.read_bytes()).hexdigest(),
    "hash_scope":"before_codesign",
    "files_before_codesign":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in app.iterdir() if p.is_file() and p.name not in ("manifest.json", "embedded.mobileprovision")}}
(app / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
PY
if [ -n "${APS5_PROVISIONING_PROFILE:-}" ] || [ -n "${APS5_SIGNING_IDENTITY:-}" ]; then
    : "${APS5_PROVISIONING_PROFILE:?Supply both profile and signing identity}"
    : "${APS5_SIGNING_IDENTITY:?Supply both profile and signing identity}"
    security cms -D -i "$APS5_PROVISIONING_PROFILE" > "$out/profile.plist"
    python3 - "$out/profile.plist" "$out/entitlements.plist" <<'PY'
import datetime, fnmatch, plistlib, sys
with open(sys.argv[1], "rb") as f: p=plistlib.load(f)
assert p["ExpirationDate"] > datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None), "provisioning profile expired"
e=p["Entitlements"]
bundle="com.konradkern.anyps5ipad.probe"
prefix=p["ApplicationIdentifierPrefix"][0]
app_id=prefix+"."+bundle
assert fnmatch.fnmatchcase(app_id,e["application-identifier"]), "profile does not cover probe bundle ID"
out={"application-identifier":app_id,"com.apple.developer.team-identifier":e["com.apple.developer.team-identifier"],"get-task-allow":e.get("get-task-allow",False)}
with open(sys.argv[2], "wb") as f: plistlib.dump(out,f)
PY
    cp "$APS5_PROVISIONING_PROFILE" "$app/embedded.mobileprovision"
    codesign --force --sign "$APS5_SIGNING_IDENTITY" --entitlements "$out/entitlements.plist" "$app"
    codesign --verify --deep --strict "$app"
    echo "Built and signed native probe: $app"
else
    echo "Built unsigned native probe: $app"
fi
# Signing changes the executable. Record final bytes outside the sealed bundle.
python3 - "$app" "$out/artifact-manifest.json" <<'PY'
import hashlib, json, pathlib, subprocess, sys
app, output = map(pathlib.Path, sys.argv[1:])
signed = subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], capture_output=True).returncode == 0
files = {str(p.relative_to(app)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(app.rglob("*")) if p.is_file()}
output.write_text(json.dumps({"schema":1,"app":app.name,"codesign_verified":signed,"files":files},indent=2)+"\n")
PY
