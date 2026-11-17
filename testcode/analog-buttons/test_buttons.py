#!/usr/bin/env python3
"""Exercise the production analog-button sampler with mocked ADC registers.

Run: python3 testcode/analog-buttons/test_buttons.py (requires gcc).
Checks acquisition/decoding/startup logic, not real electrical tolerances.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
arch = (ROOT / 'src/avr/arch-config.h').read_text()
boot_functions = []
pos = 0
for board in ('petsd', 'mini'):
    start = arch.index('static inline void late_board_init(void)', pos)
    pos = arch.index('{', start) + 1
    depth = 1
    while depth:
        depth += (arch[pos] == '{') - (arch[pos] == '}')
        pos += 1
    boot_functions.append(arch[start:pos].replace('late_board_init(', f'boot_{board}('))
    early_start = arch.rfind('static inline void early_board_init(void)', 0, start)
    early_end = arch.index('{', early_start) + 1
    depth = 1
    while depth:
        depth += (arch[early_end] == '{') - (arch[early_end] == '}')
        early_end += 1
    boot_functions.append(arch[early_start:early_end].replace(
        'early_board_init(', f'early_{board}('))

with tempfile.TemporaryDirectory(prefix='analog-buttons-') as tmp:
    tmp = Path(tmp)
    (tmp / 'util').mkdir()
    (tmp / 'avr').mkdir()
    (tmp / 'util/atomic.h').write_text('''
#define ATOMIC_RESTORESTATE 0
#define ATOMIC_BLOCK(mode) for (unsigned once=1;once;once=0)
''')
    (tmp / 'avr/power.h').write_text('''
enum {clock_div_1=0, clock_div_2=1};
#define clock_prescale_get() mock_prescaler
''')
    (tmp / 'config.h').write_text('''
#define IEC_SLOW_IEEE_FAST
#define _BV(n) (1U << (n))
enum {PA7=7,ADC7D=7,REFS0=6,MUX0=0,MUX1=1,MUX2=2};
enum {ADPS0=0,ADPS1=1,ADPS2=2,ADSC=6,ADEN=7};
''')
    (tmp / 'timer.h').write_text('''
#define KEY_SEL 1
#define KEY_NEXT 2
#define KEY_PREV 4
#define KEY_ANY 0xff
#define delay_ms(ms) fake_delay_ms(ms)
''')
    (tmp / 'boot_under_test.h').write_text('\n'.join(boot_functions))
    (tmp / 'avr/pgmspace.h').write_text('''
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
''')
    subprocess.run([
        'gcc', '-std=c99', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
        '-I' + str(tmp), '-I' + str(ROOT / 'src/avr'),
        str(Path(__file__).with_name('test_buttons.c')), '-o', str(tmp / 'test'),
    ], check=True)
    subprocess.run([str(tmp / 'test')], check=True, timeout=15)
