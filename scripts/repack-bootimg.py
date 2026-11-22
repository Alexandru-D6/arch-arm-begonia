#!/usr/bin/env python3
"""Repack a begonia v2 boot image using AOSP's exported mkbootimg arguments."""
import argparse
import filecmp
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def unpack(image, directory):
    result = subprocess.check_output([
        "unpack_bootimg", "--boot_img", image, "--out", directory,
        "--format=mkbootimg", "-0",
    ])
    args = os.fsdecode(result).removesuffix("\0").split("\0")
    return dict(zip(args[::2], args[1::2], strict=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("ramdisk", help="replacement ramdisk, or - to keep the original")
    parser.add_argument("cmdline", help="replacement command line, or - to inherit pmOS flags")
    parser.add_argument("output", type=Path)
    parser.add_argument("--kernel")
    parser.add_argument("--dtb")
    parser.add_argument("--modules-dir", type=Path, help="installed module tree for the new kernel release")
    parser.add_argument("--extra-cmdline", default="")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(dir=args.output.parent) as tmp:
        work = Path(tmp)
        fields = unpack(args.base, work / "base")
        if fields.get("--header_version") != "2" or "--dtb" not in fields:
            parser.exit(1, "The begonia pmOS base must have a v2 header and a DTB.\n")
        cmdline = args.cmdline
        if cmdline == "-":
            # Let the pmOS initramfs discover pmOS_root by label.
            selectors = {"pmos_root_uuid", "pmos_boot_uuid", "pmos_root", "pmos_boot"}
            tokens = re.findall(r'(?:[^"\s]|"[^"]*")+', fields["--cmdline"])
            cmdline = " ".join(token for token in tokens
                               if token.partition("=")[0].replace(".", "_") not in selectors)
        fields["--cmdline"] = " ".join(filter(None, (cmdline, args.extra_cmdline)))
        if args.kernel:
            fields["--kernel"] = args.kernel
        if args.dtb:
            fields["--dtb"] = args.dtb
        if args.ramdisk != "-":
            fields["--ramdisk"] = args.ramdisk

        if args.modules_dir:
            refreshed = work / "initramfs.gz"
            subprocess.run([sys.executable, Path(__file__).with_name("refresh-initramfs.py"),
                            fields["--ramdisk"], args.modules_dir, refreshed], check=True)
            fields["--ramdisk"] = str(refreshed)

        components = {"--kernel", "--ramdisk", "--dtb", "--second", "--recovery_dtbo"}
        for key in components.intersection(fields):
            if not Path(fields[key]).stat().st_size:
                parser.exit(1, f"Empty boot component: {key}\n")
        image = work / "boot.img"
        command = ["mkbootimg", "--output", image]
        for key, value in fields.items():
            command.extend((key, value))
        subprocess.run(command, check=True)

        # Compare payloads and header arguments before publishing the output.
        rebuilt = unpack(image, work / "verify")
        for key in components.intersection(fields):
            if key not in rebuilt or not filecmp.cmp(fields[key], rebuilt[key], shallow=False):
                parser.exit(1, f"Boot image payload mismatch: {key}\n")
            rebuilt[key] = fields[key]
        if rebuilt != fields:
            parser.exit(1, "Boot image header mismatch after repacking.\n")
        image.replace(args.output)


if __name__ == "__main__":
    main()
