# Internal Wi-Fi on begonia

The physical phone passed a client connection test on 2026-11-23 after
installing the MediaTek WMT/BTIF/gen4m port. It is running
`6.16.4-begonia-wifi`, built from the existing Arch kernel tree with the
connectivity additions. The original kernel, audio, power-off and charging
changes were retained.

## Installed components

- `mtk-vendor-btif.ko`: control transport to the connectivity MCU.
- `wmt_drv.ko`: connectivity power, firmware and `/dev/wmtWifi` control.
- `wlan_gen4m.ko`: internal Wi-Fi driver, with matching `cfg80211` and its
  wireless extensions enabled through `CONFIG_CFG80211_WEXT=y`.
- Device-tree nodes for Wi-Fi, CONNSYS and BTIF, the fixed 4 MiB connectivity
  firmware reservation at `0xab000000`, and the separate Wi-Fi DMA pool.
- Seven connectivity firmware files in `/usr/lib/firmware/`, where the driver
  requests them by bare filename.
- `begonia-wifi.service`, enabled at boot before NetworkManager. It loads the
  three modules in order, writes `1` to `/dev/wmtWifi`, and checks for `wlan0`.

The kernel port is pinned to
[`3a1ea7694219eb2fd513f0e630f2a882eab5c86f`](https://gitlab.postmarketos.org/minorum/linux/-/tree/3a1ea7694219eb2fd513f0e630f2a882eab5c86f).
Firmware is pinned to
[`33aa9fe1db0079945fa272e42f04db732489319d`](https://gitlab.postmarketos.org/mt6785-mainline/firmware/-/tree/33aa9fe1db0079945fa272e42f04db732489319d).
Both download helpers verify the complete archive's SHA-512 before extracting.
The [reference project's bring-up notes](https://github.com/criollojoel10/begonia-pmos-tianma#build-wifibluetooth-kernel-6164-begonia-conn-wifi)
describe the firmware paths and initialization sequence.

`boot/configure-kernel.sh` applies the connectivity source and device-tree
patch, then enables and checks the required options. Workflow 02 also installs
the firmware and boot service using `rootfs/install-connectivity.sh` and checks
the three installed modules with `modinfo`. No network credentials are stored
in this repository.

## Physical phone results

| Check | Result |
| --- | --- |
| Boot after installation | `6.16.4-begonia-wifi`, USB SSH available |
| Automatic driver initialization | Service exited successfully; all three modules loaded |
| Onboard interface | `wlan0`, device `/sys/devices/platform/soc/18000000.wifi` |
| Scan | Networks found on both 2.4 and 5 GHz |
| Association | `Guest1`, WPA2, 2.4 GHz, channel 3 |
| DHCP | `192.168.1.129/24`, gateway `192.168.1.1`, DNS `192.168.1.51` |
| Internet ICMP, bound to `wlan0` | `1.1.1.1`: 10/10 replies, 0% loss; mean 50.538 ms |
| DNS | `archlinux.org` resolved to `209.126.35.79` |
| HTTPS, bound to `wlan0` | `https://archlinux.org/`: HTTP 200 |
| Gateway ICMP | `192.168.1.1` did not reply to 5 probes; Internet traffic still passed |
| Saved connection | NetworkManager profile `Guest1`, autoconnect enabled |
| System state | No failed system services; `begonia-aux` ALSA card present |

This verifies basic client networking. A 5 GHz association, access-point mode,
suspend/resume, sustained throughput and Bluetooth HCI operation were not tested.
BTIF is the shared control transport; installing it does not provide a BlueZ
Bluetooth controller.

Host validation also passed all 25 existing tests, shell syntax checks, clean
and repeated patch application, module dependency/symbol checks, and boot-image
payload/header verification. The refreshed initramfs contains only the new
release's early-boot module tree and preserves its original startup scripts and
touch firmware. The flashed boot image was compared against the partition
before reboot.

## Interrupt debug logging correction

The initial driver printed `mtk_axi_interrupt: N callbacks suppressed` every
few seconds. Its interrupt handler called `__ratelimit()` before `pr_debug()`,
even when that debug message was disabled. The rate limiter consequently
reported suppressed log calls at warning level. The count refers to logging
calls, not dropped network packets or skipped interrupt handling.

`begonia-wifi-irq-logging.patch` replaces that pair with
`pr_debug_ratelimited()`, which checks whether the debug message is enabled
before invoking the rate limiter. The connectivity installer applies this
patch after extracting the pinned driver source, including on repeated runs.
The interrupt processing itself is unchanged.

The corrected `wlan_gen4m.ko` was installed on 2026-11-23, with the previous
module saved as
`/home/alarm/wifi-install-backup/wlan_gen4m-before-irq-log-fix.ko`.
After reboot, the phone automatically reconnected to Guest1 at
`192.168.1.130/24`. A Wi-Fi-bound test received 30/30 ping replies with no loss
and mean latency 3.433 ms; HTTPS returned HTTP 200. The Wi-Fi interrupt counter
was advancing and the kernel log contained zero matching suppression messages
for the entire boot. The new module's SHA-256 is
`095824d11cef05e26ffcfb3e243bd8a95178ee8cdedd9bc4273f02b33ec935af`.

The local installation bundle includes this correction. Its boot image is
unchanged: the Wi-Fi module loads from the root filesystem after boot.
Detailed results are in `linux-kernel/wifi-irq-log-fix-results.tar.gz` and
the build output is in `linux-kernel/wifi-irq-logging-build.log`.

## Artifacts and recovery

Local artifacts (ignored by Git) are under `linux-kernel/`:

- `boot-before-wifi.img`: complete backup of the phone's previous boot partition.
- `boot-begonia-wifi.img`: installed boot image; SHA-256
  `c160d16471d2406d9a68442935f907b0bf457918b64c7fcab5adcbcb3c3b396e`.
- `begonia-wifi-install.tar.gz`: installed kernel, modules, firmware and service.
- `wifi-live-results.tar.gz`: live kernel, service and networking logs.
- `connectivity-build.log`: kernel build output.

The phone retains `/home/alarm/wifi-install-backup/boot-before-wifi.img`, a copy
of the old modules, its original kernel config, and the new boot/service logs.
The original `/usr/lib/modules/6.16.4` tree remains installed alongside
`/usr/lib/modules/6.16.4-begonia-wifi`.

To restore the old kernel from the running phone, disable the new boot service,
write the saved boot image and reboot:

```sh
sudo systemctl disable begonia-wifi.service
sudo dd if=/home/alarm/wifi-install-backup/boot-before-wifi.img \
    of=/dev/disk/by-partlabel/boot bs=4M conv=fsync
sudo reboot
```

If Arch cannot boot, enter fastboot and restore the local backup with
`fastboot flash boot linux-kernel/boot-before-wifi.img`, then `fastboot reboot`.
The backup boot partition's SHA-256 is
`17c7f449697a8010344bb8fb4872530b0c0086d829863dab55fd05f01c5abac4`.
