"""Exercise offline image staging without pacman, root or a running systemd."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
DIGEST = "5b3cb1bc85ff8a1720e49a710da386b4807a4009456d1dc7f73bb54751f2bb79"
CHROOT = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args = sys.argv[2:]
with open(os.environ["LIVI_TEST_LOG"], "a") as f:
    f.write(json.dumps(args) + "\n")
if args[:2] == ["getent", "passwd"]:
    print(f"alarm:x:{os.getuid() or 1000}:{os.getgid()}:alarm:/home/alarm:/bin/bash")
if "--now" in args or "start" in args or "restart" in args:
    sys.exit("installer tried to start services")
if args[0] == "visudo" and os.environ.get("FAIL_VISUDO"):
    sys.exit(1)
'''


class LiviInstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.root = self.directory / "rootfs"
        (self.root / "usr").mkdir(parents=True)
        self.bin = self.directory / "bin"
        self.bin.mkdir()
        mock = self.bin / "chroot"
        mock.write_text(CHROOT)
        mock.chmod(0o755)
        self.log = self.directory / "calls.jsonl"
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        LIVI_TEST_LOG=str(self.log))
        self.asset = self.directory / "fixture.AppImage"
        self.asset.write_bytes(b"test payload; must never execute during staging\n")
        # Only the isolated test copy accepts this tiny fixture. The real
        # installer has no digest override and rejects it.
        self.source = self.directory / "source"
        shutil.copytree(REPO / "rootfs", self.source)
        self.installer = self.source / "install-livi.sh"
        self.installer.write_text(self.installer.read_text().replace(
            DIGEST, hashlib.sha256(self.asset.read_bytes()).hexdigest()))

    def run_install(self, installer=None, user="alarm", **env):
        return subprocess.run(["bash", str(installer or self.installer),
                               str(self.root), user, str(self.asset)],
                              env=dict(self.env, **env), capture_output=True,
                              text=True, timeout=15)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_real_pin_rejects_bad_asset_before_modifying_image(self):
        result = self.run_install(REPO / "rootfs/install-livi.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [["getent", "passwd", "alarm"]])
        self.assertEqual([p.name for p in self.root.iterdir()], ["usr"])

    def test_installs_offline_and_preserves_persistent_state_on_rerun(self):
        config = self.root / "home/alarm/.config/LIVI/config.json"
        config.parent.mkdir(parents=True)
        config.write_text('{"country":"ES","wifiPassword":"device-specific"}')
        pairing = self.root / "var/lib/bluetooth/test/info"
        pairing.parent.mkdir(parents=True)
        pairing.write_text("paired device")
        for _ in range(2):
            result = self.run_install()
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"wifiPassword":"device-specific"', config.read_text())
        self.assertEqual(pairing.read_text(), "paired device")
        asset = self.root / "home/alarm/LIVI/LIVI.AppImage"
        self.assertEqual(asset.read_bytes(), self.asset.read_bytes())
        self.assertTrue(asset.stat().st_mode & 0o111)
        unit = (self.root / "etc/systemd/system/livi-kiosk.service").read_text()
        self.assertNotIn("__", unit)
        self.assertIn("User=alarm", unit)
        self.assertIn("PAMName=livi-kiosk", unit)
        self.assertIn("Conflicts=getty@tty1.service", unit)
        dropin = (self.root / "etc/systemd/system/livi-kiosk.service.d/begonia.conf").read_text()
        self.assertIn(f"After=user@{os.getuid() or 1000}.service", dropin)
        self.assertIn("Environment=LIBSEAT_BACKEND=seatd", dropin)
        self.assertIn("ExecStopPost=\n", dropin)
        self.assertIn("Restart=on-failure\n", dropin)
        self.assertIn("StartLimitIntervalSec=0\n", dropin)
        self.assertIn("ExecStartPre=/usr/local/lib/livi/livi-bluetooth-ready.sh", dropin)
        wp_config = self.root / "home/alarm/.config/wireplumber/wireplumber.conf.d"
        self.assertEqual((wp_config / "99-livi-hfp.conf").read_text(),
                         'monitor.bluez.properties = {\n  bluez5.roles = [ a2dp_sink a2dp_source ]\n}\n')
        self.assertIn("hfp_hf", (wp_config / "99-z-begonia-hfp.conf").read_text())
        ap_dropin = (self.root / "etc/systemd/system/livi-wifi-ap.service.d/begonia.conf").read_text()
        self.assertIn("Conflicts=begonia-hotspot.service", ap_dropin)
        self.assertIn("After=begonia-wifi.service NetworkManager.service begonia-hotspot.service", ap_dropin)
        for station in ("wlan0", "wlan1"):
            self.assertIn(f"ExecStartPre=/usr/bin/nmcli device set {station} managed no", ap_dropin)
        self.assertTrue((self.root / "var/lib/systemd/linger/alarm").exists())
        sudoers = self.root / "etc/sudoers.d/99-LIVI-helper"
        self.assertEqual(sudoers.stat().st_mode & 0o777, 0o440)
        self.assertIn("/home/alarm/.config/LIVI/driver/livi-helperd", sudoers.read_text())
        self.assertFalse((self.root / "etc/systemd/system/getty@tty1.service.d/livi-autologin.conf").exists())
        calls = self.calls()
        packages = next(c for c in calls if c[0] == "pacman")
        self.assertTrue({"fuse3", "mesa", "libinput", "seatd", "hostapd", "avahi",
                         "pipewire", "pipewire-pulse", "wireplumber",
                         "libxrender", "vulkan-icd-loader", "v4l-utils"} <= set(packages))
        self.assertIn(["usermod", "-aG", "audio,video,render,input,seat", "alarm"], calls)
        self.assertIn(["systemctl", "enable", "seatd.service", "bluetooth.service",
                       "avahi-daemon.service", "livi-kiosk.service"], calls)
        self.assertIn(["systemctl", "mask", "getty@tty1.service"], calls)

    def test_invalid_sudoers_does_not_replace_existing_permissions(self):
        sudoers = self.root / "etc/sudoers.d/99-LIVI-helper"
        sudoers.parent.mkdir(parents=True)
        sudoers.write_text("previous validated rule")
        result = self.run_install(FAIL_VISUDO="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sudoers.read_text(), "previous validated rule")
        self.assertFalse(any(c[:2] == ["systemctl", "enable"] for c in self.calls()))

    def test_invalid_username_cannot_inject_configuration(self):
        result = self.run_install(user="alarm\nUser=root")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())


if __name__ == "__main__":
    unittest.main()
