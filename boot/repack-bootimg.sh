#!/bin/bash
# Repack the pmOS boot image with a replacement initramfs.
set -euo pipefail
base=${1:-pmos-base/boot.img}
if [[ -z ${1:-} && ! -f "$base" ]]; then
    base=pmos-base/images/boot.img
fi
ramdisk=${2:-arch-initramfs.gz}
if [[ -z ${2:-} && ! -f "$ramdisk" ]]; then
    ramdisk=-
fi
exec python3 "$(dirname "$0")/../scripts/repack-bootimg.py" \
    "$base" "$ramdisk" - "${3:-boot-begonia-arch.img}"
