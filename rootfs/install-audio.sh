#!/bin/bash
# Install files into an already populated Arch rootfs. Packages are installed
# by workflow 02; this helper also supports updating an existing handset (/).
set -euo pipefail
root=${1:?Usage: install-audio.sh ROOTFS [USER]}
audio_user=${2:-alarm}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test -d "$root/usr"
install -Dm644 "$script_dir/audio/ucm2/conf.d/begonia-aux/begonia-aux.conf" \
    "$root/usr/share/alsa/ucm2/conf.d/begonia-aux/begonia-aux.conf"
install -Dm644 "$script_dir/audio/ucm2/MediaTek/begonia-aux/HiFi.conf" \
    "$root/usr/share/alsa/ucm2/MediaTek/begonia-aux/HiFi.conf"
install -Dm644 "$script_dir/audio/wireplumber/51-begonia-aux.conf" \
    "$root/etc/wireplumber/wireplumber.conf.d/51-begonia-aux.conf"
install -Dm755 "$script_dir/../scripts/test-mic-aux.sh" \
    "$root/usr/local/bin/begonia-mic-aux-test"
# SSH sessions have no active local seat ACL; the kiosk account also needs
# access before LIVI is integrated. User services remain scoped to this user.
chroot "$root" usermod -aG audio "$audio_user"
chroot "$root" systemctl --global enable pipewire.socket pipewire-pulse.socket wireplumber.service
install -d -m755 "$root/var/lib/systemd/linger"
touch "$root/var/lib/systemd/linger/$audio_user"
