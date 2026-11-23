#!/bin/bash
# Install the firmware and boot activation for the internal MT6785 WLAN.
set -euo pipefail
rootfs=${1:?Usage: install-connectivity.sh ROOTFS [FIRMWARE_ARCHIVE]}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
commit=33aa9fe1db0079945fa272e42f04db732489319d
digest=88be19d00b39d7724b1b75c66b3c648c854f9d2e9ecac39c389345e53c7c957069a365e9f0c7731cfb3c03de1d052ab6dc63331ea51d957591a8922f332e9ffb
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
archive=${2:-$work/firmware.tar.gz}
if [ ! -f "$archive" ]; then
    curl -fL --retry 3 "https://gitlab.postmarketos.org/mt6785-mainline/firmware/-/archive/$commit/firmware-$commit.tar.gz" -o "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha512sum -c -
tar -xzf "$archive" -C "$work" "firmware-$commit/mediatek"
# The vendor driver requests bare filenames, so these belong in firmware/.
install -Dm644 "$work/firmware-$commit/mediatek/"*.zst -t "$rootfs/usr/lib/firmware/"
install -Dm755 "$script_dir/connectivity/begonia-wifi" "$rootfs/usr/local/sbin/begonia-wifi"
install -Dm755 "$script_dir/connectivity/begonia-hotspot-test" "$rootfs/usr/local/sbin/begonia-hotspot-test"
install -Dm644 "$script_dir/connectivity/begonia-wifi.service" "$rootfs/usr/lib/systemd/system/begonia-wifi.service"
mkdir -p "$rootfs/etc/systemd/system/multi-user.target.wants"
ln -sfn /usr/lib/systemd/system/begonia-wifi.service "$rootfs/etc/systemd/system/multi-user.target.wants/begonia-wifi.service"
