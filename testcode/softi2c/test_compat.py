#!/usr/bin/env python3
"""Check SDA-rise handling, write errors and the legacy Tiny register format.

Run: python3 testcode/softi2c/test_compat.py (requires gcc).
Mocks electrical delay and byte transfers; does not replace a hardware test.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/avr/softi2c.c').read_text()


def function(name):
    match = re.search(r'^[^\n;{}]*\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    pos = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[match.start():pos]


PINS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint8_t ddr,port;
static unsigned elapsed,ready_at,pulses;
static uint8_t stuck;
static uint8_t pin(void) {
    if (ddr & 2) pulses++;
    return (~ddr & 3) & ~stuck & (elapsed>=ready_at ? 3 : 2);
}
static void delay(unsigned us) {elapsed+=us; assert(elapsed<500000);}
#include "softi2c.c"
static void reset(void) {ddr=port=0; elapsed=pulses=stuck=0; ready_at=100;}
int main(void) {
    reset();
    assert(recover_bus()==0 && elapsed>=100 && pulses==0);
    reset();
    assert(stop_condition()==0 && elapsed>=100);
    reset(); stuck=2;
    assert(recover_bus()==I2C_ERROR_SCL_TIMEOUT && elapsed==200000);
    assert((ddr&3)==0);
    reset(); stuck=1;
    assert(recover_bus()==I2C_ERROR_SDA_STUCK && elapsed>=400000);
    assert((ddr&3)==0);
    reset(); stuck=2;
    assert(finish_transfer(I2C_ERROR_REGISTER_NACK)==I2C_ERROR_REGISTER_NACK);
    assert((ddr&3)==0);
    puts("SDA rise: delayed idle/STOP succeeds; stuck lines time out and release.");
}
'''

BYTES = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define HAVE_I2C
#include "i2c.h"
#include "i2c-lcd-pwm-io-config.h"
static unsigned sent,fail_at,stops,releases,writes;
static uint8_t fail_status,start_error,recovery_error,stop_error;
static uint8_t pending,last_reg,last_value,need_address;
static uint8_t recover_bus(void) {return recovery_error;}
static uint8_t start_condition(void) {need_address=1; return start_error;}
static uint8_t stop_condition(void) {stops++; return stop_error;}
static void release_bus(void) {releases++;}
static uint8_t i2c_send_byte(uint8_t value) {
    sent++;
    if (sent==fail_at) return fail_status;
    if (need_address) {need_address=0; assert(value==0x4c || value==0x4d); return 0;}
    /* Original Tiny firmware: nonzero register is the receive-phase flag. */
    if (pending==0) pending=value;
    else {last_reg=pending; last_value=value; writes++; pending=0;}
    return 0;
}
static uint8_t i2c_recv_byte(uint8_t ack,uint8_t *data) {(void)ack; *data=0x4e; return 0;}
'''
BYTES += '\n'.join(function(name) for name in (
    'finish_transfer', 'send_checked', 'i2c_write_register', 'i2c_write_registers',
    'i2c_read_register', 'i2c_read_registers'))
BYTES += r'''
static void reset(void) {
    sent=fail_at=stops=releases=writes=0;
    fail_status=start_error=recovery_error=stop_error=0;
    pending=need_address=0;
}
int main(void) {
    /* Use the real shared configuration, not a test-only address. */
    assert(I2C_SLAVE_ADDRESS==0x4c);
    for (unsigned reg=1;reg<=3;reg++) {
        reset();
        assert(i2c_write_register(I2C_SLAVE_ADDRESS,reg,42)==0);
        assert(writes==1 && last_reg==reg && last_value==42 && pending==0);
        assert(stops==1 && releases==1);
    }
    uint8_t expected[]={0,I2C_ERROR_ADDRESS_NACK,I2C_ERROR_REGISTER_NACK,I2C_ERROR_DATA_NACK};
    for (unsigned phase=1;phase<=3;phase++) {
        reset(); fail_at=phase; fail_status=1;
        assert(i2c_write_register(0x4c,1,42)==expected[phase]);
        assert(sent==phase && writes==0 && stops==1 && releases==1);
        reset(); fail_at=phase; fail_status=I2C_ERROR_SCL_TIMEOUT;
        assert(i2c_write_register(0x4c,1,42)==I2C_ERROR_SCL_TIMEOUT);
    }
    reset(); stop_error=I2C_ERROR_SDA_STUCK;
    assert(i2c_write_register(0x4c,1,42)==I2C_ERROR_SDA_STUCK);
    reset(); recovery_error=I2C_ERROR_SCL_TIMEOUT;
    assert(i2c_write_register(0x4c,1,42)==I2C_ERROR_SCL_TIMEOUT && sent==0);
    reset();
    assert(i2c_read_register(I2C_SLAVE_ADDRESS,I2C_SOFTWARE_VERSION_COMPAT)==0x4e && pending==0);
    sent=0;
    assert(i2c_write_register(I2C_SLAVE_ADDRESS,PWM_BRIGHTNESS,128)==0 && last_reg==2 && last_value==128);
    puts("Legacy Tiny: contrast/brightness/bus writes and version workaround pass; error phases preserved.");
}
'''

with tempfile.TemporaryDirectory(prefix='softi2c-compat-') as tmp:
    tmp = Path(tmp)
    (tmp / 'avr').mkdir()
    (tmp / 'util').mkdir()
    (tmp / 'avr/io.h').write_text('#define _BV(n) (1U << (n))\n')
    (tmp / 'util/delay.h').write_text('#define _delay_us(us) delay(us)\n')
    (tmp / 'config.h').write_text('''
#define HAVE_I2C
#define SOFTI2C_BIT_SDA 0
#define SOFTI2C_BIT_SCL 1
#define SOFTI2C_DDR ddr
#define SOFTI2C_PORT port
#define SOFTI2C_PIN pin()
#define SOFTI2C_DELAY 6
''')
    for name, code in [('pins', PINS), ('bytes', BYTES)]:
        (tmp / f'{name}.c').write_text(code)
        subprocess.run([
            'gcc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
            '-I'+str(tmp), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'src/avr'),
            '-I'+str(ROOT/'lcd-i2c-pwm-io'),
            str(tmp/f'{name}.c'), '-o', str(tmp/name),
        ], check=True)
        subprocess.run([str(tmp/name)], check=True, timeout=10)
