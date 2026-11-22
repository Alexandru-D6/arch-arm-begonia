#!/bin/bash
# Run in the kernel source tree after installing the pinned pmOS .config.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bash "$script_dir/apply-kernel-audio.sh"
# Start the panel at its existing supported maximum instead of 800.
dts=arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dts
sed -i 's/default-brightness = <800>;/default-brightness = <1879>;/' "$dts"
grep -q 'default-brightness = <1879>;' "$dts"
grep -q 'max-brightness = <1879>;' "$dts"

scripts/config --enable MEMCG_V1 --enable CPUSETS_V1 \
    --module NF_CT_NETLINK --module NF_NAT_FTP --module NF_NAT_TFTP \
    --enable SOUND --enable SND --enable SND_SOC \
    --module SND_SOC_MT6785_BEGONIA --module SND_SOC_BEGONIA_TFA9874 \
    --module I2C_GPIO \
    --module MTK_ADSP_MBOX --module MTK_SCP \
    --enable USB_USBNET --module USB_RTL8152 --module USB_NET_AX88179_178A \
    --module USB_NET_SMSC95XX --module USB_SERIAL_OPTION \
    --module DM_VERITY --enable ZSWAP --disable LOCALVERSION_AUTO --set-str LOCALVERSION ""
make ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" olddefconfig
for driver in USB_RTL8152 USB_NET_AX88179_178A USB_NET_SMSC95XX; do
    grep -Fx "CONFIG_${driver}=m" .config
done
for driver in SND_SOC_MT6785_BEGONIA SND_SOC_MT6359 SND_SOC_MT6359_ACCDET SND_SOC_BEGONIA_TFA9874 I2C_GPIO; do
    grep -Fx "CONFIG_${driver}=m" .config
done
