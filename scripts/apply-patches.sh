#!/usr/bin/env bash
# Apply or reverse the patch series in patches/<upstream>/.
# Each series is applied in lexical order inside the matching submodule.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Patch series touch these checkouts. rpmalloc is nested inside FEX.
git -C "$root" submodule update --init --depth 1 \
    upstreams/AnyPS5 upstreams/Madeira upstreams/FEX upstreams/wine
git -C "$root/upstreams/FEX" submodule update --init --depth 1 External/rpmalloc
mode="apply"
if [ "${1:-}" = "--reverse" ]; then
    mode="reverse"
    shift
fi

declare -A paths=(
    [anyps5]="$root/upstreams/AnyPS5"
    [madeira]="$root/upstreams/Madeira"
    [fex]="$root/upstreams/FEX"
    [wine]="$root/upstreams/wine"
    [moltenvk]="$root/upstreams/MoltenVK"
)

shopt -s nullglob
for name in "${!paths[@]}"; do
    dir="$root/patches/$name"
    [ -d "$dir" ] || continue
    files=("$dir"/*.patch)
    [ ${#files[@]} -gt 0 ] || continue
    repo="${paths[$name]}"
    if [ ! -d "$repo/.git" ] && [ ! -f "$repo/.git" ]; then
        echo "missing submodule: $repo" >&2
        exit 1
    fi
    if [ "$mode" = "reverse" ]; then
        mapfile -t ordered < <(printf '%s\n' "${files[@]}" | sort -r)
    else
        mapfile -t ordered < <(printf '%s\n' "${files[@]}" | sort)
    fi
    for patch in "${ordered[@]}"; do
        if [ "$mode" = "reverse" ]; then
            if git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
                git -C "$repo" apply --reverse "$patch"
                echo "reversed ${name}/$(basename "$patch")"
            else
                echo "skip reverse (not applied): ${name}/$(basename "$patch")"
            fi
        else
            if git -C "$repo" apply --check "$patch" 2>/dev/null; then
                git -C "$repo" apply "$patch"
                echo "applied ${name}/$(basename "$patch")"
            elif git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
                echo "already applied: ${name}/$(basename "$patch")"
            else
                echo "patch does not apply: $patch" >&2
                exit 1
            fi
        fi
    done
done
