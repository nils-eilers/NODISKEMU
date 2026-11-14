#!/usr/bin/env python3
"""Check production formatting code against Commodore-ROM output.

python3 testcode/d8x-format/test_format.py          # recorded ROM images
python3 testcode/d8x-format/test_format.py --vice   # also run xpet with real ROMs
"""
import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
from formatter import Formatter

FIXTURES = Path(__file__).resolve().parent / 'fixtures'
RUN_VICE = '--vice' in sys.argv
if RUN_VICE:
    sys.argv.remove('--vice')


def fixture(kind, mode):
    ext = 'd80' if kind == 8 else 'd82'
    return gzip.decompress((FIXTURES/f'{8050 if kind == 8 else 8250}-{mode}.{ext}.gz').read_bytes())


def quick_seed(kind):
    data = bytearray(fixture(kind, 'new'))
    data[:256] = bytes([0x7e])*256
    # Unlinked old directory sectors: the ROM preserves them on quick NEW.
    data[0x45000:0x46b00] = bytes([0x5a])*6912
    return bytes(data)


class DiskFormatting(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.formatter = Formatter()

    @classmethod
    def tearDownClass(cls):
        cls.formatter.close()

    def assert_image(self, expected):
        actual = self.formatter.image()
        if actual != expected:
            offset = next(i for i,(a,b) in enumerate(zip(actual,expected)) if a != b)
            self.fail(f'Image differs at ${offset:x}: firmware ${actual[offset]:02x}, ROM ${expected[offset]:02x}')

    def test_fixture_checksums(self):
        for name, digest in json.loads((FIXTURES/'sha256.json').read_text()).items():
            self.assertEqual(hashlib.sha256(gzip.decompress((FIXTURES/(name+'.gz')).read_bytes())).hexdigest(),digest)

    def test_full_format_matches_rom(self):
        for kind in (8,16):
            with self.subTest(kind=kind):
                self.formatter.lib.setup(kind,0x5a)
                self.assertEqual(self.formatter.lib.run_format(b'FORMAT TEST',b'AB'),0)
                self.assertEqual(self.formatter.lib.commit(),0)
                self.assert_image(fixture(kind,'new'))
                self.assertEqual(self.formatter.lib.blocks_free(),2052 if kind==8 else 4133)

    def test_quick_format_matches_rom(self):
        for kind in (8,16):
            with self.subTest(kind=kind):
                self.formatter.lib.setup(kind,0)
                self.formatter.load(quick_seed(kind))
                self.assertEqual(self.formatter.lib.run_format(b'RENAMED',None),0)
                self.assertEqual(self.formatter.lib.commit(),0)
                self.assert_image(fixture(kind,'quick'))
                self.assertEqual(self.formatter.lib.blocks_free(),2052 if kind==8 else 4133)

    def test_allocation_after_format(self):
        for kind in (8,16):
            with self.subTest(kind=kind):
                self.formatter.lib.setup(kind,0)
                self.assertEqual(self.formatter.lib.run_format(b'ALLOC',b'42'),0)
                # The first file sector is available; BAM/header sectors are not.
                self.assertEqual(self.formatter.lib.allocate(38,1),0)
                expected=2051 if kind==8 else 4132
                self.assertEqual(self.formatter.lib.blocks_free(),expected)
                for sector in (0,3) if kind==8 else (0,3,6,9):
                    self.assertEqual(self.formatter.lib.allocate(38,sector),0)
                self.assertEqual(self.formatter.lib.blocks_free(),expected)
                self.assertEqual(self.formatter.lib.commit(),0)
                self.formatter.lib.reset_cache()
                self.assertEqual(self.formatter.lib.blocks_free(),expected)

    def test_label_is_bounded_and_padded(self):
        for kind in (8,16):
            for label in (b'',b'A',b'1234567890ABCDEF',b'1234567890ABCDEFGHIJKLMNOP'):
                with self.subTest(kind=kind,label=label):
                    self.formatter.lib.setup(kind,0)
                    self.assertEqual(self.formatter.lib.run_format(label,b'XY'),0)
                    data=self.formatter.image()[0x44e00:0x44f00]
                    self.assertEqual(data[6:22],label[:16].ljust(16,b'\xa0'))
                    self.assertEqual(data[22:33],b'\xa0\xa0XY\xa02C'+b'\xa0'*4)

    def test_write_failure(self):
        for kind in (8,16):
            self.formatter.lib.setup(kind,0)
            self.formatter.lib.fail_write(0)
            self.assertEqual(self.formatter.lib.run_format(b'FAIL',b'AB'),25)
            self.assertEqual(self.formatter.image()[0x44e00:0x44e03],bytes(3))

    def test_read_failure(self):
        for kind in (8,16):
            self.formatter.lib.setup(kind,0)
            self.formatter.lib.fail_read(0)
            self.assertEqual(self.formatter.lib.run_format(b'FAIL',b'AB'),20)
            self.assertEqual(self.formatter.image()[0x44e00:0x44e03],bytes(3))

    def test_bam_commit_reports_failure(self):
        self.formatter.lib.setup(8,0)
        self.assertEqual(self.formatter.lib.run_format(b'FAIL',b'AB'),0)
        self.assertEqual(self.formatter.lib.allocate(1,0),0)
        self.formatter.lib.fail_write(0)
        self.assertNotEqual(self.formatter.lib.commit(),0)

    @unittest.skipUnless(RUN_VICE,'use --vice to run the Commodore ROM in xpet')
    def test_live_commodore_rom(self):
        from vice_reference import format_image
        with tempfile.TemporaryDirectory(prefix='nodiskemu-vice-') as directory:
            for kind,ext in ((8,'d80'),(16,'d82')):
                for mode,command in (('new','N0:FORMAT TEST,AB'),('quick','N0:RENAMED')):
                    with self.subTest(kind=kind,mode=mode):
                        image=Path(directory)/(mode+'.'+ext)
                        image.write_bytes(bytes([0x5a])*len(fixture(kind,'new')) if mode=='new' else quick_seed(kind))
                        format_image(image,command)
                        # Also verifies that the recorded fixtures are reproducible.
                        self.assertEqual(image.read_bytes(),fixture(kind,mode))


if __name__ == '__main__':
    unittest.main(verbosity=2)
