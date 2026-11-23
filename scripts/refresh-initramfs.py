#!/usr/bin/env python3
"""Refresh pmOS early-boot modules, including begonia USB and haptic support."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def archive_files(archive):
    listing = subprocess.check_output(["bsdtar", "-tf", archive], text=True)
    return {Path(name) for name in listing.splitlines()}


def refresh(ramdisk, installed, output, work):
    release = installed.name
    members = archive_files(ramdisk)
    prefixes = (Path("usr/lib/modules"), Path("lib/modules"))
    roots = sorted({prefix / path.relative_to(prefix).parts[0]
                    for prefix in prefixes for path in members
                    if prefix in path.parents and len(path.relative_to(prefix).parts) > 1})
    if len(roots) != 1:
        raise SystemExit("pmOS ramdisk must contain exactly one kernel module tree")
    original_root = roots[0]
    root = original_root.parent / release
    modules = [path.relative_to(original_root) for path in members
               if original_root in path.parents and path.name.endswith((".ko", ".ko.gz", ".ko.xz", ".ko.zst"))]
    if not modules:
        raise SystemExit(f"No early-boot modules in {root}")

    deps = {}
    for line in (installed / "modules.dep").read_text().splitlines():
        name, dependencies = line.split(":", 1)
        deps[Path(name)] = [Path(dep) for dep in dependencies.split()]
    by_name = {name.name.split(".ko")[0]: name for name in deps}
    builtins = {Path(name).stem for name in (installed / "modules.builtin").read_text().splitlines()}
    needed = set()
    for original in modules:
        name = original.name.split(".ko")[0]
        if name not in builtins:
            if name not in by_name:
                raise SystemExit(f"Missing compiled early-boot module: {name}")
            needed.add(by_name[name])
    # The USB controller depends on the MT6360 VBUS regulator and Type-C
    # role switch. Without these modules it remains deferred in the initramfs,
    # so neither the network gadget nor the debug shell can appear over USB.
    # The charging-only stage acknowledges Power through the vibrator before
    # the rootfs is mounted. Keep its driver in this early module tree too.
    for name in ("mt6360-regulator", "tcpci_mt6360", "regulator-haptic"):
        if name not in builtins:
            if name not in by_name:
                raise SystemExit(f"Missing begonia early-boot module: {name}")
            needed.add(by_name[name])
    # modules.dep lists the complete load order for each module.
    selected = needed.union(*(deps[path] for path in needed))
    stage = work / "stage"
    tree = stage / root
    tree.mkdir(parents=True)
    if root.parts[0] == "usr":
        (stage / "lib").symlink_to("usr/lib")
    for path in selected:
        (tree / path).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(installed / path, tree / path)
    for meta in ("modules.order", "modules.builtin", "modules.builtin.modinfo"):
        shutil.copyfile(installed / meta, tree / meta)
    subprocess.run(["depmod", "-b", stage, release], check=True)

    # Copy the rest of the archive with libarchive to preserve device nodes,
    # ownership and symlinks without needing root privileges.
    keep = work / "keep.tar"
    new = work / "modules.tar"
    subprocess.run(["bsdtar", "-cf", keep, "--exclude", str(original_root),
                    "--exclude", f"./{original_root}", f"@{ramdisk}"], check=True)
    subprocess.run(["bsdtar", "-cf", new, "--uid", "0", "--gid", "0",
                    "-C", stage, root], check=True)
    subprocess.run(["bsdtar", "--format=newc", "-czf", output,
                    f"@{keep}", f"@{new}"], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ramdisk", type=Path)
    parser.add_argument("installed_modules", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        refresh(args.ramdisk, args.installed_modules, args.output, Path(tmp))
