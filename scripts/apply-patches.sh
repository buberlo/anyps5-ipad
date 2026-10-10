#!/usr/bin/env bash
# Apply or reverse the patch series in patches/<upstream>/.
# Each series is applied in lexical order inside the matching submodule.
# macOS bash is 3.2: no associative arrays and no mapfile.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="apply"
names=(anyps5 madeira fex wine moltenvk spirv-cross)
explicit_only=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --reverse) mode="reverse"; shift ;;
        --only)
            [ "$#" -ge 2 ] || { echo "--only needs a patch series name" >&2; exit 1; }
            names=("$2"); explicit_only=1; shift 2 ;;
        *) echo "Usage: $0 [--reverse] [--only SERIES]" >&2; exit 1 ;;
    esac
done

repo_for() {
    case "$1" in
        anyps5) printf '%s\n' "$root/upstreams/AnyPS5" ;;
        madeira) printf '%s\n' "$root/upstreams/Madeira" ;;
        fex) printf '%s\n' "$root/upstreams/FEX" ;;
        wine) printf '%s\n' "$root/upstreams/wine" ;;
        moltenvk) printf '%s\n' "$root/upstreams/MoltenVK" ;;
        spirv-cross) printf '%s\n' "$root/upstreams/MoltenVK/External/SPIRV-Cross" ;;
        *)
            echo "unknown patch series: $1" >&2
            exit 1
            ;;
    esac
}

# Initialize only the selected checkouts. rpmalloc is nested inside FEX.
paths=()
for name in "${names[@]}"; do
    # SPIRV-Cross is a fetched MoltenVK dependency, not a root submodule.
    # Never invoke fetchDependencies here: it force-checks out dependencies
    # and can discard unrelated local changes.
    if [ "$name" = spirv-cross ]; then continue; fi
    repo="$(repo_for "$name")"
    paths+=("${repo#"$root/"}")
done
if [ "${#paths[@]}" -gt 0 ]; then
    git -C "$root" submodule update --init --depth 1 "${paths[@]}"
fi
for name in "${names[@]}"; do
    if [ "$name" = fex ]; then
        git -C "$root/upstreams/FEX" submodule update --init --depth 1 External/rpmalloc
    fi
done

shopt -s nullglob
for name in "${names[@]}"; do
    if [ "$name" = spirv-cross ]; then
        repo="$(repo_for "$name")"
        if [ ! -d "$repo/.git" ] && [ ! -f "$repo/.git" ]; then
            if [ "$explicit_only" = 1 ]; then
                echo "Missing MoltenVK External/SPIRV-Cross; initialize pinned dependencies before applying this series" >&2
                exit 1
            fi
            echo "skip unavailable MoltenVK SPIRV-Cross dependency; apply --only spirv-cross after dependencies are initialized"
            continue
        fi
        expected="$(tr -d '[:space:]' < "$root/upstreams/MoltenVK/ExternalRevisions/SPIRV-Cross_repo_revision")"
        actual="$(git -C "$repo" rev-parse HEAD)"
        if [ "$actual" != "$expected" ]; then
            echo "SPIRV-Cross HEAD differs from MoltenVK dependency pin: $actual != $expected" >&2
            exit 1
        fi
    fi
    if [ "$name" = moltenvk ] && [ "${APS5_APPLY_MOLTENVK:-0}" != 1 ]; then
        echo "skip optional MoltenVK stability patches (APS5_APPLY_MOLTENVK=1 enables them)"
        continue
    fi
    dir="$root/patches/$name"
    [ -d "$dir" ] || continue
    files=("$dir"/*.patch)
    [ "${#files[@]}" -gt 0 ] || continue
    repo="$(repo_for "$name")"
    if [ ! -d "$repo/.git" ] && [ ! -f "$repo/.git" ]; then
        echo "missing submodule: $repo" >&2
        exit 1
    fi
    ordered=()
    if [ "$mode" = "reverse" ]; then
        while IFS= read -r patch; do
            ordered[${#ordered[@]}]="$patch"
        done < <(printf '%s\n' "${files[@]}" | sort -r)
    else
        while IFS= read -r patch; do
            ordered[${#ordered[@]}]="$patch"
        done < <(printf '%s\n' "${files[@]}" | sort)
    fi
    # Later patches can intentionally change lines introduced by an earlier
    # patch. Checking each earlier patch in isolation then reports a false
    # conflict on a fully patched checkout. Validate the complete reverse
    # series first, without touching the checkout.
    if [ "$mode" = "apply" ]; then
        if python3 "$root/scripts/check-patch-series.py" "$repo" "${ordered[@]}"; then
            echo "already applied: ${name} complete series"
            continue
        fi
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
