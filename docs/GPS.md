# Internal GPS in LIVI

Begonia's receiver is connected through MediaTek WMT/STP, rather than a
`ttyAMA` UART. LIVI 10.5.0 already parses NMEA and forwards valid positions to
Android Auto/CarPlay. This integration supplies that NMEA stream at
`/run/begonia-gps/nmea`.

The kernel build enables `CONFIG_MTK_BEGONIA_GPS=m`, including GPS function
power control in `wmt_drv`, and builds `gps_stp.ko`. `/dev/stpgps` carries the
**binary chip protocol**, and must not be selected directly in LIVI. The
begonia device-tree overlay supplies GPIO74 antenna control using the stock
Xiaomi pin assignments. The device remains root-only.

`begonia-gps.service` runs with the kiosk. Its supervisor checks LIVI's
`gpsEnabled` and `gpsDevice` settings: turning GPS off, selecting a different
receiver, or stopping the kiosk stops both GPS processes and releases the
receiver. Turning it on starts the stock `mnld` positioning engine and a small
GPS HAL client that writes a pseudo-terminal. This also allows LIVI to reopen
the stream after a service restart. The PTY is accessible to the kiosk group;
LIVI's baud setting is retained at 38400 and does not control a physical UART.

The engine and its firmware stay on the handset's existing Android **system**
and **vendor** partitions. Both partitions, and **nvdata** for factory GPS
calibration, are mounted read-only with journal replay disabled in the
service's private mount namespace. This has been tested with the retained
Android 11/MIUI runtime. Other Android runtimes are not qualified. Images with
those partitions erased or replaced may need their original runtime restored.
The installer does not download or redistribute proprietary GPS binaries.

The adapters provide the Android properties the engine needs and use its
legacy GPS HAL. Only the required device nodes enter the chroot; Android
binder devices are excluded because no Android service managers run on Arch.
Android inertial-sensor fusion is disabled. Cellular assistance and automatic
EPO downloads are not configured, so a first standalone fix needs a clear view
of the sky and may take longer than on Android. Cache and assistance state
remain under `/var/lib/begonia-gps`; stock calibration is never written.
The stock daemon may log failed connections to absent Android assistance,
logging and sensor services. These are separate from receiving NMEA.

## Installation

Workflow 02 builds the adapters using the runner's Android NDK and stages the
service after LIVI. Kernel-only workflow 04 includes the kernel GPS changes,
but does not install userspace files.

For an existing installation, first install a kernel/module set built with
the GPS changes. Build the userspace adapters on an x86-64 Linux host:

```bash
bash scripts/build-gps-runtime.sh /path/to/android-ndk /tmp/begonia-gps-adapters
```

Transfer the checkout's `rootfs/` directory and the built adapters to the
handset. Stop LIVI before editing its configuration, then run there:

```bash
sudo systemctl stop livi-kiosk
sudo bash rootfs/install-gps.sh / alarm /path/to/begonia-gps-adapters
sudo systemctl daemon-reload
sudo systemctl start livi-kiosk
```

The installer backs up existing settings to
`~/.config/LIVI/config.before-begonia-gps.json`, selects the internal receiver,
and preserves unrelated settings and a deliberate GPS-off setting. In that
case enable GPS in **Settings → General → GPS**. Offline rootfs installation
enables services without starting them.

## Verification and rollback

```bash
systemctl status begonia-gps livi-kiosk
journalctl -b -u begonia-gps
cat ~/.config/LIVI/state/gps.json
```

`receiver.connected=true` confirms live NMEA. A position additionally requires
a satellite fix: check `fixMode`, `fixQuality`, `satellitesUsed` and `fix`.
Connection alone does not prove navigation accuracy. LIVI's u-blox-specific
module identity/RF queries are not supported by this MediaTek receiver.

To stop using this integration, turn GPS off in LIVI or select another
receiver. To remove automatic startup:

```bash
sudo systemctl disable --now begonia-gps
```

The development handset's pre-change boot image and WMT module are backed up
under `/root/begonia-gps-before-20261126/`; its `restore.sh` restores both for
the next reboot. The original LIVI config backup is independent of that kernel
rollback. Local build, boot and acceptance artifacts live in the ignored
`release/gps-20261126/` directory.

### Handset acceptance — 2026-11-26

The final reboot, boot ID `c40236a6-4bee-4037-a017-c44db6301c8d`, started both
the kiosk and GPS service automatically. Both reported `active/running`,
`NRestarts=0` and `ExecMainStatus=0`. LIVI reported a connected receiver at
`/run/begonia-gps/nmea` with continuously updated NMEA data. Its live GPS switch
also stopped the stream and drove GPIO74 low, then restored the stream and
antenna power when enabled again. No kiosk restart was needed for that toggle.

**A satellite fix was not obtained during this test:** the final state still
reported no satellites and no position. The transport and lifecycle are
verified; satellite acquisition, position accuracy and forwarding a real fix
to Android Auto remain acceptance tests. Do not interpret the connected
indicator as a successful navigation test.

All 43 host tests passed, including a fake HAL driving the actual NMEA adapter
through a real PTY. Both Android adapter architectures and the GPS-enabled
WMT/GPS modules compiled successfully. `systemd-analyze verify` passed on the
handset. A complete workflow image build has not been run for this change.

## Sources

The binary transport ABI and GPIO assignments follow Xiaomi/MediaTek's GPL
[begonia kernel](https://github.com/LineageOS/android_kernel_xiaomi_mt6785/tree/lineage-23.0/drivers/misc/mediatek/connectivity/gps/gps_stp).
The HAL declarations are a reduced, adapted version of AOSP's Apache-2.0
`hardware.h` and `gps.h`; their license is included with the adapters.
The use of the vendor daemon and extended MediaTek callback table was also
cross-checked against the [BF7 Linux GPS integration](https://github.com/annel0/bf7-linux/tree/main/hybris/bf7-gps).
LIVI's NMEA settings and forwarding are described in its
[v10.5.0 GPS documentation](https://github.com/f-io/LIVI/blob/v10.5.0/README.md#gps).
