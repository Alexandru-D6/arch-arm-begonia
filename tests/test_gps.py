"""GPS configuration, lifecycle and the real serial NMEA adapter on a fake HAL."""
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO / "rootfs/gps"
loader = importlib.machinery.SourceFileLoader("begonia_gps", str(SOURCE / "begonia-gps"))
spec = importlib.util.spec_from_loader(loader.name, loader)
gps = importlib.util.module_from_spec(spec)
loader.exec_module(gps)


class GpsInstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.root = self.path / "root"
        (self.root / "usr").mkdir(parents=True)
        self.config = self.root / "home/alarm/.config/LIVI/config.json"
        self.config.parent.mkdir(parents=True)
        self.bin = self.path / "bin"
        self.bin.mkdir()
        chroot = self.bin / "chroot"
        chroot.write_text('''#!/usr/bin/env python3
import os, sys
if sys.argv[2:4] == ["getent", "passwd"]:
    print(f"alarm:x:{os.getuid() or 1000}:{os.getgid()}:alarm:/home/alarm:/bin/bash")
if any(a in ("start", "restart", "--now") for a in sys.argv):
    sys.exit("offline installer tried to start services")
''')
        chroot.chmod(0o755)
        self.adapters = self.path / "adapters"
        self.adapters.mkdir()
        for name in ("begonia-gps-nmea", "begonia-gps-properties32.so", "begonia-gps-properties64.so"):
            (self.adapters / name).write_text("build fixture")

    def install(self):
        return subprocess.run(["bash", str(REPO / "rootfs/install-gps.sh"),
                               str(self.root), "alarm", str(self.adapters)],
                              env=dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}"),
                              capture_output=True, text=True, timeout=10)

    def test_offline_install_preserves_settings_off_choice_and_original_backup(self):
        original = {"gpsEnabled": False, "gpsDevice": "/dev/ttyAMA0",
                    "country": "ES", "wifiPassword": "keep-me", "touchCalibration": [1, 0, 0]}
        self.config.write_text(json.dumps(original))
        for _ in range(2):
            result = self.install()
            self.assertEqual(result.returncode, 0, result.stderr)
        config = json.loads(self.config.read_text())
        self.assertFalse(config["gpsEnabled"])
        self.assertEqual(config["gpsDevice"], "/run/begonia-gps/nmea")
        self.assertEqual(config["wifiPassword"], original["wifiPassword"])
        self.assertEqual(config["touchCalibration"], original["touchCalibration"])
        backup = self.config.with_name("config.before-begonia-gps.json")
        self.assertEqual(json.loads(backup.read_text()), original)
        self.assertEqual(backup.stat().st_mode & 0o777, 0o600)
        unit = (self.root / "etc/systemd/system/begonia-gps.service").read_text()
        self.assertIn("PrivateMounts=yes", unit)
        self.assertNotIn("__", unit)

    def test_new_configuration_enables_internal_receiver(self):
        result = self.install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.config.read_text()), {
            "gpsEnabled": True, "gpsDevice": "/run/begonia-gps/nmea", "gpsBaudRate": 38400})

    def test_invalid_config_is_preserved_without_installing_service(self):
        self.config.write_text("broken config")
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.config.read_text(), "broken config")
        self.assertFalse((self.root / "etc/systemd/system/begonia-gps.service").exists())


