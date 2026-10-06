#!/usr/bin/env bash
# Compatibility entrypoint. This builds iPhoneOS, not an iOS Simulator app.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$root/scripts/m3-madeira-ios.sh" "$@"
