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

No shell autologin is added. LIVI owns tty1 while running; quitting it restores
the regular login prompt. Settings and identities remain in
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
```

Host checks cover image staging, checksum rejection, persistent-state preservation
and service setup. The ARM64 release asset's checksum was also verified directly.
A full image build and handset boot/projection test are still required; this
integration does not qualify wireless AP handover, SCO calls, rotation or video
performance.

## Upstream files

`rootfs/livi/upstream/` contains unchanged files from LIVI v10.5.0:
`scripts/install/packages.txt`, the four corresponding `assets/linux/` templates
and touch filter, plus `LICENSE` and `NOTICE`. Their license is GPL-3.0-or-later
with the attribution terms in `NOTICE`. The kiosk's begonia customizations live
in a separate systemd drop-in; PAM follows the upstream installer's Arch branch.
See the [upstream headless installer](https://github.com/f-io/LIVI/blob/v10.5.0/scripts/install/headless/install.sh).
