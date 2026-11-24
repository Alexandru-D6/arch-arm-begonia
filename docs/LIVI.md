# LIVI headless on begonia

Workflow 02 installs LIVI **10.5.0** for `alarm` and enables its kiosk on tty1.
Here, headless means no desktop environment: LIVI drives the built-in display
and touch input through its compositor, DRM/Mesa, libinput and seatd. SSH remains
available. The release filename retains `console` for compatibility with the
existing flashing instructions.

Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI).

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
3. In LIVI, select the Wi-Fi interface, onboard Bluetooth controller, country
   and channel before enabling wireless Android Auto. Country, AP credentials,
   rotation and wireless enablement are not preseeded by this installer.
4. Stop/disable `begonia-hotspot.service` if you enabled the separate test AP;
   it must not compete with LIVI for AP control. See [Wi-Fi notes](WIFI.md) for
   the vendor driver's AP and station-interface constraints.
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

## Upstream files

`rootfs/livi/upstream/` contains unchanged files from LIVI v10.5.0:
`scripts/install/packages.txt`, the four corresponding `assets/linux/` templates
and touch filter, plus `LICENSE` and `NOTICE`. Their license is GPL-3.0-or-later
with the attribution terms in `NOTICE`. The kiosk's begonia customizations live
in a separate systemd drop-in; PAM follows the upstream installer's Arch branch.
See the [upstream headless installer](https://github.com/f-io/LIVI/blob/v10.5.0/scripts/install/headless/install.sh).
