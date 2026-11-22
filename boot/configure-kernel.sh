#!/bin/bash
# Run in the kernel source tree after installing the pinned pmOS .config.
set -euo pipefail
scripts/config --enable MEMCG_V1 --enable CPUSETS_V1 \
    --module NF_CT_NETLINK --module NF_NAT_FTP --module NF_NAT_TFTP \
    --module SND_SOC_MT6359 --module MTK_ADSP_MBOX --module MTK_SCP \
    --enable USB_USBNET --module USB_RTL8152 --module USB_NET_AX88179_178A \
    --module USB_NET_SMSC95XX --module USB_SERIAL_OPTION \
    --module DM_VERITY --enable ZSWAP --disable LOCALVERSION_AUTO --set-str LOCALVERSION ""
make ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" olddefconfig
for driver in USB_RTL8152 USB_NET_AX88179_178A USB_NET_SMSC95XX; do
    grep -Fx "CONFIG_${driver}=m" .config
done
