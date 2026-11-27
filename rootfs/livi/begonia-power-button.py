#!/usr/bin/python
"""Own the running phone's power key: short release blanks, 3s hold shuts down."""
import fcntl
import os
from pathlib import Path
import select
import struct
import subprocess
import time


KEY_POWER = 116
EVENT = struct.Struct("@llHHi")
BACKLIGHT = Path("/sys/class/backlight/lm36274-backlight")


class PowerButton:
    def __init__(self, initially_down=False):
        # A key held across boot/service startup must be released first.
        self.wait_release = initially_down
        self.pressed_at = None
        self.fired = False

    def update(self, down, now):
        if self.wait_release:
            self.wait_release = down
            return None
        if down:
            if self.pressed_at is None:
                self.pressed_at = now
                self.fired = False
            if not self.fired and now - self.pressed_at >= 3:
                self.fired = True
                return "poweroff"
        else:
            started = self.pressed_at
            self.pressed_at = None
            if started is not None and not self.fired:
                if now - started >= 3:
                    self.fired = True
                    return "poweroff"
                return "toggle"
        return None


def input_device(name):
    for path in sorted(Path("/sys/class/input").glob("event*/device/name")):
        if path.read_text().strip() == name:
            return Path("/dev/input") / path.parents[1].name
    raise FileNotFoundError(f"Input device {name!r} is unavailable")


def key_down(fd):
    keys = bytearray(96)
    fcntl.ioctl(fd, 0x80004518 | len(keys) << 16, keys)  # EVIOCGKEY
    return bool(keys[KEY_POWER // 8] & (1 << (KEY_POWER % 8)))


class Backlight:
    def __init__(self, path=BACKLIGHT):
        self.path = path
        self.saved = None

    def toggle(self):
        brightness = int((self.path / "brightness").read_text())
        power = int((self.path / "bl_power").read_text())
        actual = int((self.path / "actual_brightness").read_text())
        if brightness > 0 and actual > 0 and power == 0:
            self.saved = brightness
            (self.path / "brightness").write_text("0\n")
            (self.path / "bl_power").write_text("4\n")
            print("Short press: screen off", flush=True)
        else:
            target = self.saved or brightness or int((self.path / "max_brightness").read_text())
            (self.path / "bl_power").write_text("0\n")
            (self.path / "brightness").write_text(f"{target}\n")
            print("Short press: screen on", flush=True)


def vibrate():
    # Linux's 64-bit ff_effect is 48 bytes; rumble union begins at offset 16.
    # Both the target aarch64 and host x86_64 use this ABI.
    effect = bytearray(48)
    struct.pack_into("@HhHHHHH", effect, 0, 0x50, -1, 0, 0, 0, 120, 0)
    struct.pack_into("@HH", effect, 16, 40000, 0)
    fd = os.open(input_device("regulator-haptic"), os.O_RDWR | os.O_CLOEXEC)
    try:
        fcntl.ioctl(fd, 0x40304580, effect)  # EVIOCSFF
        effect_id = struct.unpack_from("@h", effect, 2)[0]
        os.write(fd, EVENT.pack(0, 0, 0x15, effect_id, 1))  # EV_FF
        time.sleep(0.15)  # Finish acknowledgment before shutdown stops devices.
    finally:
        os.close(fd)


def main():
    backlight = Backlight()
    fd = os.open(input_device("mtk-pmic-keys"), os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        # Compare event times and the hold deadline in the same clock domain.
        fcntl.ioctl(fd, 0x400445A0, struct.pack("@i", time.CLOCK_MONOTONIC))
        button = PowerButton(initially_down=key_down(fd))
        resync = False
        print("Ready: short press toggles screen; hold 3s to vibrate and power off", flush=True)
        while True:
            timeout = None
            if button.pressed_at is not None and not button.fired:
                timeout = max(0, button.pressed_at + 3 - time.monotonic())
            readable, _, _ = select.select([fd], [], [], timeout)
            actions = []
            if readable:
                data = os.read(fd, EVENT.size * 64)
                if not data:
                    raise OSError("Power key disconnected")
                for sec, usec, kind, code, value in EVENT.iter_unpack(data):
                    if kind == 0 and code == 3:  # SYN_DROPPED: cancel uncertain hold.
                        button = PowerButton(initially_down=True)
                        resync = True
                    elif resync:
                        if kind == 0 and code == 0:  # SYN_REPORT ends the lost packet.
                            button = PowerButton(initially_down=key_down(fd))
                            resync = False
                    elif kind == 1 and code == KEY_POWER and value in (0, 1):
                        actions.append(button.update(bool(value), sec + usec / 1e6))
            else:
                # Confirm the physical key is still held before committing.
                if key_down(fd):
                    actions.append(button.update(True, time.monotonic()))
                else:
                    button = PowerButton()
            for action in actions:
                if action == "toggle":
                    backlight.toggle()
                elif action == "poweroff":
                    print("Power held for 3s: vibrating and requesting shutdown", flush=True)
                    try:
                        vibrate()
                    except OSError as error:
                        print(f"Vibration unavailable: {error}", flush=True)
                    subprocess.run(["/usr/bin/systemctl", "--no-block", "poweroff"], check=True)
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
