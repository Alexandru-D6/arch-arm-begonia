#!/bin/bash
# Add the pinned MT6785 WMT/BTIF/gen4m port to the 6.16.4 pmOS tree.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
commit=3a1ea7694219eb2fd513f0e630f2a882eab5c86f
digest=47ae3ae3d879fb1929ca5e74977688f9933c162324fab12f493a9f0fd813d7437eb8427fb4865b6513daca747b69874a5aa8d298989fc6409606399865e9a4fd
archive=${CONNECTIVITY_KERNEL_ARCHIVE:-$PWD/.begonia-connectivity-source.tar.gz}
test -f drivers/misc/Kconfig
if [ ! -f "$archive" ]; then
    curl -fL --retry 3 "https://gitlab.postmarketos.org/minorum/linux/-/archive/$commit/linux-3a1ea769.tar.gz" -o "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha512sum -c -
tar -xzf "$archive" --strip-components=1 "linux-$commit/drivers/misc/mediatek"
for name in begonia-connectivity.patch begonia-wifi-irq-logging.patch \
    begonia-wifi-mgmt-registration.patch begonia-wifi-regulatory.patch \
    begonia-wifi-channel-notify.patch begonia-wifi-sae-rsnxe.patch; do
    patch_file="$script_dir/kernel-patches/$name"
    if patch --batch --silent --forward --dry-run -p1 < "$patch_file" >/dev/null 2>&1; then
        patch --batch --silent --forward -p1 < "$patch_file"
    elif patch --batch --silent --dry-run -R -p1 < "$patch_file" >/dev/null 2>&1; then
        echo "Already applied: $name"
    else
        echo "Cannot apply kernel patch $name to this kernel" >&2
        exit 1
    fi
done
