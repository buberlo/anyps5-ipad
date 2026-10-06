#!/usr/bin/env bash
# Copy this project's app icon into the patched upstream asset catalog.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source="$root/assets/DreamingSarah.appiconset"
destination="$root/upstreams/Madeira/app/Madeira/Assets.xcassets/DreamingSarah.appiconset"
test -f "$source/DreamingSarah-1024.png"
test -f "$source/Contents.json"
mkdir -p "$destination"
cp "$source/DreamingSarah-1024.png" "$source/Contents.json" "$destination/"
