#!/bin/bash
# Stage LIVI's headless profile in an Arch ARM rootfs; never start host services.
set -euo pipefail
root=${1:?Usage: install-livi.sh ROOTFS [USER] [APPIMAGE]}
kiosk_user=${2:-alarm}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
upstream="$script_dir/livi/upstream"
version=10.5.0
digest=5b3cb1bc85ff8a1720e49a710da386b4807a4009456d1dc7f73bb54751f2bb79

[[ "$kiosk_user" =~ ^[a-z_][a-z0-9_-]*$ ]] || { echo 'Invalid kiosk user' >&2; exit 1; }
test -d "$root/usr"
root=$(realpath "$root")
account=$(chroot "$root" getent passwd "$kiosk_user")
IFS=: read -r _ _ uid gid _ user_home _ <<< "$account"
[[ "$uid" =~ ^[0-9]+$ && "$uid" != 0 && "$gid" =~ ^[0-9]+$ ]] || exit 1
# Paths are substituted into systemd and sudoers files without shell evaluation.
[[ "$user_home" =~ ^/home/[a-zA-Z0-9_-]+$ ]] || { echo 'Expected a home under /home' >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
appimage=${3:-$work/LIVI.AppImage}
if [ "$#" -lt 3 ]; then
    curl -fL --retry 3 "https://github.com/f-io/LIVI/releases/download/v$version/LIVI-$version-linux-arm64.AppImage" -o "$appimage"
fi
# Verify before installing packages or changing any target files.
printf '%s  %s\n' "$digest" "$appimage" | sha256sum -c -

# Use upstream's pacman mapping for both core and headless (lite) packages.
mapfile -t packages < <(awk -F '|' '$1 == "core" || $1 == "lite" { n=split($6,p," "); for(i=1;i<=n;i++) if(p[i]!="") print p[i] }' "$upstream/packages.txt" | sort -u)
chroot "$root" pacman -S --noconfirm --needed "${packages[@]}" ttf-dejavu sudo
# pacman hooks do not always run sysusers inside an image build chroot.
chroot "$root" systemd-sysusers
chroot "$root" usermod -aG audio,video,render,input,seat "$kiosk_user"

install -d -m755 -o "$uid" -g "$gid" "$root$user_home/LIVI"
install -m755 -o "$uid" -g "$gid" "$appimage" "$root$user_home/LIVI/LIVI.AppImage.new"
mv -f "$root$user_home/LIVI/LIVI.AppImage.new" "$root$user_home/LIVI/LIVI.AppImage"
# Do not replace config.json, identities or Bluetooth pairing state on reruns.
install -d -m700 -o "$uid" -g "$gid" "$root$user_home/.config/LIVI"
chown "$uid:$gid" "$root$user_home/.config"
install -Dm644 "$upstream/99-LIVI.rules.template" "$root/etc/udev/rules.d/99-LIVI.rules"
install -Dm755 "$upstream/livi-touch-filter" "$root/usr/local/lib/livi/livi-touch-filter"
install -Dm755 "$script_dir/livi/livi-power.sh" "$root/usr/local/lib/livi/livi-power.sh"
install -Dm644 "$script_dir/livi/livi-kiosk.pam" "$root/etc/pam.d/livi-kiosk"

sed -e "s|__USERNAME__|$kiosk_user|g" \
    -e "s|__HELPER__|$user_home/.config/LIVI/driver/livi-helperd|g" \
    "$upstream/99-LIVI-helper.sudoers.template" > "$work/sudoers"
printf '%s ALL=(root) NOPASSWD: /usr/local/lib/livi/livi-power.sh poweroff, /usr/local/lib/livi/livi-power.sh reboot\n' "$kiosk_user" >> "$work/sudoers"
install -Dm440 "$work/sudoers" "$root/etc/sudoers.d/99-LIVI-helper.tmp"
chroot "$root" visudo -cf /etc/sudoers.d/99-LIVI-helper.tmp
mv -f "$root/etc/sudoers.d/99-LIVI-helper.tmp" "$root/etc/sudoers.d/99-LIVI-helper"

sed -e "s|__USERNAME__|$kiosk_user|g" -e "s|__HOME__|$user_home|g" \
    -e "s|__APPIMAGE__|$user_home/LIVI/LIVI.AppImage|g" \
    -e 's|__SYSTEMCTL__|/usr/bin/systemctl|g' \
    "$upstream/livi-kiosk.service.template" > "$work/kiosk.service"
install -Dm644 "$work/kiosk.service" "$root/etc/systemd/system/livi-kiosk.service"
install -d "$root/etc/systemd/system/livi-kiosk.service.d"
cat > "$root/etc/systemd/system/livi-kiosk.service.d/begonia.conf" <<EOF
[Unit]
Wants=seatd.service user@$uid.service bluetooth.service avahi-daemon.service begonia-wifi.service begonia-bluetooth.service
After=user@$uid.service bluetooth.service avahi-daemon.service begonia-wifi.service begonia-bluetooth.service
StartLimitIntervalSec=0

[Service]
Environment=HOME=$user_home
Environment=XDG_RUNTIME_DIR=/run/user/$uid
Environment=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus
Environment=LIBSEAT_BACKEND=seatd
# tty1 belongs to the kiosk, including after a crash. Keep maintenance on SSH
# and other VTs rather than bringing the tty1 login prompt back automatically.
ExecStopPost=
Restart=on-failure
RestartSec=5
EOF
install -d "$root/etc/security/limits.d"
printf '%s - rtprio 30\n' "$kiosk_user" > "$root/etc/security/limits.d/99-livi-rtprio.conf"
install -d -m755 "$root/var/lib/systemd/linger"
touch "$root/var/lib/systemd/linger/$kiosk_user"
chroot "$root" systemctl --global enable pipewire.socket pipewire-pulse.socket wireplumber.service
chroot "$root" systemctl enable seatd.service bluetooth.service avahi-daemon.service livi-kiosk.service
chroot "$root" systemctl mask getty@tty1.service
chroot "$root" systemctl set-default multi-user.target

install -d "$root/usr/share/doc/livi"
install -m644 "$upstream/LICENSE" "$upstream/NOTICE" "$root/usr/share/doc/livi/"
printf 'LIVI %s\nSHA256 %s\nSource: https://github.com/f-io/LIVI/tree/v%s\n' "$version" "$digest" "$version" > "$root/usr/share/doc/livi/release.txt"
echo "LIVI $version installed for $kiosk_user; reboot to start the kiosk on tty1."