class GpsSupervisorTests(unittest.TestCase):
    def test_receiver_only_runs_for_explicit_internal_gps_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "config.json"
            for value, expected in [({}, False),
                                    ({"gpsEnabled": True, "gpsDevice": "/dev/ttyUSB0"}, False),
                                    ({"gpsEnabled": False, "gpsDevice": "/run/begonia-gps/nmea"}, False),
                                    ({"gpsEnabled": "true", "gpsDevice": "/run/begonia-gps/nmea"}, False),
                                    ({"gpsEnabled": True, "gpsDevice": "/run/begonia-gps/nmea"}, True)]:
                config.write_text(json.dumps(value))
                self.assertEqual(gps.enabled(config), expected)
            config.write_text("incomplete write")
            self.assertFalse(gps.enabled(config))

    def test_toggle_off_and_shutdown_stop_client_before_engine(self):
        events = []
        daemon = Mock(name="daemon")
        client = Mock(name="client")
        daemon.poll.return_value = client.poll.return_value = None
        gps.stopping = False
        self.addCleanup(setattr, gps, "stopping", False)

        def tick(_seconds):
            if len(events) >= 2:
                gps.stopping = True

        with patch.object(gps, "enabled", side_effect=[True, True, False]), \
                patch.object(gps, "hal_ready", return_value=True), \
                patch.object(gps, "start_android", side_effect=[daemon, client]), \
                patch.object(gps, "stop", side_effect=lambda p: events.append(p) if p else None), \
                patch.object(gps.time, "sleep", side_effect=tick), \
                patch.object(gps, "OUTPUT", Path("/tmp/nonexistent-begonia-gps-test")):
            gps.serve(Path("unused"))
        self.assertEqual(events, [client, daemon])


@unittest.skipUnless(shutil.which("cc"), "native C compiler required for the fake HAL")
class GpsNmeaTests(unittest.TestCase):
    def test_nmea_crosses_a_real_pty_and_link_is_removed_on_stop(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            fake = directory / "fake.c"
            fake.write_text(r'''
#include "android-gps-abi.h"
#include <string.h>
#include <unistd.h>
static struct gps_callbacks *cb;
static volatile int running;
static void *send_nmea(void *unused) {
    (void)unused;
    while (running) {
        const char *s = "$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*69\r\n";
        cb->nmea(0, s, strlen(s));
        usleep(50000);
    }
    return 0;
}
static int init(struct gps_callbacks *c) { cb=c; return c->size == 112 ? 0 : -1; }
static int start(void) { pthread_t t; running=1; return pthread_create(&t, 0, send_nmea, 0); }
static int stop(void) { running=0; return 0; }
static void cleanup(void) { }
static int mode(int a,int b,uint32_t c,uint32_t d,uint32_t e) { return 0; }
static const struct gps_interface iface = {
    .size=sizeof(iface), .init=init, .start=start, .stop=stop, .cleanup=cleanup,
    .set_position_mode=mode
};
static const struct gps_interface *get(struct gps_device *d) { return &iface; }
static struct gps_device device = {.get_gps_interface=get};
static int open_gps(const struct gps_hw_module *m, const char *s, struct gps_hw_device **d) {
    *d=(struct gps_hw_device *)&device; return 0;
}
static struct gps_hw_methods methods = {.open=open_gps};
struct gps_hw_module HMI = {.methods=&methods};
''')
            library = directory / "fake.so"
            binary = directory / "adapter"
            subprocess.run(["cc", "-shared", "-fPIC", "-I", str(SOURCE), str(fake),
                            "-pthread", "-o", str(library)], check=True, capture_output=True)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(SOURCE / "begonia-gps-nmea.c"),
                            "-ldl", "-pthread", "-o", str(binary)], check=True, capture_output=True)
            link = directory / "nmea"
            process = subprocess.Popen([str(binary), str(link)], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.PIPE,
                                       env=dict(os.environ, BEGONIA_GPS_HAL=str(library)))
            try:
                deadline = time.monotonic() + 5
                while not link.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.02)
                self.assertTrue(link.exists(), process.poll())
                fd = os.open(link, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                try:
                    self.assertTrue(select.select([fd], [], [], 2)[0])
                    self.assertIn(b"$GPGGA,", os.read(fd, 4096))
                    os.write(fd, b"\xb5\x62\x0a\x04\x00\x00\x0e\x34")
                finally:
                    os.close(fd)
            finally:
                process.terminate()
                _, error = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 0, error.decode())
            self.assertFalse(link.is_symlink())


if __name__ == "__main__":
    unittest.main()
