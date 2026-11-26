#!/usr/bin/env python3
"""Add begonia's charging-only stage to the known pmOS initramfs layout."""
import argparse
import os
import shlex
import shutil
from pathlib import Path
import subprocess
import tempfile


BLOCK = """# BEGIN BEGONIA CHARGE ONLY
/usr/bin/begonia-charge-only
# END BEGONIA CHARGE ONLY
"""


def add_charge_only(ramdisk, output, work):
    names = subprocess.check_output(["bsdtar", "-tf", ramdisk], text=True).splitlines()
    members = {name.removeprefix("./") for name in names}
    required = {"init_2nd.sh", "usr/bin/iskey", "usr/bin/busybox", "usr/bin/beebzzr"}
    if not required <= members:
        raise ValueError(f"Unsupported pmOS initramfs: missing {sorted(required - members)}")
    init = subprocess.check_output(["bsdtar", "-xOf", ramdisk, "init_2nd.sh"], text=True)
    if BLOCK in init:
        if init.count(BLOCK) != 1:
            raise ValueError("Duplicate charging-only stage")
    else:
        if "BEGONIA CHARGE ONLY" in init:
            raise ValueError("Unrecognized charging-only stage")
        anchor = "setup_udev\n\nsetup_usb_network\n"
        if init.count(anchor) != 1:
            raise ValueError("Cannot locate stage after udev and before USB networking")
        init = init.replace(anchor, f"setup_udev\n\n{BLOCK}\nsetup_usb_network\n")

    stage = work / "stage"
    helper = stage / "usr/bin/begonia-charge-only"
    helper.parent.mkdir(parents=True)
    helper.write_bytes((Path(__file__).resolve().parents[1] / "initramfs/charge-only.sh").read_bytes())
    helper.chmod(0o755)
    # Build a self-contained ARM64 binary, usable before mounting the rootfs.
    compiler = os.environ.get("BEGONIA_CHARGE_CC") or shutil.which("aarch64-linux-gnu-gcc")
    if not compiler:
        local = Path(__file__).resolve().parents[1] / "linux-kernel/toolchain/bin/aarch64-linux-gcc"
        if local.exists():
            compiler = str(local)
    if not compiler:
        raise ValueError("ARM64 C compiler required (or set BEGONIA_CHARGE_CC)")
    display = stage / "usr/bin/begonia-charge-display"
    subprocess.run([*shlex.split(compiler), "-Os", "-static", "-Wall", "-Wextra", "-Werror",
                    str(Path(__file__).resolve().parents[1] / "initramfs/charge-display.c"),
                    "-o", str(display)], check=True)
    display.chmod(0o755)
    second = stage / "init_2nd.sh"
    second.write_text(init)
    second.chmod(0o755)

    # Preserve the archive's ownership, symlinks and device nodes. Only replace
    # these regular files; no need to extract the root-owned archive.
    keep = work / "keep.tar"
    overlay = work / "overlay.tar"
    command = ["bsdtar", "-cf", keep]
    for name in ("init_2nd.sh", "usr/bin/begonia-charge-only", "usr/bin/begonia-charge-display"):
        command.extend(("--exclude", name, "--exclude", f"./{name}"))
    subprocess.run([*command, f"@{ramdisk}"], check=True)
    subprocess.run(["bsdtar", "-cf", overlay, "--uid", "0", "--gid", "0",
                    "-C", stage, "init_2nd.sh", "usr/bin/begonia-charge-only", "usr/bin/begonia-charge-display"], check=True)
    subprocess.run(["bsdtar", "--format=newc", "-czf", output,
                    f"@{keep}", f"@{overlay}"], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ramdisk", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        add_charge_only(args.ramdisk, args.output, Path(temporary))
