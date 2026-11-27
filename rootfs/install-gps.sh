#!/bin/bash
# Offline staging only. Runtime libraries are read from the phone's stock partitions.
set -euo pipefail
root=${1:?Usage: install-gps.sh ROOTFS USER BUILT_ADAPTER_DIRECTORY}
kiosk_user=${2:?Usage: install-gps.sh ROOTFS USER BUILT_ADAPTER_DIRECTORY}
adapters=${3:?Usage: install-gps.sh ROOTFS USER BUILT_ADAPTER_DIRECTORY}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ "$kiosk_user" =~ ^[a-z_][a-z0-9_-]*$ ]]
root=$(realpath "$root")
test -d "$root/usr"
for binary in begonia-gps-nmea begonia-gps-properties32.so begonia-gps-properties64.so; do
    test -s "$adapters/$binary"
done
account=$(chroot "$root" getent passwd "$kiosk_user")
IFS=: read -r _ _ uid gid _ user_home _ <<< "$account"
[[ "$uid" =~ ^[0-9]+$ && "$uid" != 0 && "$gid" =~ ^[0-9]+$ ]]
[[ "$user_home" =~ ^/home/[a-zA-Z0-9_-]+$ ]]

# Validate JSON before staging. Change only the receiver selection; retain a
# deliberate GPS-off choice, and preserve every unrelated LIVI setting.
python3 - "$root$user_home/.config/LIVI/config.json" "$uid" "$gid" <<'PY'
import json, os, shutil, sys, tempfile
from pathlib import Path
path = Path(sys.argv[1])
config = json.loads(path.read_text()) if path.exists() else {}
if not isinstance(config, dict):
    raise ValueError("LIVI config.json must contain an object")
if path.exists():
    backup = path.with_name("config.before-begonia-gps.json")
    if not backup.exists():
        shutil.copy2(path, backup)
        os.chown(backup, int(sys.argv[2]), int(sys.argv[3]))
        os.chmod(backup, 0o600)
path.parent.mkdir(parents=True, exist_ok=True)
config["gpsDevice"] = "/run/begonia-gps/nmea"
config["gpsBaudRate"] = 38400  # PTY: no physical UART baud rate is involved.
config.setdefault("gpsEnabled", True)
fd, temporary = tempfile.mkstemp(prefix=".gps-config-", dir=path.parent)
try:
    with os.fdopen(fd, "w") as stream:
        json.dump(config, stream, indent=2)
        stream.write("\n")
    os.chown(temporary, int(sys.argv[2]), int(sys.argv[3]))
    os.replace(temporary, path)
finally:
    if os.path.exists(temporary):
        os.unlink(temporary)
PY

chroot "$root" pacman -S --noconfirm --needed python
destination=$root/usr/local/lib/begonia-gps
install -d -m755 "$destination"
install -m755 "$script_dir/gps/begonia-gps" "$adapters/begonia-gps-nmea" "$destination/"
install -m644 "$adapters/begonia-gps-properties32.so" "$adapters/begonia-gps-properties64.so" "$destination/"
install -Dm644 "$script_dir/gps/LICENSE.android-abi" "$root/usr/share/doc/begonia-gps/LICENSE.android-abi"
install -d -m755 "$root/etc/systemd/system"
sed -e "s|__GROUP__|$gid|g" -e "s|__HOME__|$user_home|g" \
    "$script_dir/gps/begonia-gps.service.template" > "$root/etc/systemd/system/begonia-gps.service"
chroot "$root" systemctl enable begonia-gps.service
echo "Internal GPS staged for $kiosk_user; restart LIVI after installing the GPS-enabled kernel."
