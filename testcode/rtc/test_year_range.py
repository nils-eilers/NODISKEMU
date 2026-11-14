#!/usr/bin/env python3
"""Check production time-command parsing and DS1307/DS3231 year bounds.

Run: python3 testcode/rtc/test_year_range.py (requires gcc).
Host I2C stubs exercise register contents, not physical bus timing.
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


dsrtc = (ROOT / 'src/ds1307-3231.c').read_text()
doscmd = (ROOT / 'src/doscmd.c').read_text()
utils = (ROOT / 'src/utils.c').read_text()
parser = (ROOT / 'src/parser.c').read_text()
defines = '\n'.join(line for line in dsrtc.splitlines() if line.startswith('#define'))
chip_type = re.search(r'static enum \{[^}]+\} dsrtc_type;', dsrtc).group()
days = re.search(r'^static const PROGMEM uint8_t downames\[\].*;', doscmd, re.M).group()
skel = re.search(r'^static const PROGMEM uint8_t asciitime_skel\[\].*;', doscmd, re.M).group()

PRELUDE = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#define HAVE_RTC
#include "rtc.h"
#define PROGMEM
#define memcpy_P memcpy
#define memcmp_P memcmp
#define ERROR_SYNTAX_UNABLE 31
#define ERROR_SYNTAX_UNKNOWN 30
rtcstate_t rtc_state;
const struct tm rtc_default_date = {.tm_year=82,.tm_mon=7,.tm_mday=31,.tm_wday=2};
static uint8_t command_buffer[120], command_length, current_error;
static uint8_t error_buffer[100];
#define ERRORBUFFER_IDX 0
static struct {uint8_t lastused;} buffers[1];
static uint8_t registers[32];
static unsigned writes, set_calls;
static bool read_failure;
static struct tm requested;
static void set_error(uint8_t value) {current_error=value;}
static uint8_t i2c_read_registers(uint8_t addr, uint8_t reg, uint8_t count, void *data) {
    (void)addr;
    if (read_failure) return 1;
    memcpy(data,registers+reg,count); return 0;
}
static uint8_t i2c_write_registers(uint8_t addr, uint8_t reg, uint8_t count, const void *data) {
    (void)addr; writes++; memcpy(registers+reg,data,count); return 0;
}
static uint8_t i2c_write_register(uint8_t addr, uint8_t reg, uint8_t value) {
    return i2c_write_registers(addr,reg,1,&value);
}
'''

TEST = r'''
static void reset(void) {
    memset(registers,0,sizeof(registers));
    memset(command_buffer,0,sizeof(command_buffer));
    writes=set_calls=current_error=0;
    rtc_state=RTC_OK; dsrtc_type=RTC_3231; read_failure=false;
}
static void check_command(bool valid, unsigned year) {
    parse_timewrite();
    assert(set_calls==(unsigned)valid);
    assert(current_error==(valid ? 0 : ERROR_SYNTAX_UNABLE));
    assert((writes!=0)==valid);
    if (valid) assert(requested.tm_year==year);
}
int main(void) {
    unsigned iso[]={1980,1999,2000,2099,2100,2156,2255,2256,2356,9999,67635,1979};
    for (unsigned i=0;i<sizeof(iso)/sizeof(iso[0]);i++) {
        reset();
        command_length=snprintf((char *)command_buffer,sizeof(command_buffer),
                                "T-WI%u-12-31T23:59:59",iso[i]);
        check_command(iso[i]>=1980 && iso[i]<=2099,iso[i]-1900);
    }
    unsigned ascii[]={0,79,80,99,100,199,200,255,256,65536};
    for (unsigned i=0;i<sizeof(ascii)/sizeof(ascii[0]);i++) {
        reset();
        command_length=snprintf((char *)command_buffer,sizeof(command_buffer),
                                "T-WASUN. 01/01/%02u 01:02:03 AM",ascii[i]);
        check_command(ascii[i]<=199,ascii[i]<80 ? ascii[i]+100 : ascii[i]);
    }
    unsigned decimal[]={0,79,80,99,100,199,200,255};
    for (unsigned i=0;i<sizeof(decimal)/sizeof(decimal[0]);i++) {
        reset();
        uint8_t cmd[]={'T','-','W','D',0,decimal[i],12,31,11,59,59,1};
        memcpy(command_buffer,cmd,sizeof(cmd)); command_length=sizeof(cmd);
        check_command(decimal[i]<=199,decimal[i]<80 ? decimal[i]+100 : decimal[i]);
    }
    reset();
    uint8_t bcd[]={'T','-','W','B',0,0x99,0x12,0x31,0x11,0x59,0x59,1};
    memcpy(command_buffer,bcd,sizeof(bcd)); command_length=sizeof(bcd);
    check_command(true,99);

    /* Every supported DSRTC year round-trips with the century bit clear. */
    for (unsigned chip=RTC_1307;chip<=RTC_3231;chip++) {
        for (unsigned year=100;year<=RTC_MAX_YEAR;year++) {
            reset(); dsrtc_type=chip;
            struct tm time={59,59,23,31,11,year,4}, result;
            dsrtc_set(&time);
            assert(writes>0 && !(registers[REG_MONTH]&0x80));
            dsrtc_read(&result);
            assert(!memcmp(&time,&result,sizeof(time)));
        }
        for (unsigned year=200;year<=255;year++) {
            reset(); dsrtc_type=chip;
            struct tm time={0,0,0,1,0,year,0};
            dsrtc_set(&time);
            assert(writes==0 && rtc_state==RTC_OK);
        }
    }
    /* Existing out-of-range DS3231 dates cannot wrap into a valid year. */
    for (unsigned year=0;year<100;year++) {
        reset();
        registers[REG_YEAR]=int2bcd(year);
        registers[REG_MONTH]=0x81;
        struct tm result;
        dsrtc_read(&result);
        assert(rtc_state==RTC_INVALID && writes==0);
        assert(!memcmp(&result,&rtc_default_date,sizeof(result)));
    }
    /* The first T-R after a century rollover must report an error too. */
    const char formats[]="ABDI";
    for (unsigned i=0;i<sizeof(formats)-1;i++) {
        reset();
        registers[REG_MONTH]=0x81;
        command_buffer[3]=formats[i];
        parse_timeread();
        assert(current_error==ERROR_SYNTAX_UNABLE && rtc_state==RTC_INVALID);
    }
    reset();
    struct tm last={59,59,23,31,11,199,4};
    dsrtc_set(&last);
    command_buffer[3]='I';
    parse_timeread();
    assert(current_error==0 && !memcmp(error_buffer,"2099-12-31T23:59:59",19));
    reset();
    rtc_state=RTC_NOT_FOUND;
    struct tm time={0,0,0,1,0,199,0}, result;
    dsrtc_set(&time); dsrtc_read(&result);
    assert(writes==0 && !memcmp(&result,&rtc_default_date,sizeof(result)));
    puts("RTC: command year bounds, all DSRTC years 2000-2099, rejected writes and century rollover passed.");
}
'''

source = PRELUDE + '\n' + defines + '\n' + chip_type + '\n' + days + '\n' + skel + '\n'
source += '\n'.join(function(utils, name) for name in ('bcd2int', 'int2bcd', 'appendnumber'))
source += '\n' + function(parser, 'parse_number')
source += '\n' + function(dsrtc, 'dsrtc_read') + '\n' + function(dsrtc, 'dsrtc_set')
source += '''
void set_rtc(struct tm *time) {
    set_calls++; requested=*time; dsrtc_set(time);
}
void read_rtc(struct tm *time) {dsrtc_read(time);}
'''
source += function(doscmd, 'day_of_week') + '\n' + function(doscmd, 'parse_timewrite')
source += '\n' + function(doscmd, 'parse_timeread')
source += TEST

with tempfile.TemporaryDirectory(prefix='rtc-year-test-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(source)
    subprocess.run([
        'gcc', '-std=c99', '-g', '-O1', '-Wall', '-Wextra',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
        '-I' + str(ROOT / 'src'), str(tmp / 'test.c'), '-o', str(tmp / 'test'),
    ], check=True)
    subprocess.run([str(tmp / 'test')], check=True, timeout=15)
