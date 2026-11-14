#!/usr/bin/env python3
"""Check the production LCD custom-character loader and petSD+ splash.

Run: python3 testcode/lcd-splash/test_splash.py (requires gcc).
Models LCD memory; physical display appearance still needs hardware review.
"""
from pathlib import Path
import re
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
menu = (ROOT / 'src/menu.c').read_text()
lcd = (ROOT / 'src/avr/lcd.c').read_text()


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


logo = re.search(r'static const PROGMEM uint8_t petsd_logo\[8\]\[8\] = \{.*?\n\};',
                 menu, re.S).group()
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define strlen_P strlen
#define HW_PETSDPLUS 9
#define CONFIG_HARDWARE_VARIANT 9
#define LCD_COLS 20
#define LCD_LINES 4
#define SCRN_SPLASH 1
#define SCRN_STATUS 2
#define HWNAME "petSD+"
#define IEC 0
#define IEEE488 1
#define lcd_printf(...) ((void)0)
static uint8_t lcd_x,lcd_y,active_bus;
static uint16_t lcd_current_screen;
static uint8_t cells[4][20],cgram[64];
static unsigned cgram_pos,clears,loads;
static bool writing_cgram;
const char versionstr[]=TEST_VERSION;
static void lcd_clear(void) {
    memset(cells,' ',sizeof(cells));
    lcd_x=lcd_y=0; clears++; writing_cgram=false;
}
static void lcd_locate(uint8_t x,uint8_t y) {
    if (x>=20) {x=0; y++;}
    if (y>=4) {lcd_clear(); y=0;}
    lcd_x=x; lcd_y=y; writing_cgram=false;
}
static void lcd_send_command(uint8_t value) {
    assert(value==0x40);
    writing_cgram=true; cgram_pos=0; loads++;
}
static void lcd_set_data_mode(void) {}
static void lcd_write(uint8_t value) {
    if (writing_cgram) {
        assert(cgram_pos<64 && value<32);
        cgram[cgram_pos++]=value;
    } else {
        assert(lcd_x<20 && lcd_y<4);
        cells[lcd_y][lcd_x]=value;
    }
}
static void lcd_update_device_addr(void) {}
static void lcd_update_disk_status(void) {}
'''
source += logo + '\n' + function(lcd, 'lcd_load_custom_chars_P')
source += '\n' + function(lcd, 'lcd_putc')
source += r'''
static void lcd_puts_P(const char *s) {while (*s) lcd_putc(*s++);}
'''
source += function(menu, 'lcd_draw_screen')
source += r'''
int main(void) {
    lcd_clear(); lcd_locate(3,1);
    lcd_load_custom_chars_P(&petsd_logo[0][0]);
    assert(cgram_pos==64 && !memcmp(cgram,petsd_logo,64));
    assert(!writing_cgram && lcd_x==3 && lcd_y==1);
    lcd_putc(0); lcd_putc(7); lcd_putc((char)0xff);
    assert(cells[1][3]==0 && cells[1][4]==7 && cells[1][5]==0xff);
    lcd_putc('\t'); assert(lcd_x==6);
    lcd_putc('\n'); assert(lcd_x==0 && lcd_y==2);

    clears=loads=0;
    lcd_draw_screen(SCRN_SPLASH);
    assert(clears==1 && loads==1 && lcd_current_screen==SCRN_SPLASH);
    for (unsigned i=0;i<4;i++) {
        assert(cells[0][9+i]==i);
        assert(cells[1][9+i]==4+i);
    }
    /* Normal-sized text on the baseline; ordinary plus at the upper right. */
    assert(!memcmp(cells[1]+6,"pet",3));
    assert(cells[0][13]=='+');
    assert(cells[0][8]==' ' && cells[0][14]==' ');
    assert(cells[1][5]==' ' && cells[1][13]==' ');
    assert(strlen(versionstr)<=40);
    size_t n=strlen(versionstr);
    if (n<=20) {
        unsigned left=(20-n)/2;
        for (unsigned x=0;x<20;x++) assert(cells[2][x]==' ');
        assert(!memcmp(cells[3]+left,versionstr,n));
        for (unsigned x=0;x<left;x++) assert(cells[3][x]==' ');
        for (unsigned x=left+n;x<20;x++) assert(cells[3][x]==' ');
    } else {
        assert(!memcmp(cells[2],versionstr,n));
    }
    lcd_draw_screen(SCRN_STATUS);
    assert(loads==1 && lcd_current_screen==SCRN_STATUS);
    for (unsigned y=0;y<4;y++)
        for (unsigned x=0;x<20;x++) assert(cells[y][x]>=32);
    puts("LCD splash: 4x2 SD, baseline pet, upper-right plus, version and DDRAM restore passed.");
}
'''

with tempfile.TemporaryDirectory(prefix='lcd-splash-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(source)
    for version in ('NODISKEMU V1.2.0', 'V' * 19, 'V' * 20, 'V' * 21,
                    'NODISKEMU V1.0.0ALPHA0-20261114-7652C44'):
        subprocess.run([
            'gcc', '-std=c99', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
            '-DTEST_VERSION=' + json.dumps(version),
            str(tmp / 'test.c'), '-o', str(tmp / 'test'),
        ], check=True)
        subprocess.run([str(tmp / 'test')], check=True, timeout=10)
