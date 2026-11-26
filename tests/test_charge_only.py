"""Exercise the charging stage with fake sysfs, key presses and USB removal."""
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
HELPER = REPO / "initramfs/charge-only.sh"

MOCK = r'''#!/usr/bin/env python3
import os, sys
from pathlib import Path
root = Path(os.environ["FIXTURE"])
command = Path(sys.argv[0]).name
if command == "iskey":
    path = root / "keys-count"
    count = int(path.read_text()) if path.exists() else 0
    path.write_text(str(count + 1))
    sequence = os.environ.get("KEY_SEQUENCE", "1," + "0," * 20 + "1").split(",")
    sys.exit(int(sequence[min(count, len(sequence)-1)]))
if command == "sleep":
    path = root / "ticks"
    count = int(path.read_text()) + 1 if path.exists() else 1
    path.write_text(str(count))
    with (root / "backlight-history").open("a") as history:
        history.write((root / "sys/class/backlight/lm36274-backlight/brightness").read_text().strip() + "\n")
    with (root / "led-history").open("a") as history:
        history.write((root / "sys/class/leds/white:indicator/brightness").read_text())
    if count >= int(os.environ.get("REMOVE_USB_AT", "1000")):
        (root / "sys/class/power_supply/mt6360-chg.1.auto/online").write_text("0\n")
    elif count >= int(os.environ.get("CONNECT_USB_AT", "1000")):
        (root / "sys/class/power_supply/mt6360-chg.1.auto/online").write_text("1\n")
if command == "poweroff":
    (root / "poweroff-called").write_text(" ".join(sys.argv[1:]))
if command == "beebzzr":
    (root / "vibration-called").write_text(" ".join(sys.argv[1:]))
    sys.exit(int(os.environ.get("VIBRATION_FAIL", "0")))
'''


class ChargeOnlyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sys = self.root / "sys"
        self.dt = self.sys / "firmware/devicetree/base"
        (self.dt / "chosen").mkdir(parents=True)
        (self.dt / "compatible").write_bytes(b"xiaomi,begonia\0mediatek,mt6785\0")
        self.tag = self.dt / "chosen/atag,boot"
        self.tag.write_bytes(struct.pack("<4I", 3, 0x41000802, 8, 2))
        self.cmdline = self.root / "cmdline"
        self.cmdline.write_text("androidboot.bootreason=usb\n")
        keys = self.sys / "class/input/event1/device/name"
        keys.parent.mkdir(parents=True)
        keys.write_text("mtk-pmic-keys\n")
        self.charger = self.sys / "class/power_supply/mt6360-chg.1.auto"
        self.charger.mkdir(parents=True)
        (self.charger / "online").write_text("1\n")
        (self.charger / "status").write_text("Charging\n")
        self.led = self.sys / "class/leds/white:indicator"
        self.led.mkdir(parents=True)
        (self.led / "brightness").write_text("0\n")
        (self.led / "trigger").write_text("none\n")
        self.backlight = self.sys / "class/backlight/lm36274-backlight"
        self.backlight.mkdir(parents=True)
        (self.backlight / "brightness").write_text("1879\n")
        (self.backlight / "bl_power").write_text("0\n")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for name in ("iskey", "sleep", "poweroff", "beebzzr", "modprobe"):
            path = self.bin / name
            path.write_text(MOCK)
            path.chmod(0o755)
        self.display = self.bin / "display"
        self.display.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$FIXTURE/display-calls"\ncat "$FIXTURE/keys-count" >> "$FIXTURE/display-keys-count"\nprintf "\\n" >> "$FIXTURE/display-keys-count"\n')
        self.display.chmod(0o755)
        self.log = self.root / "kmsg"
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        FIXTURE=str(self.root), BEGONIA_CHARGE_DISPLAY=str(self.display), BEGONIA_CHARGE_SYSFS=str(self.sys),
                        BEGONIA_CHARGE_CMDLINE=str(self.cmdline),
                        BEGONIA_CHARGE_KMSG=str(self.log))

    def run_helper(self, *args, **env):
        return subprocess.run(["sh", str(HELPER), *args], text=True,
                              capture_output=True, env=dict(self.env, **env), timeout=15)

    def test_short_press_shows_voltage_estimate_then_goes_dark_without_boot(self):
        adc = self.sys / "bus/iio/devices/iio:device7"
        adc.mkdir(parents=True)
        for suffix, value in (("label", "vbat"), ("raw", "3331"), ("scale", "1250"), ("offset", "0")):
            (adc / f"in_voltage4_{suffix}").write_text(value + "\n")
        result = self.run_helper(KEY_SEQUENCE="0,1", REMOVE_USB_AT="70")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "display-keys-count").read_text().splitlines()[0], "1")
        self.assertIn("-1 4163 1", (self.root / "display-calls").read_text())
        levels = (self.root / "backlight-history").read_text().splitlines()
        self.assertEqual(levels[0], "188")
        self.assertIn("188", levels)
        self.assertEqual(levels[60], "0")
        self.assertTrue((self.root / "poweroff-called").exists())
        self.assertNotIn("starting Arch", self.log.read_text())
        self.assertFalse((self.root / "vibration-called").exists())

    def test_charging_only_led_tracks_online_status_and_clears_on_exit(self):
        for status, expected in (("Charging", "1"), ("Full", "1"), ("Not charging", "0")):
            with self.subTest(status=status):
                (self.charger / "online").write_text("1\n")
                (self.charger / "status").write_text(status + "\n")
                for name in ("ticks", "led-history", "keys-count"):
                    (self.root / name).unlink(missing_ok=True)
                result = self.run_helper(KEY_SEQUENCE="1", REMOVE_USB_AT="2")
                self.assertEqual(result.returncode, 0, result.stderr)
                history = (self.root / "led-history").read_text().splitlines()
                self.assertEqual(history[0], expected)
                self.assertEqual(history[-1], "0")
                self.assertEqual((self.led / "brightness").read_text(), "0\n")

    def test_normal_power_reboot_and_recovery_bypass_even_with_usb_reason(self):
        for mode in (0, 2, 3, 7):
            with self.subTest(mode=mode):
                self.tag.write_bytes(struct.pack("<4I", 3, 0x41000802, mode, 2))
                self.assertEqual(self.run_helper().returncode, 0)
                self.assertFalse(self.log.exists())
                self.assertFalse((self.root / "keys-count").exists())

    def test_charger_modes_detect_without_side_effects(self):
        for mode in (8, 9):
            self.tag.write_bytes(struct.pack("<4I", 3, 0x41000802, mode, 2))
            self.assertEqual(self.run_helper("--check").returncode, 10)
            self.assertFalse(self.log.exists())
            self.assertEqual((self.backlight / "brightness").read_text(), "1879\n")

    def test_invalid_tags_and_other_board_bypass(self):
        for tag in (b"", b"1234", struct.pack("<3I", 3, 0, 8),
                    struct.pack("<3I", 0, 0x41000802, 8)):
            self.tag.write_bytes(tag)
            self.assertEqual(self.run_helper().returncode, 0)
            self.assertFalse(self.log.exists())
        self.tag.write_bytes(struct.pack("<4I", 3, 0x41000802, 8, 2))
        (self.dt / "compatible").write_bytes(b"other,phone\0")
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_explicit_cmdline_bypass(self):
        self.cmdline.write_text("quiet begonia.charge_only=0 rd.info\n")
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_held_power_then_release_resumes_and_restores_backlight(self):
        result = self.run_helper(KEY_SEQUENCE="0,1," + "0," * 21 + "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        log = self.log.read_text()
        self.assertIn("entered charging-only", log)
        self.assertIn("status=Charging, online=1", log)
        self.assertIn("Power released; starting Arch", log)
        self.assertEqual((self.backlight / "brightness").read_text(), "1879\n")
        self.assertEqual((self.backlight / "bl_power").read_text(), "0\n")
        self.assertEqual((self.root / "keys-count").read_text(), "24")
        self.assertFalse((self.root / "poweroff-called").exists())
        self.assertEqual((self.root / "vibration-called").read_text(), "-s 40000 -d 120")

    def test_usb_removal_powers_off_without_booting(self):
        result = self.run_helper(KEY_SEQUENCE="1", REMOVE_USB_AT="2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "poweroff-called").read_text(), "-f")
        self.assertIn("USB removed; powering off", self.log.read_text())
        self.assertNotIn("Power released; starting Arch", self.log.read_text())
        self.assertFalse((self.root / "vibration-called").exists())

    def test_failed_vibration_still_allows_power_button_start(self):
        result = self.run_helper(VIBRATION_FAIL="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("vibration unavailable; continuing", self.log.read_text())
        self.assertIn("Power released; starting Arch", self.log.read_text())

    def test_initial_offline_reading_cannot_cause_a_usb_reboot_loop(self):
        (self.charger / "online").write_text("0\n")
        result = self.run_helper(KEY_SEQUENCE=",".join(["1"] * 16 + ["0"] * 20 + ["1"]),
                                 CONNECT_USB_AT="12")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "poweroff-called").exists())
        self.assertIn("online=0", self.log.read_text())
        self.assertIn("online=1", self.log.read_text())
        self.assertIn("Power released; starting Arch", self.log.read_text())

    def test_initial_offline_can_still_be_started_with_power(self):
        (self.charger / "online").write_text("0\n")
        result = self.run_helper(KEY_SEQUENCE=",".join(["1"] * 12 + ["0"] * 20 + ["1"]))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "poweroff-called").exists())
        self.assertIn("Power released; starting Arch", self.log.read_text())

    def test_failed_key_reader_does_not_strand_the_phone(self):
        result = self.run_helper(KEY_SEQUENCE="2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("power-key check failed", self.log.read_text())
        self.assertEqual((self.backlight / "brightness").read_text(), "1879\n")

    def test_missing_charger_or_input_does_not_strand_the_phone(self):
        (self.charger / "online").unlink()
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertIn("charger unavailable", self.log.read_text())
        (self.charger / "online").write_text("1\n")
        (self.sys / "class/input/event1/device/name").unlink()
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertIn("power key unavailable", self.log.read_text())


@unittest.skipUnless(shutil.which("bsdtar"), "libarchive bsdtar is required")
class ChargeOnlyArchiveTest(unittest.TestCase):
    def test_archive_preserves_files_and_runs_before_network_and_root(self):
        spec = importlib.util.spec_from_file_location("charge_archive", REPO / "scripts/add-charge-only.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "input"
            (stage / "usr/bin").mkdir(parents=True)
            (stage / "usr/bin/iskey").write_text("test iskey")
            (stage / "usr/bin/busybox").write_text("test busybox")
            (stage / "usr/bin/beebzzr").write_text("test beebzzr")
            (stage / "bin").symlink_to("usr/bin")
            init = "#!/bin/busybox ash\nsetup_udev\n\nsetup_usb_network\nmount_root_partition\n"
            (stage / "init_2nd.sh").write_text(init)
            source = root / "source.gz"
            subprocess.run(["bsdtar", "--format=newc", "-czf", source, "-C", stage, "."], check=True)
            previous = source
            for number in range(2):
                work = root / f"work{number}"
                work.mkdir()
                output = root / f"out{number}.gz"
                with patch.dict(os.environ, BEGONIA_CHARGE_CC="cc"):
                    module.add_charge_only(previous, output, work)
                contents = subprocess.check_output(["bsdtar", "-xOf", output, "init_2nd.sh"], text=True)
                self.assertEqual(contents.count(module.BLOCK), 1)
                self.assertLess(contents.index("setup_udev"), contents.index("/usr/bin/begonia-charge-only"))
                self.assertLess(contents.index("/usr/bin/begonia-charge-only"), contents.index("setup_usb_network"))
                self.assertLess(contents.index("setup_usb_network"), contents.index("mount_root_partition"))
                extracted = root / f"extracted{number}"
                extracted.mkdir()
                subprocess.run(["bsdtar", "-xf", output, "-C", extracted], check=True)
                self.assertTrue((extracted / "bin").is_symlink())
                self.assertEqual((extracted / "usr/bin/iskey").read_text(), "test iskey")
                self.assertEqual((extracted / "usr/bin/begonia-charge-only").read_bytes(), HELPER.read_bytes())
                self.assertTrue((extracted / "usr/bin/begonia-charge-only").stat().st_mode & 0o111)
                previous = output
