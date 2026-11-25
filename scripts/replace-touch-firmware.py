#!/usr/bin/env python3
"""Replace the 6.16 begonia touch firmware in a pmOS initramfs."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def replace(ramdisk, firmware, output):
    names = subprocess.check_output(['bsdtar', '-tf', ramdisk], text=True).splitlines()
    matches = [p for p in names if p.lstrip('./') in (
        'usr/lib/firmware/novatek/nt36672a_begonia_csot.bin.zst',
        'lib/firmware/novatek/nt36672a_begonia_csot.bin.zst')]
    if len(matches) != 1:
        raise ValueError('Expected exactly one compressed begonia touch firmware in ramdisk')
    member = matches[0]
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        stage = work / 'stage'
        dest = stage / member.lstrip('./')
        dest.parent.mkdir(parents=True)
        shutil.copyfile(firmware, dest)
        keep, new = work / 'keep.tar', work / 'new.tar'
        subprocess.run(['bsdtar', '-cf', keep, '--exclude', member,
                        '--exclude', './' + member.lstrip('./'), '@' + str(ramdisk)], check=True)
        subprocess.run(['bsdtar', '-cf', new, '--uid', '0', '--gid', '0',
                        '-C', stage, member.lstrip('./')], check=True)
        result = work / 'initramfs.gz'
        subprocess.run(['bsdtar', '--format=newc', '-czf', result,
                        '@' + str(keep), '@' + str(new)], check=True)
        actual = subprocess.check_output(['bsdtar', '-xOf', result, member.lstrip('./')])
        if actual != Path(firmware).read_bytes():
            raise ValueError('Firmware readback mismatch')
        shutil.copyfile(result, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ramdisk', type=Path)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    replace(args.ramdisk, args.firmware, args.output)
