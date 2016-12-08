/* NODISKEMU - SD/MMC to IEEE-488 interface/controller
   Copyright (C) 2007-2015  Ingo Korb <ingo@akana.de>
   Copyright (C) 2016 Nils Eilers <nils.eilers@gmx.de>

   NODISKEMU is a fork of sd2iec by Ingo Korb (et al.), http://sd2iec.de

   Inspired by MMC2IEC by Lars Pontoppidan et al.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   debug.c: debug output functions

*/


#include "config.h"
#include "debug.h"
#include "progmem.h"


void debug_puthex(uint8_t num) {
  uint8_t tmp;
  tmp = (num & 0xf0) >> 4;
  if (tmp < 10)
    debug_putc('0'+tmp);
  else
    debug_putc('A'+tmp-10);

  tmp = num & 0x0f;
  if (tmp < 10)
    debug_putc('0'+tmp);
  else
    debug_putc('A'+tmp-10);
}

void debug_trace(void *ptr, uint16_t start, uint16_t len) {
  uint16_t i;
  uint8_t j;
  uint8_t ch;
  uint8_t *data = ptr;

  data+=start;
  for(i=0;i<len;i+=16) {

    debug_puthex(start>>8);
    debug_puthex(start&0xff);
    debug_putc('|');
    debug_putc(' ');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data + j);
        debug_puthex(ch);
      } else {
        debug_putc(' ');
        debug_putc(' ');
      }
      debug_putc(' ');
    }
    debug_putc('|');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data++);
        if(ch<32 || ch>0x7e)
          ch='.';
        debug_putc(ch);
      } else {
        debug_putc(' ');
      }
    }
    debug_putc('|');
    debug_putcrlf();
    debug_flush();
    start+=16;
  }
}


void debug_puts_P(const char *text) {
  uint8_t ch;

  while ((ch = pgm_read_byte(text++))) {
    debug_putc(ch);
  }
}

void debug_putcrlf(void) {
  debug_putc(13);
  debug_putc(10);
}

