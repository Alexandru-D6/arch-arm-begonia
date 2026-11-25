#!/bin/bash
# Select the physical panel's touch firmware for the 6.16 begonia DTB.
set -euo pipefail
root=${1:?Usage: install-touchscreen.sh ROOTFS csot|tianma [ARCHIVE]}
variant=${2:?Select csot or tianma}
case "$variant" in csot|tianma) ;; *) echo 'Invalid touchscreen variant' >&2; exit 1 ;; esac
commit=33aa9fe1db0079945fa272e42f04db732489319d
digest=88be19d00b39d7724b1b75c66b3c648c854f9d2e9ecac39c389345e53c7c957069a365e9f0c7731cfb3c03de1d052ab6dc63331ea51d957591a8922f332e9ffb
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
archive=${3:-$work/firmware.tar.gz}
if [ "$#" -lt 3 ]; then
    curl -fL --retry 3 "https://gitlab.postmarketos.org/mt6785-mainline/firmware/-/archive/$commit/firmware-$commit.tar.gz" -o "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha512sum -c -
tar -xzf "$archive" -C "$work" "firmware-$commit/novatek"
dir="$root/usr/lib/firmware/novatek"
install -d "$dir"
install -m644 "$work/firmware-$commit/novatek/nt36672a_begonia_"*.bin.zst "$dir/"
# The 6.16 DTB requests this exact filename for either panel. Both rootfs and
# initramfs must use the selected bytes; changing only one is insufficient.
install -m644 "$work/firmware-$commit/novatek/nt36672a_begonia_$variant.bin.zst" "$dir/nt36672a_begonia_csot.bin.zst"
install -d "$root/usr/share/begonia"
printf '%s\n' "$variant" > "$root/usr/share/begonia/touchscreen-variant"
