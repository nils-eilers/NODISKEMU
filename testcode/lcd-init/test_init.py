#!/usr/bin/env python3
"""Replay production LCD bus writes against HD44780 and WS0010 models.

Run: python3 testcode/lcd-init/test_init.py (requires gcc).
Covers cold start, both 4-bit nibble phases, stale data transfers, repeated
initialization, controller-specific commands and clock-dependent delays.
The models check protocol behavior, not physical/electrical tolerances.
References: Hitachi HD44780U instruction table and 4-bit initialization;
Winstar WS0010 vB, instructions and "Repeated procedures for an 4-bit bus".
https://www.sparkfun.com/datasheets/LCD/HD44780.pdf
https://www.crystalfontz.com/controllers/Winstar%20Display/WS0010/491
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/avr/lcd.c').read_text()


def function(name):
    match = re.search(r'^[^\n;{}]*\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    start = source.index('{', match.start()) + 1
    end, depth = start, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define _BV(n) (1u << (n))
#define IEC 0
#define LCD_PIN_E 4
#define LCD_PIN_RS 3
#define LCD_COLS 20
#define LCD_LINES 4
#define LCD_ADDR_LINE1 0
#define LCD_ADDR_LINE2 64
#define LCD_ADDR_LINE3 20
#define LCD_ADDR_LINE4 84
static uint8_t LCD_PORT_E, LCD_DDR_E, LCD_PORT_RS, LCD_DDR_RS;
static uint8_t LCD_PORT_DATA, LCD_DDR_DATA, lcd_x, lcd_y, active_bus;
static FILE lcd_stream;
static FILE *lcd_stdout;
static unsigned long elapsed_us;
static unsigned clock_scale = 1;
static void delay_us(unsigned us) {
  if (LCD_PORT_E & _BV(LCD_PIN_E)) {
    printf("%lu %u %u\n", elapsed_us, LCD_PORT_DATA & 15,
           !!(LCD_PORT_RS & _BV(LCD_PIN_RS)));
  }
  elapsed_us += us / clock_scale;
}
static void delay_ms(unsigned ms) {elapsed_us += ms * 1000ul / clock_scale;}
#define _delay_us(x) delay_us(x)
#define _delay_ms(x) delay_ms(x)
void lcd_clear(void);
'''
harness += source[source.index('#define LCD_DELAY_US_DATA'):source.index('uint8_t lcd_x;')]
harness += source[source.index('#ifdef IEC_SLOW_IEEE_FAST'):source.index('static inline void lcd_set_data_mode')]
for name in ('lcd_set_data_mode', 'lcd_set_command_mode', 'lcd_pulse_e',
             'lcd_write', 'lcd_send_command', 'lcd_locate', 'lcd_clear',
             'lcd_lines_init', 'lcd_init', 'lcd_putc'):
    harness += '\n' + function(name)
harness += r'''
int main(int argc, char **argv) {
  (void)argc;
  active_bus = (uint8_t)atoi(argv[1]);
#ifdef IEC_SLOW_IEEE_FAST
  clock_scale = active_bus == IEC ? 1 : 2;
#endif
  LCD_PORT_E = LCD_PORT_RS = LCD_PORT_DATA = 0xff;
  lcd_init();
  if (lcd_stdout != &lcd_stream) return 1;
  if (LCD_PORT_E & _BV(LCD_PIN_E)) return 2;
  if (LCD_PORT_RS & _BV(LCD_PIN_RS)) return 3;
  if ((LCD_PORT_DATA & 0xf0) != 0xf0) return 4;
  if (!(LCD_DDR_E & _BV(LCD_PIN_E))) return 5;
  if (!(LCD_DDR_RS & _BV(LCD_PIN_RS))) return 6;
  if ((LCD_DDR_DATA & 15) != 15) return 7;
  puts("READY");
  lcd_putc('A');
}
'''


class Controller:
    def __init__(self, oled, four_bit, pending=None, pending_data=False):
        self.oled, self.four_bit = oled, four_bit
        self.pending, self.pending_data = pending, pending_data
        self.zeros = 0
        self.busy_until = 0
        self.address, self.shift = 37, 9
        self.display = False
        self.increment = False
        self.graphic, self.power = True, False
        self.ram = {}
        self.commands = []

    def write(self, time, nibble, data):
        assert time >= self.busy_until, (self.oled, time, self.busy_until)
        if self.oled and self.four_bit and not data:
            self.zeros = self.zeros + 1 if nibble == 0 else 0
            if self.zeros == 5:
                # WS0010 vB: after five zero nibbles the next is the LOW half.
                self.pending, self.pending_data = 0, False
                return
        if not self.four_bit:
            self.execute(time, nibble << 4, data)
        elif self.pending is None:
            self.pending, self.pending_data = nibble, data
        else:
            value, kind = self.pending << 4 | nibble, self.pending_data
            self.pending = None
            self.execute(time, value, kind)

    def execute(self, time, value, data):
        if data:
            self.ram[self.address] = value
            self.address += 1 if self.increment else -1
        else:
            self.commands.append(value)
            if value & 0xe0 == 0x20:
                # WS0010 forbids FT=01 and FT=11 in 4-bit operation.
                if self.oled and self.four_bit and value & 1:
                    return
                self.four_bit = not bool(value & 0x10)
            elif value & 0xf0 == 0x10:
                if self.oled and value & 3 == 3:
                    self.graphic, self.power = bool(value & 8), bool(value & 4)
                elif value & 8:
                    self.shift += 1 if value & 4 else -1
                else:
                    self.address += 1 if value & 4 else -1
            elif value & 0xf8 == 8:
                self.display = bool(value & 4)
            elif value & 0xfc == 4:
                self.increment = bool(value & 2)
            elif value in (1, 2, 3):
                self.address = self.shift = 0
                if value == 1:
                    self.ram.clear()
            elif value & 0x80:
                self.address = value & 0x7f
        self.busy_until = time + (2000 if not data and value in (1, 2, 3) else 40)

    def ready(self):
        assert self.four_bit and self.pending is None
        assert self.display and self.increment
        assert self.address == self.shift == 0
        assert self.ram == {}
        assert 0x28 in self.commands and 0x17 in self.commands
        if self.oled:
            assert self.power and not self.graphic


cases = 0
with tempfile.TemporaryDirectory(prefix='nodiskemu-lcd-init-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(harness)
    for dual in (False, True):
        subprocess.run([
            'gcc', '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
            *(['-DIEC_SLOW_IEEE_FAST'] if dual else []),
            str(tmp / 'test.c'), '-o', str(tmp / 'test'),
        ], check=True)
        for bus in ((0, 1) if dual else (0,)):
            output = subprocess.check_output([str(tmp / 'test'), str(bus)], text=True)
            init_text, data_text = output.split('READY\n')
            init = [tuple(map(int, line.split())) for line in init_text.splitlines()]
            writes = [tuple(map(int, line.split())) for line in data_text.splitlines()]
            assert init[0][0] >= 500000
            assert all(not data for _, _, data in init)
            for oled in (False, True):
                states = [(False, None, False), (True, None, False)]
                states += [(True, high, data) for high in range(16) for data in (False, True)]
                for four_bit, pending, data in states:
                    controller = Controller(oled, four_bit, pending, data)
                    for time, nibble, kind in init:
                        controller.write(time, nibble, kind)
                    controller.ready()
                    for time, nibble, kind in writes:
                        controller.write(time, nibble, kind)
                    assert controller.ram == {0: ord('A')}
                    # A subsequent MCU reset must recover a previously active display.
                    offset = writes[-1][0] + 10000
                    for time, nibble, kind in init:
                        controller.write(offset + time, nibble, kind)
                    controller.ready()
                    cases += 1
print(f'LCD initialization: {cases} HD44780/WS0010 startup states, clocks, timings and resets passed.')
