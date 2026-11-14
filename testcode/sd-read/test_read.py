#!/usr/bin/env python3
"""Test production SD read recovery and FAT cache handling with host I/O stubs.

Run: python3 testcode/sd-read/test_read.py (requires gcc).
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r'^[^\n;{}]*\b' + name + r'\s*\([^;{}]*\)\s*(?:/\*.*?\*/\s*)?\{', source, re.M)
    if not match:
        raise ValueError(name)
    # Nested blocks are indented; counting braces would include both #if arms.
    end = source.index('\n}', match.end()) + 2
    return source[match.start():end]


sd = (ROOT / 'src/sdcard.c').read_text()
fat = (ROOT / 'src/ff.c').read_text()
with tempfile.TemporaryDirectory(prefix='nodiskemu-sd-read-') as directory:
    directory = Path(directory)
    (directory / 'production.c').write_text('\n'.join(
        function(sd, name) for name in ('sd_read_once', 'sd_read')))
    (directory / 'window.c').write_text(function(fat, 'move_window'))
    for shared in (0, 1):
        subprocess.run([
            'gcc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
            f'-D_USE_1_BUF={shared}', '-I' + str(directory),
            str(Path(__file__).with_name('test_read.c')), '-o', str(directory / 'test'),
        ], check=True)
        subprocess.run([str(directory / 'test')], check=True, timeout=15)
