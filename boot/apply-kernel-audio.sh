#!/bin/bash
# Run from the pinned pmOS kernel source directory. Safe on restored CI caches.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test -f sound/soc/mediatek/Kconfig
while IFS= read -r name; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    patch_file="$script_dir/kernel-patches/$name"
    if patch --batch --silent --forward --dry-run -p1 < "$patch_file" >/dev/null 2>&1; then
        patch --batch --silent --forward -p1 < "$patch_file"
    elif patch --batch --silent --dry-run -R -p1 < "$patch_file" >/dev/null 2>&1; then
        echo "Already applied: $name"
    else
        echo "Cannot apply audio patch $name to this kernel" >&2
        exit 1
    fi
done < "$script_dir/kernel-patches/series"
cp -a "$script_dir/kernel-overlay/." .
