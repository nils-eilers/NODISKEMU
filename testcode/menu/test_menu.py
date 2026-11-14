#!/usr/bin/env python3
"""Run the production LCD browser and buffer allocator with host stubs.

Usage: python3 testcode/menu/test_menu.py
Requires gcc. Uses ASan/UBSan; no device or firmware flashing is needed.
The entry layout is packed like AVR, but pointers retain the host width.
Both a 64-entry and a 256-entry pointer table are exercised on 64-bit hosts.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r'^[^\n;{}]*\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    pos = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[match.start():pos]


menu = (ROOT / 'src/menu.c').read_text()
buffers = (ROOT / 'src/buffers.c').read_text()
declarations = menu[menu.index('#ifndef CONFIG_DIR_BUFFERS'):menu.index('static tick_t')]
table = re.search(r'^static entry_t .*;', menu, re.M).group()
allocator = '\n'.join(function(buffers, name) for name in (
    'callback_dummy', 'alloc_specific_buffer', 'alloc_system_buffer',
    'alloc_linked_buffers', 'free_buffer'))
browser = '\n'.join(function(menu, name) for name in (
    'clear_command_buffer', 'compare', 'menu_browse_files'))

with tempfile.TemporaryDirectory(prefix='nodiskemu-menu-') as tmp:
    tmp = Path(tmp)
    (tmp / 'production.c').write_text(
        '#pragma pack(push, 1)\n' + declarations + '\n#pragma pack(pop)\n'
        + table + '\n' + allocator + '\n' + browser)
    for count in (2, 8):
        subprocess.run([
            'gcc', '-std=gnu99', '-g', '-O1', '-fno-omit-frame-pointer',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
            f'-DCONFIG_DIR_BUFFERS={count}', '-I' + str(tmp),
            str(Path(__file__).with_name('test_menu.c')), '-o', str(tmp / 'test'),
        ], check=True)
        subprocess.run([str(tmp / 'test')], check=True, timeout=15)
