# Charge without starting Arch

USB insertion wakes the MediaTek bootloader so that the phone can charge.
The bootloader already requests **power-off charging**, but the original pmOS
initramfs continued into Arch unconditionally.

On this handset, `/chosen/atag,boot` is a little-endian vendor structure:
`(3, 0x41000802, bootmode, 2)`. A normal Power-button start uses mode `0`;
the observed USB start uses mode `8`. Xiaomi's
[boot-mode definitions](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/begonia-p-oss/drivers/misc/mediatek/include/mt-plat/mtk_boot_common.h)
name modes `8` and `9` as normal and low-power off-charging.

`initramfs/charge-only.sh` honors those modes on `xiaomi,begonia`. It runs after
udev loads the initramfs drivers, before USB networking and before mounting
the Arch root filesystem:

- USB starts leave the screen off, with the existing MT6360 charger running.
- Press Power to show a battery card immediately for about three seconds.
  Detection runs every 50 ms; showing the card does not wait for release.
  The screen then goes dark without starting Arch. Another short press refreshes it.
- Holding Power until the recognition vibration and releasing it continues
  into Arch. The stage requires twenty consecutive key checks, about one second plus
  sensor/display processing.
- Removing USB while waiting powers the phone off through the working PWRHOLD
  shutdown handler, after USB presence has first been confirmed. Initial
  offline readings are ignored while Type-C detection settles; a later
  disconnect must persist for about two seconds.
- Normal Power-button starts, reboots and recovery skip this stage.

This is a small Linux charging environment, rather than an electrically off
processor. Arch, its services and its root filesystem stay inactive until
Power is pressed. Charging current and voltage limits are left at their
existing values. The card uses battery `capacity` if a Battery power supply
provides it. The current kernel has no fuel-gauge power supply, so it instead
shows five coarse voltage-based bars, measured battery voltage and **EST.**
Charging voltage depends on current, temperature and battery condition; these
bars are an approximate indication, not a calibrated state of charge. A
missing voltage reading displays an unknown level rather than a fabricated
percentage. Green bars and a small lightning symbol mean the charger reports
`Charging`.

The card uses a small static ARM64 fbdev renderer (`initramfs/charge-display.c`),
with no mounted rootfs, external fonts or graphical services. The backlight
runs at about 10% while the card is visible. Framebuffer or sensor failures do
not prevent the long-press boot path. The normal console and original
backlight settings are restored before continuing boot.

The kernel handles the boot-enabled watchdog while waiting. The build checks
`CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y` and `CONFIG_WATCHDOG_OPEN_TIMEOUT=0`.
If essential charger/input support is missing, the stage lets normal boot
continue so the phone remains accessible. `begonia.charge_only=0` on the kernel
command line also bypasses it for diagnosis.

## Build integration

`scripts/add-charge-only.py` inserts the stage into the known pmOS
`init_2nd.sh`, before `setup_usb_network`. It preserves the archive's other
files, firmware, ownership, device nodes and symlinks and can be applied again
without duplicating the hook. Unsupported initramfs layouts fail during image
construction. The builder needs an ARM64 C compiler with static libc and Linux
UAPI headers (`aarch64-linux-gnu-gcc` in CI); `BEGONIA_CHARGE_CC` can select a
compiler wrapper with a complete sysroot. Archive tests use a native compiler
for their isolated fixture. `refresh-initramfs.py` includes `mt6360-adc` and
its dependencies for voltage readings before the rootfs is mounted.

The workflow passes `--charge-only` to `scripts/repack-bootimg.py`, after
refreshing the matching kernel modules. The working kernel, DTB and PWRHOLD
fix do not need to change. The refreshed module tree also includes
`regulator-haptic`, so the existing pmOS `beebzzr` utility can send the
recognition pulse before mounting the rootfs. A failed vibration never
prevents starting Arch.

## Artifacts and recovery

The final image is in `release/charge-only-20261123/` (ignored by Git):

- `boot-begonia-charge-only-v3.img`, SHA-256
  `b6726714dcaff294df13b9efde889206ce4e9227859dc1ce6e9d1506b107e3b4`.
- `flash-charge-only-v3.py`, `SHA256SUMS.txt` and `host-tests-v3.txt`.

The first experimental image caused a repeating Redmi-logo cycle on USB.
The revised stage does not interpret an initial offline charger reading as
cable removal. Its regression tests cover delayed and unconfirmed USB
detection. The first image is retained only as a diagnostic artifact.

