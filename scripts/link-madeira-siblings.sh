#!/usr/bin/env bash
# Madeira's tree expects FEX/ and wine/ as nested submodules. This repo
# pins those same commits as sibling checkouts so they are not cloned twice.
# The links are local build glue. They are not committed.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
madeira="$root/upstreams/Madeira"

link() {
    local name="$1"
    local dest="$madeira/$name"
    if [ -L "$dest" ]; then
        ln -sfn "../$name" "$dest"
        return
    fi
    if [ -d "$dest" ]; then
        # An initialized nested submodule (or an empty gitlink dir) is in the way.
        if [ -n "$(ls -A "$dest" 2>/dev/null || true)" ]; then
            echo "$dest is a non-empty checkout; leaving it alone" >&2
            return
        fi
        rmdir "$dest"
    fi
    ln -s "../$name" "$dest"
    echo "linked $dest -> ../$name"
}

link FEX
link wine
