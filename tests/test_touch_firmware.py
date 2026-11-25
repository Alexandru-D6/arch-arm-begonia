"""Firmware selection and initramfs preservation without root or a live device."""
import hashlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('touch_fw', REPO / 'scripts/replace-touch-firmware.py')
FW = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FW)
COMMIT = '33aa9fe1db0079945fa272e42f04db732489319d'
MEMBER = 'usr/lib/firmware/novatek/nt36672a_begonia_csot.bin.zst'


class TouchFirmwareTests(unittest.TestCase):
    def test_replacement_preserves_other_contents_and_links(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            with tarfile.open(p / 'original.tar', 'w') as archive:
                for name, data in [(MEMBER, b'csot'), ('init', b'original init'),
                                   ('usr/lib/modules/test.ko', b'original module')]:
                    info = tarfile.TarInfo(name); info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
                link = tarfile.TarInfo('lib'); link.type = tarfile.SYMTYPE
                link.linkname = 'usr/lib'; archive.addfile(link)
            (p / 'selected.zst').write_bytes(b'tianma')
            FW.replace(p / 'original.tar', p / 'selected.zst', p / 'result.gz')
            for name, expected in [(MEMBER, b'tianma'), ('init', b'original init'),
                                   ('usr/lib/modules/test.ko', b'original module')]:
                self.assertEqual(subprocess.check_output(
                    ['bsdtar', '-xOf', p / 'result.gz', name]), expected)
            listing = subprocess.check_output(['bsdtar', '-tvf', p / 'result.gz'], text=True)
            self.assertIn('lib -> usr/lib', listing)
            self.assertEqual(listing.count('nt36672a_begonia_csot.bin.zst'), 1)
            with tarfile.open(p / 'empty.tar', 'w'):
                pass
            with self.assertRaises(ValueError):
                FW.replace(p / 'empty.tar', p / 'selected.zst', p / 'bad.gz')

    def test_installer_selects_variants_and_rejects_corrupt_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            archive = p / 'firmware.tar.gz'
            with tarfile.open(archive, 'w:gz') as tar:
                for variant in ['csot', 'tianma']:
                    data = variant.encode()
                    info = tarfile.TarInfo(f'firmware-{COMMIT}/novatek/nt36672a_begonia_{variant}.bin.zst')
                    info.size = len(data); tar.addfile(info, io.BytesIO(data))
            script = (REPO / 'rootfs/install-touchscreen.sh').read_text()
            digest = next(s.split('=', 1)[1] for s in script.splitlines() if s.startswith('digest='))
            installer = p / 'install.sh'
            installer.write_text(script.replace(digest, hashlib.sha512(archive.read_bytes()).hexdigest()))
            root = p / 'root'
            for variant in ['tianma', 'csot']:
                subprocess.run(['bash', installer, root, variant, archive], check=True, capture_output=True)
                self.assertEqual((root / MEMBER).read_bytes(), variant.encode())
                self.assertEqual((root / 'usr/share/begonia/touchscreen-variant').read_text(), variant + '\n')
            archive.write_bytes(b'corrupt')
            for variant in ['tianma', 'invalid']:
                r = subprocess.run(['bash', installer, root, variant, archive], capture_output=True)
                self.assertNotEqual(r.returncode, 0)
                self.assertEqual((root / MEMBER).read_bytes(), b'csot')


if __name__ == '__main__':
    unittest.main()
