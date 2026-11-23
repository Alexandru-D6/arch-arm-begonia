# Begonia power-off investigation and fix

## Symptom and cause

After a short power-button press or `systemctl poweroff`, the original mainline
build appeared off but required an approximately 20-second power-button hold
before it could start again. The symptom occurred with and without USB power.

The previous boot journal recorded a short power-button press at uptime
207.662 seconds and reached `System Power Off` at 208.506 seconds. Services
stopped promptly. The original board DT advertised PSCI, and the kernel used
`psci_sys_poweroff()` / `PSCI SYSTEM_OFF` for final power-off.

Xiaomi's stock kernel takes an additional PMIC path:
`mt_power_off()` calls `rtc_bbpu_power_down()`, which eventually calls
`rtc_bbpu_pwrdown()` and `PMIC_POWER_HOLD(0)`. On MT6359 this clears bit 0 of
`PPCCTL0` at register `0x0a08`.

Sources:

- [Stock shutdown entry point](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/drivers/misc/mediatek/rtc/mtk_rtc_common.c)
- [Stock RTC/PMIC shutdown path](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/drivers/misc/mediatek/rtc/mt6358/mtk_rtc_hal.c)
- [MT6359 register definitions](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/drivers/misc/mediatek/include/mt-plat/mt6785/include/mach/upmu_hw.h)

## Hardware experiment — 2026-11-23

A temporary kernel module was loaded on the existing 6.16.4 kernel. Loading it
only read `PPCCTL0`, which was `0x1`. Its shutdown callback cleared PWRHOLD
after `device_shutdown()`, using `SYS_OFF_MODE_POWER_OFF_PREPARE` so that the
PWRAP regmap could sleep safely. The test did not replace the boot image.

| Test | Result |
| --- | --- |
| Terminal power-off with USB connected | Phone started automatically. The following boot reported `androidboot.bootreason=usb`. |
| Captured final shutdown | Ramoops contained the test callback's final PWRHOLD-clear message after device shutdown. The saved RAM log contains bit corruption, so it is supporting evidence rather than an exact byte-for-byte trace. |
| Short button power-off with USB disconnected | User confirmed normal startup with a 2–3 second power-button press after waiting 10 seconds. |

The controlled change to PWRHOLD resolved the long-press symptom. It does not
establish the exact internal firmware failure; it supplies the missing working
hardware power-off path directly.

## Implementation

`boot/kernel-overlay/drivers/power/reset/mt6359-poweroff.c` registers a
sleepable power-off handler. It clears only `PPCCTL0.RG_PWRHOLD`; additional
vendor RTC/oscillator changes were unnecessary for the successful experiment
and are not included. Reboot remains on the existing reboot path. If the
PMIC operation fails, the existing firmware power-off path remains available.

Patch `0004-mt6359-begonia-poweroff.patch` adds the MFD child, driver build
entries, binding compatibility and begonia's `power-controller` DT node.
`boot/configure-kernel.sh` enables `CONFIG_POWER_RESET_MT6359=y`, so future
images carry the handler without a userspace shutdown hook or manually loaded
module. The driver requires the explicit DT node and an MT6359 parent.

Validation of the installed image:

- Cross-compiled kernel and begonia DTB; no build warnings or errors.
- 13 existing host tests, shell syntax and kernel driver style checks passed.
- The patch applies and reverses on the pinned source and tolerates repeated
  application through the existing kernel-patch script.
- Repacking verified the boot payloads and header; installation verified the
  original partition against its backup and read back the new image checksum.
- The phone booted kernel `6.16.4 #4` and registered the MT6359 handler at
  uptime 0.242 seconds, without the temporary test module.
- No failed system services; ALSA still enumerates AUX and speaker playback.
- PipeWire enumerates AUX and internal speaker sinks and the internal mic;
  the internal speaker and mic are selected by default with AUX disconnected.
- After installation and reboot, the user repeated the unplugged short-button
  shutdown test and confirmed normal startup with a 2–3 second press.

The final confirmed boot ID is `ceefbb31-3ff6-42e6-a547-2d44205114f8`, running
`6.16.4 #4`; its built-in handler registered at uptime 0.234 seconds.

USB power can start the phone automatically after shutdown. This was observed
with the temporary fix and confirmed by the bootloader's `usb` boot reason.
The subsequent [charging-only stage](CHARGE_ONLY.md) now honors that bootloader
request and waits with the screen off until Power is held and released, rather
than starting Arch automatically.

## Artifacts and recovery

Local artifacts are under `release/poweroff-20261123/` (ignored by Git):

- `boot-begonia-poweroff.img`, SHA-256
  `bf4e50bd246877cf698a04f652e70879f56c054022b087ef9cdaca4ea04ea024`.
- `boot-before-poweroff.img`: complete original 64 MiB boot partition,
  SHA-256 `6f4dde55e0e3fce5d7ef508a26ca3a9c586d32a257a59e608d0bee9fa1a83847`.
- `pwrhold-usb-console.txt`, `build.log`, temporary module source/binary,
  `flash-poweroff.py` and `SHA256SUMS.txt`.

The phone also retains `/root/begonia-poweroff-before/boot.img` and the captured
trace. Only the boot partition was updated; existing audio modules and
userspace are retained. The new boot image inherits the working speaker v3
ramdisk and boot header, replacing the kernel and DTB.

To restore the original boot partition from a running system:

```sh
sudo dd if=/root/begonia-poweroff-before/boot.img of=/dev/disk/by-partlabel/boot bs=1M conv=fsync
sudo systemctl reboot
```

Alternatively, flash the saved `boot-before-poweroff.img` to `boot` from fastboot.
