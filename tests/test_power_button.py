"""Check power-key timing and backlight behavior without shutting down the host."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("power_button", REPO / "rootfs/livi/begonia-power-button.py")
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


class PowerButtonTests(unittest.TestCase):
    def test_short_release_toggles_without_poweroff(self):
        for duration in (0.05, 1, 2.999):
            with self.subTest(duration=duration):
                button = power.PowerButton()
                self.assertIsNone(button.update(True, 100))
                self.assertEqual(button.update(False, 100 + duration), "toggle")
                self.assertIsNone(button.update(False, 104))

    def test_hold_fires_at_three_seconds_once_and_does_not_toggle_on_release(self):
        button = power.PowerButton()
        button.update(True, 100)
        self.assertIsNone(button.update(True, 102.999))
        self.assertEqual(button.update(True, 103), "poweroff")
        self.assertIsNone(button.update(True, 110))
        self.assertIsNone(button.update(False, 111))
        button.update(True, 112)
        self.assertEqual(button.update(False, 112.1), "toggle")

    def test_release_at_deadline_is_long_even_if_timeout_has_not_run(self):
        button = power.PowerButton()
        button.update(True, 1)
        self.assertEqual(button.update(False, 4), "poweroff")

    def test_key_held_during_startup_requires_release_and_fresh_press(self):
        button = power.PowerButton(initially_down=True)
        self.assertIsNone(button.update(True, 1))
        self.assertIsNone(button.update(True, 30))
        self.assertIsNone(button.update(False, 31))
        button.update(True, 32)
        self.assertEqual(button.update(False, 32.1), "toggle")

    def test_separate_short_presses_do_not_accumulate(self):
        button = power.PowerButton()
        for start in (1, 4, 7):
            button.update(True, start)
            self.assertEqual(button.update(False, start + 2), "toggle")

    def test_backlight_round_trip_preserves_dimmed_brightness(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            for name, value in {"brightness": 73, "actual_brightness": 73,
                                "max_brightness": 1879, "bl_power": 0}.items():
                (path / name).write_text(f"{value}\n")
            backlight = power.Backlight(path)
            backlight.toggle()
            self.assertEqual((path / "brightness").read_text(), "0\n")
            self.assertEqual((path / "bl_power").read_text(), "4\n")
            (path / "actual_brightness").write_text("0\n")
            backlight.toggle()
            self.assertEqual((path / "brightness").read_text(), "73\n")
            self.assertEqual((path / "bl_power").read_text(), "0\n")

    def test_vibration_upload_and_duration_before_closing_device(self):
        with patch.object(power, "input_device", return_value="/dev/input/test"), \
             patch.object(power.os, "open", return_value=9), \
             patch.object(power.fcntl, "ioctl", side_effect=lambda fd, request, effect:
                          power.struct.pack_into("@h", effect, 2, 0)) as ioctl, \
             patch.object(power.os, "write") as write, \
             patch.object(power.time, "sleep") as sleep, \
             patch.object(power.os, "close") as close:
            power.vibrate()
            effect = ioctl.call_args.args[2]
            self.assertEqual(power.struct.unpack_from("@HhHHHHH", effect),
                             (0x50, 0, 0, 0, 0, 120, 0))
            self.assertEqual(power.struct.unpack_from("@HH", effect, 16), (40000, 0))
            self.assertEqual(write.call_args.args[0], 9)
            sleep.assert_called_once_with(0.15)
            close.assert_called_once_with(9)


if __name__ == "__main__":
    unittest.main()
