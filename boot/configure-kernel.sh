#!/bin/bash
# Run in the kernel source tree after installing the pinned pmOS .config.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bash "$script_dir/apply-kernel-audio.sh"
bash "$script_dir/apply-kernel-connectivity.sh"
# Start the panel at its existing supported maximum instead of 800.
dts=arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dts
sed -i 's/default-brightness = <800>;/default-brightness = <1879>;/' "$dts"
grep -q 'default-brightness = <1879>;' "$dts"
grep -q 'max-brightness = <1879>;' "$dts"

scripts/config --enable MEMCG_V1 --enable CPUSETS_V1 \
    --module BT --enable BT_BREDR --enable BT_LE --module BT_RFCOMM \
    --enable BT_RFCOMM_TTY --module BT_BEGONIA_STP \
    --module MTK_WMT_FWPORT --module MTK_WMT_DRV \
    --module MTK_WMT_FWPORT_BTIF --module MTK_WLAN_GEN4M \
    --enable CFG80211_WEXT \
    --enable POWER_RESET_MT6359 \
    --module NF_CT_NETLINK --module NF_NAT_FTP --module NF_NAT_TFTP \
    --enable SOUND --enable SND --enable SND_SOC \
    --module SND_SOC_MT6785_BEGONIA --module SND_SOC_BEGONIA_TFA9874 \
    --module I2C_GPIO \
    --module INPUT_REGULATOR_HAPTIC \
    --module MTK_ADSP_MBOX --module MTK_SCP \
    --enable USB_USBNET --module USB_RTL8152 --module USB_NET_AX88179_178A \
    --module USB_NET_SMSC95XX --module USB_SERIAL_OPTION \
    --module FUSE_FS \
    --module DM_VERITY --enable ZSWAP --disable LOCALVERSION_AUTO --set-str LOCALVERSION ""
make ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" olddefconfig
for driver in USB_RTL8152 USB_NET_AX88179_178A USB_NET_SMSC95XX; do
    grep -Fx "CONFIG_${driver}=m" .config
done
for driver in SND_SOC_MT6785_BEGONIA SND_SOC_MT6359 SND_SOC_MT6359_ACCDET SND_SOC_BEGONIA_TFA9874 I2C_GPIO; do
    grep -Fx "CONFIG_${driver}=m" .config
done
grep -Fx 'CONFIG_POWER_RESET_MT6359=y' .config
grep -Fx 'CONFIG_INPUT_REGULATOR_HAPTIC=m' .config
for driver in MTK_WMT_FWPORT MTK_WMT_DRV MTK_WMT_FWPORT_BTIF MTK_WLAN_GEN4M; do
    grep -Fx "CONFIG_${driver}=m" .config
done
grep -Fx 'CONFIG_CFG80211_WEXT=y' .config
grep -Fx 'CONFIG_WEXT_CORE=y' .config
for driver in BT BT_RFCOMM BT_BEGONIA_STP; do
    grep -Fx "CONFIG_${driver}=m" .config
done
grep -Fx 'CONFIG_BT_LE=y' .config
grep -Fx 'CONFIG_FUSE_FS=m' .config
# Keep servicing a boot-enabled watchdog while waiting for Power to start Arch.
grep -Fx 'CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y' .config
grep -Fx 'CONFIG_WATCHDOG_OPEN_TIMEOUT=0' .config