The pre-change 64 MiB boot partition is saved on the phone as
`/root/begonia-charge-only-before/boot.img`, SHA-256
`800e2c5df92a71bce1845ffcfe7d0ffb4f2d49c1afd4c9748d035f929cf326a5`.
It includes the working PWRHOLD fix. From a running system, restore it with:

```sh
sudo dd if=/root/begonia-charge-only-before/boot.img of=/dev/disk/by-partlabel/boot bs=1M conv=fsync
sudo systemctl reboot
```

From fastboot, the previous local
`release/poweroff-20261123/boot-begonia-poweroff.img` can also be flashed to
`boot`. Userdata does not need to be replaced.

## Host validation — 2026-11-23

All 25 host tests passed, including twelve charging-stage tests covering normal
boot/reboot/recovery, vendor-tag validation, key hold/release, USB removal,
missing devices, delayed USB detection, vibration failure, command-line bypass
and repeated archive injection. Boot
repacking verifies the resulting payloads and header. The installed image's
kernel, DTB and recovery DTBO match the tested PWRHOLD image byte for byte.

## Handset validation — 2026-11-23

| Check | Result |
| --- | --- |
| Intentional reboot | Boot mode `0` bypasses charging-only and starts Arch. |
| Cold USB insertion after power-off | User confirmed the screen stays dark until Power is held and released. |
| Charging-only dwell | The v2 log records entry at uptime 1.009 s and Power release at 76.750 s, before Arch startup. The charger changes from initial `Discharging, online=0` to `Charging, online=1` at 17.191 s. |
| Recognition vibration | Final v3 image includes the haptic module and a 120 ms pulse. User confirmed vibration and subsequent Arch startup, with the screen dark beforehand. |
| USB charging limits | MT6360 reports Charging, with the existing 500 mA input/charge limit and 4.35 V charge-voltage limit. These were not raised. |
| Services after startup | No failed system services; AUX and speaker playback devices still enumerate in the final v3 verification. |

Final boot ID: `49dceec7-9239-4a60-9991-2b79af88d182`. Its log records
charging-only entry at 1.094 s, Charging at 2.510 s, the recognition pulse at
21.270 s, and Power release / Arch startup at 21.822 s. The early haptic device
registered at 0.823 s. The original PWRHOLD handler remains registered.

## Charging card update — 2026-11-26

The update was installed on the connected handset from
`release/charging-indicators-20261126/`. Its boot image preserves the Tianma
image's kernel, DTB and recovery DTBO; only the ramdisk changes. The installer
checks the existing boot image against that baseline, backs up the full boot
partition and verifies the write. On the connected phone, run:

```sh
sudo bash /home/alarm/charging-indicators-20261126/install.sh
```

Reboot once after installation. Then power off with USB connected, press Power, and check that the card disappears after about three
seconds while Arch stays inactive. Holding Power until vibration and releasing
must still start Arch. The installer verified the boot write and started the
LED service. Physical confirmation of the card remains pending after restart. Rollback:

```sh
sudo bash /root/charging-indicators-before-20261126/restore.sh
```

All 36 host tests pass, including charging mode selection, short press/timeout,
voltage scaling, long press, key failure, delayed charger detection, USB
removal, repeated archive injection, LED transitions and offline LIVI staging.

### Charging-only LED and press detection correction

`release/charging-indicators-v2-20261126/` contains a second update based on the
first card image. It adds `leds-mt6360` and dependencies to the initramfs and
loads the driver before the charging loop. The loop owns only `white:indicator`
and applies the same online + Charging/Full policy as LIVI, then clears the LED
before boot handoff. Missing LED hardware does not block charging or startup.
The card is now drawn on the first detected press, instead of after release;
polling is 50 ms. The card timeout, long-hold threshold and disconnect debounce
remain about three, one and two seconds respectively. The LIVI service is
unchanged. Regression coverage includes display invocation while Power is
still held and LED changes in charging-only mode. All 37 host tests pass.
The second image has been copied to the handset. Install with:

```sh
sudo bash /home/alarm/charging-indicators-v2-20261126/install.sh
```

The installer verifies the first card image as its baseline, saves a full
backup in `/root/charging-indicators-v2-before-20261126/`, then writes and
verifies the new image. It does not change the working LIVI LED service.
Installation/physical validation of this second image is pending.
