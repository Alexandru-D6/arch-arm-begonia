"""Check real monitoring transitions against an isolated sysfs tree."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

REPO = Path(__file__).resolve().parents[1]

class ChargeLedTest(unittest.TestCase):
    def test_charging_full_disconnect_and_stop(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            led = root / "class/leds/white:indicator"
            led.mkdir(parents=True)
            (led / "brightness").write_text("0\n")
            (led / "trigger").write_text("none\n")
            supply = root / "class/power_supply/mt6360-chg.1.auto"
            supply.mkdir(parents=True)
            (supply / "online").write_text("1\n")
            (supply / "status").write_text("Charging\n")
            process = subprocess.Popen(["sh", str(REPO / "rootfs/livi/livi-charge-led.sh")],
                env=dict(os.environ, BEGONIA_CHARGE_SYSFS=tmp))
            def expect(value):
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline:
                    if (led / "brightness").read_text().strip() == value:
                        return
                    time.sleep(.02)
                self.fail(f"LED did not become {value}")
            try:
                expect("1")
                (supply / "status").write_text("Not charging\n")
                expect("0")
                (supply / "status").write_text("Full\n")
                expect("1")
                (supply / "online").write_text("0\n")
                expect("0")
                (supply / "online").write_text("1\n")
                expect("1")
            finally:
                process.terminate()
                process.wait(timeout=4)
            self.assertEqual((led / "brightness").read_text(), "0\n")
