# LIVI headless on begonia

Workflow 02 installs LIVI **10.5.0** for `alarm` and enables its kiosk on tty1.
Here, headless means no desktop environment: LIVI drives the built-in display
and touch input through its compositor, DRM/Mesa, libinput and seatd. SSH remains
available. The release filename retains `console` for compatibility with the
existing flashing instructions.

Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI).

## Built-in GPS

Workflow 02 also installs the begonia GPS transport and NMEA adapter. LIVI uses
`/run/begonia-gps/nmea`; its GPS switch starts/stops the internal receiver.
The stock Android system, vendor and nvdata partitions supply the positioning
runtime and factory calibration through read-only mounts. See
[GPS setup, verification and rollback](GPS.md).

## Front charging indicator

`livi-charge-led.service` starts with the kiosk and checks MT6360 every two
seconds. The white front LED (`white:indicator`, brightness 1 of 12) stays on
when USB is online and status is `Charging` or `Full`. It turns off when USB
is removed, charging stops or the service stops. The two rear flash LEDs are
never used. The service follows kiosk stop/restart and is enabled by the
offline installer. It does not change charging current or voltage limits.
See [charging-only mode](CHARGE_ONLY.md) for the short-press battery card and
the pending handset installation.

## Image installation

[`rootfs/install-livi.sh`](../rootfs/install-livi.sh) runs after the base packages,
`alarm` account and begonia audio profile are installed. It downloads
[`LIVI-10.5.0-linux-arm64.AppImage`](https://github.com/f-io/LIVI/releases/tag/v10.5.0)
and verifies SHA-256 before changing the target:

```text
5b3cb1bc85ff8a1720e49a710da386b4807a4009456d1dc7f73bb54751f2bb79
```

The installer consumes the `core` and `lite` pacman columns of the vendored
upstream manifest. All mapped packages were present in the Arch Linux ARM
aarch64 core/extra repository metadata checked during integration. LIVI bundles
its GStreamer runtime; the host supplies the libraries listed in that manifest,
including Mesa, libva, libssh, FUSE3 and the audio/session services. The kernel
build explicitly enables the FUSE module.
The begonia installer additionally supplies `libxrender`, `vulkan-icd-loader`
and `v4l-utils`: the ARM64 bundle's FFmpeg and V4L2 plugins require these host
libraries even when using software H.264 decoding.

The account receives `audio,video,render,input,seat` access. The upstream kiosk
unit uses an Arch PAM session, with a begonia drop-in to order startup after
the user manager and connectivity services. Lingering and enabled PipeWire user
services supply audio without an SSH login. Services are only enabled during
image construction; the installer does not start them or invoke a live seat,
`loginctl`, NetworkManager or udev reloads in the build chroot.

The upstream udev rules, touch filter and privileged helper permission are
installed, together with a root-owned helper for LIVI's power menu. As upstream
requires, `alarm` can launch its extracted
`~/.config/LIVI/driver/livi-helperd` as root without a password. This is a trusted
maintenance/kiosk account, not a restricted application sandbox.

LIVI 10.5.0 disables WirePlumber's hands-free roles and registers its own HFP
profile with `Role=client`. On the tested BlueZ build that left no Hands-Free
UUID advertised, and the source phone connected only as an ordinary Bluetooth
audio device. The installer supplies a later user WirePlumber override enabling
the native `hfp_hf` role and waits for its advertisement before launching LIVI.
It also stages LIVI's own base role file, avoiding LIVI restarting WirePlumber
mid-startup. The UI can still start if the controller is unavailable.
PipeWire owns HFP/SCO in this configuration; LIVI logs that the profile/socket is
already owned. Projection was tested with this arrangement; calls remain untested.

No shell autologin is added. The tty1 getty is masked so the boot screen belongs
to LIVI, and the kiosk restarts after a failure. Quitting LIVI leaves tty1 without
a login prompt; SSH and other VTs remain available for maintenance.
Settings and identities remain in
`/home/alarm/.config/LIVI`, and BlueZ pairing state remains in `/var/lib/bluetooth`.
Rerunning the installer preserves these files. Reflashing userdata replaces them.

To apply to an existing Arch ARM installation from this checkout (after updating
packages with `sudo pacman -Syu` and installing the begonia audio/connectivity
profiles):

```bash
sudo bash rootfs/install-livi.sh / alarm
sudo reboot
```

For an offline AppImage source, pass its path as the third argument. The same
pinned checksum is mandatory; pacman still needs the required packages cached
or network access. For image builds, pass a mounted rootfs instead of `/`; an
x86 host needs the same aarch64 binfmt/QEMU setup as workflow 02.

## First boot and verification

1. Change the existing image's `alarm` maintenance password with `passwd`.
2. Confirm LIVI appears without logging into the console, and verify touch and
   sound. The existing begonia PipeWire profile provides AUX/speaker switching
   and the internal microphone.
3. In LIVI, select **`ap0`** for Wi-Fi and **`hci0`** for Bluetooth, then the
   operating country and 5 GHz channel (channel 36 tested) before enabling
   wireless Android Auto. `wlan0` is the station interface; attempting to use it
   for LIVI's AP failed with `Could not configure driver mode` on the handset.
   Country, AP credentials, rotation and wireless enablement are not preseeded
   by this installer.
4. Stop/disable `begonia-hotspot.service` if you enabled the separate test AP;
   it must not compete with LIVI for AP control. See [Wi-Fi notes](WIFI.md) for
   the vendor driver's AP and station-interface constraints. The installed
   `livi-wifi-ap.service.d/begonia.conf` also conflicts with that test service,
   waits for connectivity startup and makes `wlan0`/`wlan1` unmanaged before
   starting LIVI's AP. This prevents a saved 2.4 GHz station connection from
   moving the shared radio away from 5 GHz.
5. Pair the source phone and test wireless handover, projection, media, microphone,
   reconnect after a cold boot, and sustained video decoding.

Useful commands over SSH:

```bash
systemctl status livi-kiosk seatd begonia-wifi begonia-bluetooth bluetooth
journalctl -b -u livi-kiosk
systemctl --user status pipewire.socket pipewire-pulse.socket wireplumber
wpctl status
# Stop / restart the kiosk:
sudo systemctl stop livi-kiosk
sudo systemctl start livi-kiosk
# Restore console-only boot:
sudo systemctl disable --now livi-kiosk
sudo systemctl unmask getty@tty1
sudo systemctl start getty@tty1
```

Host checks cover image staging, checksum rejection, persistent-state preservation
and service setup. The ARM64 release asset's checksum was also verified directly.

### Handset boot test — 2026-11-24

Installed with this script on the connected begonia running
`6.16.4-begonia-wifi`, then rebooted through `systemctl reboot`. The boot ID
changed from `0229bc46-11e8-4abd-807a-70a04c619d91` to
`54329b2d-e9f0-4f24-885f-891ed4f6033b`.

| Check after reboot | Result |
|---|---|
| Automatic startup | `livi-kiosk` enabled, active/running at 13.68 seconds after boot, before the maintenance SSH login |
| Terminal replacement | `getty@tty1` masked and inactive; active VT is tty1 |
| UI | Core logged `UI started` and `LIVI connected`; compositor mapped `dev.f-io.livi` to the main screen |
| Display | DSI-1, 1080×2340 at 60 Hz; one UI surface scanned out directly |
| Renderer | Mali-G76 MC4 (Panfrost), Mesa 26.3.1 |
| Audio services | PipeWire, pipewire-pulse and WirePlumber active |
| Stability at inspection | `NRestarts=0`, `ExecMainStatus=0` |
| Configuration validation | `systemd-analyze verify` passed on the handset; 32 host tests passed |

Local evidence is in `release/livi-20261124/before-reboot.txt` and
`release/livi-20261124/after-reboot.txt` (ignored build/test artifacts).
The test used USB Internet sharing for dependency installation and restored the
phone's previous resolver and mirror configuration before reboot. The custom
kernel was preserved; the unrelated generic `linux-aarch64` update was skipped.

This verifies automatic kiosk startup on the existing handset installation.
A complete workflow image build remains untested. Wireless AP handover, SCO
calls, rotation and video performance are separate acceptance tests. The initial
core log reported no available video codecs; projection decoding still needs
investigation before claiming Android Auto playback works.

### Projection repair — 2026-11-24

The subsequent handset investigation found four independent blockers:

- The bundled `libgstlibav.so` could not load `libXrender.so.1` and
  `libvulkan.so.1`. Installing the additional packages above and rebuilding
  the user's GStreamer registry made `avdec_h264` load successfully. After
  restarting LIVI, its log changed to `output codecs h264=sw h265=sw`.
  A two-second 1280×720/30 fps H.264 sample decoded to I420 through the bundled
  `h264parse ! avdec_h264 ! fakesink` pipeline, reaching EOS with exit status 0.
  This small synthetic clip is a decoder smoke test, not a performance test.
- Wireless Android Auto was enabled, but LIVI selected `wlan0`. Its helper
  repeatedly failed to start hostapd while the old NetworkManager test hotspot
  occupied `ap0`. Stopping/disabling the test service and selecting `ap0`
  allowed LIVI's own hostapd/DHCP stack to start.
- NetworkManager then reconnected `wlan0` to a saved 2.4 GHz network, moving the
  AP off 5 GHz. Isolating both station interfaces restored channel 36; the
  new AP service drop-in makes that isolation repeatable on startup.
- The helper's client-only HFP registration did not advertise the Hands-Free
  service. Allowing WirePlumber to own that service restored UUID `0x111e`.
  After the source phone retried pairing, the user confirmed that Android Auto
  setup appeared and a wireless projection session started.

Verified on the handset: `hostapd_cli -i ap0 status` reported `ENABLED`,
5180 MHz/channel 36, and the Nothing Phone (3a) Pro completed WPA2 authentication
and received `10.10.0.41` from LIVI's DHCP server. The Android Auto Bluetooth
profile and TCP listener on port 5277 were active. At 21:34:31 UTC the source
phone connected to port 5277, completed TLS/protocol 6.1, negotiated a 1280×720
session with H.265 video and logged `Android Auto projects over wifi`. At
21:34:33 the decoder received its first frame and LIVI switched to Projection.
After installing the persistent configuration and restarting the kiosk, LIVI
automatically paged the saved phone, established a second wireless session at
21:37:53 and decoded 1280×720 I420 frames with `avdec_h265` into `waylandsink`.
Navigation updates also arrived; the kiosk remained active with no crash restarts.
Audio, calls, sustained video performance and cold-boot reconnection remain
unqualified. Handset diagnostic logs are in
`/home/alarm/livi-projection-diagnostics/`.

For an existing installation with a cached failed decoder scan, stop
`livi-kiosk`, move `~/.cache/gstreamer-1.0/registry*.bin` aside as the kiosk user,
then start it after installing the missing libraries. Preserve the AppImage,
LIVI settings and Bluetooth bonds. On the repaired handset the original settings
are backed up in `/root/livi-projection-backup/config.before.json`.

## Landscape orientation

The begonia boot image uses
`video=DSI-1:panel_orientation=right_side_up` to rotate LIVI by −90° into
landscape. LIVI's compositor reads the DRM panel orientation and applies the
same transform to the display and touchscreen. Workflow 02 includes the flag;
workflow 04 offers it as the default
extra command line. Changing this boot option takes effect after a reboot.

The handset's previous boot partition is saved as
`/root/livi-landscape-backup/boot-before-landscape.img`, with hashes and command
lines in the adjacent `change.json`.

Post-reboot verification on 2026-11-25: the active command line contains this
flag, the compositor reports transform `_270` and a 2340×1080 workspace, and
the kiosk is running with `NRestarts=0`. Wireless Android Auto reconnected and
decoded 1280×720 video into the landscape workspace. The saved `spi1.0` touch
calibration is near identity and remains mapped to DSI-1; it contains no extra
90° rotation. The later physical test revealed the digitizer issue below.

### Physical touchscreen firmware fix — 2026-11-25

The handset loaded CSOT touchscreen firmware even though its digitizer requires
Tianma firmware. It reported horizontally reversed coordinates in the upper
half of landscape in both LIVI and Android Auto. A temporary virtual input
filter corrected the symptom but created a duplicate device entry.

Replacing the firmware in **both the rootfs and boot initramfs** with the Tianma
blob resolved the issue on the physical device. After reboot, the user confirmed
that all four corners work in LIVI and Android Auto with the filter disabled.
Only the physical `spi1.0` touchscreen remains. The virtual filter, service and
kiosk dependency were retired; no extra coordinate correction is required.

The tested compressed Tianma blob has SHA-256
`5bd2ba075d80d45739e1fa0b2fca466c12120a9a000ed4c37839d513d4a09a2a`.
It is included in the checksum-pinned firmware archive already used by the
connectivity installer. `rootfs/install-touchscreen.sh ROOTFS csot|tianma`
selects the panel variant. The 6.16 DTB still requests the CSOT filename, so the
installer places the selected bytes under that filename and records the
selection in `/usr/share/begonia/touchscreen-variant`. The repacker's
`--touch-firmware` option applies the same bytes to the early-boot ramdisk.

Workflows 02 and 04 expose `touchscreen_variant`, defaulting to `tianma` for the
verified development handset; select `csot` for an actual CSOT panel. Kernel-only
updates must use the same selection as the installed rootfs. The DTB's panel
compatible string reflects its build configuration and does not identify the
physical panel automatically. See the [reference port's panel firmware notes](https://github.com/criollojoel10/begonia-pmos-tianma).

Handset originals are saved under `/root/begonia-touch-firmware-backup/`.
The verified replacement boot image and its unpacked components are in the
ignored `release/touch-20261125/` directory. Rebinding this driver live failed
with an IRQ mapping error, so firmware acceptance was performed after reboot.

### Wide Android Auto view

On the landscape handset, `projectionWidth=1300` and `projectionHeight=600`
match the panel's 13:6 aspect ratio. The previous 1280×720 view left 210 pixels
unused at each side. After restarting LIVI, the source phone negotiated the
1280×720 decoder tier with a 1280×590 active image and 65-pixel encoded margins
above and below. The compositor maps the active image across the full 2340×1080
workspace, keeping the existing decoder tier. Existing frame-rate, DPI and touch
calibration settings are preserved. These are per-device LIVI settings in
`~/.config/LIVI/config.json`; the previous handset configuration is backed up
as `config.before-wide-projection.json` in the same directory.

## Projection startup delay — 2026-11-25

The handset's captured boot showed a 41-second wait from the LIVI UI to
projection despite Auto Connect and Dedicated Interface already being enabled:

| Event (UTC) | Time |
| --- | --- |
| UI started | 17:47:28.742 |
| Paging held for AP readiness | 17:47:29.105 |
| Readiness wait ended; Bluetooth paging started | 17:47:58.561 |
| Bluetooth connected | 17:47:59.652 |
| Phone connected to the projection TCP listener | 17:48:08.057 |
| First decoded frame; Projection displayed | 17:48:09.998 |

The AP service reported `AP up` at boot +26.27 seconds, shortly after the UI
appeared. However, `iw dev ap0 info` omitted the operating channel and LIVI's
`--wifi-ap-status` returned `running false`. LIVI 10.5.0 requires the channel
in its nl80211 interface query before releasing Bluetooth paging, so it waited
the full 30-second readiness timeout. Bluetooth then succeeded on its first
attempt; its retry backoff did not cause this captured delay.

`begonia-wifi-get-channel.patch` adds the missing unified-wiphy `get_channel`
callback for AP/P2P GO interfaces. It reports the channel definition maintained
by cfg80211 after successful AP startup and channel-switch notifications,
under the wiphy lock. Stopped APs, unsupported interface types and MLO requests
return an error rather than a fabricated channel. This also lets LIVI read
the AP's selected channel width. No LIVI binary or Bluetooth retry changes
are needed.

The replacement `wlan_gen4m.ko` builds for the installed
`6.16.4-begonia-wifi` kernel; patch application/reversal and all 34 host tests
pass. The checksum-verified module and a backup installer are staged at
`/home/alarm/livi-startup-fix-20261125/`. The installer preserves the original module under
`/root/livi-startup-backup-20261125/`; its `restore.sh` restores the original
for the next reboot. Local baseline evidence and build artifacts are in
`release/livi-startup-20261125/` (ignored).

After installation and reboot on 2026-11-26, the module's SHA-256 matched
the built artifact (`44715953668671c53fc521b94926a3f5762eb54c92f440522f5eedd5cf2a5961`).
`iw dev ap0 info` now includes the operating channel and width, and
`--wifi-ap-status` reports `running true`. The captured boot no longer logs
`paging waits for the access point`:

| Event (UTC) | Time |
| --- | --- |
| UI started | 16:00:40.623 |
| Helper began Bluetooth paging | 16:00:42.622 |
| Bluetooth connected | 16:00:43.070 |
| Phone connected to the projection TCP listener | 16:00:47.256 |
| First decoded frame; Projection displayed | 16:00:48.575 |

UI-to-projection time fell from **41.256 seconds to 7.952 seconds** in these
two boot captures. Removing the readiness timeout explains most of that
33.304-second improvement; phone handover and decoder timing also varied.
Both kiosk and AP services were active with `NRestarts=0` at inspection.
The first paging-list delivery raced helper startup and was retried successfully
when its socket became available. This is one successful post-fix boot,
not a repeated cold-boot reliability test. Evidence is in `after.txt` in the
same local artifact directory.

## Arch boot scheduling — 2026-11-26

The measured pre-change boot took 2.385 seconds in the kernel and 11.076
seconds in systemd userspace (13.461 seconds total, before LIVI finishes
initializing its UI). `systemd-random-seed.service` waited 5.049 seconds for
the kernel entropy pool. Only then did the default service dependencies let
`begonia-wifi.service` start its 3.466-second radio initialization. Wi-Fi
delayed NetworkManager, Bluetooth, user sessions and the kiosk in turn.

The Wi-Fi unit now uses `DefaultDependencies=no`, with explicit dependencies
on module loading, root remounting and its executable/module/firmware mounts.
It can initialize the radio in parallel with early system services while
remaining ordered before NetworkManager and shutdown. The random-seed service
and entropy checks are unchanged. Existing downstream Bluetooth and kiosk
dependencies continue to wait for Wi-Fi startup to finish.

The unit passes `systemd-analyze verify` against the handset's actual
services. After installation and reboot, boot ID
`87dc058f-9d30-4336-bb4b-3bc596185ae3` showed:

| Measurement | Before | After |
| --- | --- | --- |
| Kernel + systemd userspace startup | 13.461 s | 9.352 s |
| Systemd userspace startup | 11.076 s | 6.845 s |
| Wi-Fi initialization starts (kernel monotonic clock) | 8.239 s | 3.121 s |
| Wi-Fi initialization finishes | 11.705 s | 7.381 s |
| Kiosk service starts | 13.447 s | 9.339 s |

Wi-Fi and random-seed initialization now start together. The measured total
improvement is **4.109 seconds (31%)** in these two boot captures, even though
the radio itself took longer on the second boot. These systemd measurements
do not include bootloader time or the kiosk's subsequent UI initialization.
Bluetooth, Wi-Fi, the AP and the kiosk were active with `NRestarts=0`.
Android Auto automatically reconnected: the UI started at 16:10:34.198 UTC
and displayed the first projection frame at 16:10:42.439 (8.241 seconds later).
This validates one reboot; repeated cold-start timing remains unmeasured.

The installer at `/home/alarm/arch-boot-fix-20261126/install.sh`
backs up the old unit and provides
`/root/arch-boot-backup-20261126/restore.sh` for rollback. Local artifacts and
the before/after timing captures are in `release/arch-boot-20261126/` (ignored).

## Upstream files

`rootfs/livi/upstream/` contains unchanged files from LIVI v10.5.0:
`scripts/install/packages.txt`, the four corresponding `assets/linux/` templates
and touch filter, plus `LICENSE` and `NOTICE`. Their license is GPL-3.0-or-later
with the attribution terms in `NOTICE`. The kiosk's begonia customizations live
in a separate systemd drop-in; PAM follows the upstream installer's Arch branch.
See the [upstream headless installer](https://github.com/f-io/LIVI/blob/v10.5.0/scripts/install/headless/install.sh).
