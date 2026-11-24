"""Exercise per-device Bluetooth identity provisioning without host hardware."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]


class BluetoothIdentityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.state = self.root / "state"
        self.log = self.root / "module.args"
        mock = self.root / "modprobe"
        mock.write_text('#!/bin/sh\nprintf "%s\\n" "$@" >> "$BT_TEST_LOG"\n')
        mock.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{self.root}:{os.environ['PATH']}",
                        BT_TEST_LOG=str(self.log),
                        BEGONIA_BLUETOOTH_STATE_DIR=str(self.state))

    def start(self):
        return subprocess.run(
            ["sh", str(REPO / "rootfs/connectivity/begonia-bluetooth")],
            env=self.env, capture_output=True, text=True, timeout=5)

    def test_first_start_provisions_identity_and_later_starts_preserve_it(self):
        first = self.start()
        self.assertEqual(first.returncode, 0, first.stderr)
        path = self.state / "begonia-bdaddr"
        address = path.read_text().strip()
        self.assertTrue(re.fullmatch(r"02(:[0-9a-f]{2}){5}", address))
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        second = self.start()
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(path.read_text().strip(), address)
        self.assertEqual(self.log.read_text().splitlines(),
                         ["hci_stp", f"bdaddr={address}"] * 2)

    def test_configured_address_is_preserved(self):
        self.state.mkdir()
        address = "12:34:56:78:9A:BC"
        (self.state / "begonia-bdaddr").write_text(address + "\n")
        result = self.start()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines(),
                         ["hci_stp", f"bdaddr={address}"])

    def test_invalid_identity_never_loads_controller_or_overwrites_file(self):
        self.state.mkdir()
        path = self.state / "begonia-bdaddr"
        path.write_text("invalid\n")
        result = self.start()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())
        self.assertEqual(path.read_text(), "invalid\n")


if __name__ == "__main__":
    unittest.main()
