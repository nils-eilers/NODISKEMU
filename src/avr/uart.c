/* NODISKEMU - SD/MMC to IEEE-488 interface/controller
   Copyright (C) 2007-2015  Ingo Korb <ingo@akana.de>

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


   uart.c: UART access routines

*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>

#include "config.h"
#include "avrcompat.h"
#include "uart.h"
#include "bus.h"

#if CONFIG_UART_BAUDRATE != 0xdeadbeef
#  define BAUD CONFIG_UART_BAUDRATE
#  include <util/setbaud.h>
#endif

// uint8_t txbuf[1 << CONFIG_UART_BUF_SHIFT];
// FIXME: use CONFIG_UART_BUF_SHIFT in interrupt routine
uint8_t txbuf[1 << 8];

volatile uint16_t read_idx;
volatile uint16_t write_idx;

void uart_putc(char c) {
  uint16_t t=(write_idx+1) & (sizeof(txbuf)-1);
#ifndef CONFIG_DEADLOCK_ME_HARDER // :-)
  UCSRB &= ~ _BV(UDRIE);   // turn off RS232 irq
#else
  while (t == read_idx);   // wait for free space
#endif
  txbuf[write_idx] = c;
  write_idx = t;
  UCSRB |= _BV(UDRIE);
}

void uart_puthex(uint8_t num) {
  uint8_t tmp;
  tmp = (num & 0xf0) >> 4;
  if (tmp < 10)
    uart_putc('0'+tmp);
  else
    uart_putc('A'+tmp-10);

  tmp = num & 0x0f;
  if (tmp < 10)
    uart_putc('0'+tmp);
  else
    uart_putc('A'+tmp-10);
}

void uart_trace(void *ptr, uint16_t start, uint16_t len) {
  uint16_t i;
  uint8_t j;
  uint8_t ch;
  uint8_t *data = ptr;

  data+=start;
  for(i=0;i<len;i+=16) {

    uart_puthex(start>>8);
    uart_puthex(start&0xff);
    uart_putc('|');
    uart_putc(' ');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data + j);
        uart_puthex(ch);
      } else {
        uart_putc(' ');
        uart_putc(' ');
      }
      uart_putc(' ');
    }
    uart_putc('|');
    for(j=0;j<16;j++) {
      if(i+j<len) {
        ch=*(data++);
        if(ch<32 || ch>0x7e)
          ch='.';
        uart_putc(ch);
      } else {
        uart_putc(' ');
      }
    }
    uart_putc('|');
    uart_putcrlf();
    uart_flush();
    start+=16;
  }
}

static int ioputc(char c, FILE *stream) {
  if (c == '\n') uart_putc('\r');
  uart_putc(c);
  return 0;
}

uint8_t uart_getc(void) {
  loop_until_bit_is_set(UCSRA,RXC);
  return UDR;
}

void uart_flush(void) {
  while (read_idx != write_idx) ;
}

void uart_puts_P(const char *text) {
  uint8_t ch;

  while ((ch = pgm_read_byte(text++))) {
    uart_putc(ch);
  }
}

void uart_putcrlf(void) {
  uart_putc(13);
  uart_putc(10);
}

static FILE mystdout = FDEV_SETUP_STREAM(ioputc, NULL, _FDEV_SETUP_WRITE);

void uart_init(void) {
  /* Configure serial port */
#if CONFIG_HARDWARE_VARIANT == HW_PETSDPLUS
   // petSD+ 38400 baud, 8N1
   // Values taken from http://wormfood.net/avrbaudcalc.php?clock=8%2C16
   UBRRH = 0;
   UBRRL = (active_bus == IEC) ? 0x0C : 0x19;
   UCSRA &= ~(1 << U2X0);
#  else
   UBRRH = UBRRH_VALUE;
   UBRRL = UBRRL_VALUE;
#  if USE_2X
   UCSRA |= (1 << U2X0);        /* U2X-mode required */
#    else
   UCSRA &= ~(1 << U2X0);       /* U2X-not required */
#  endif
#endif

  UCSRB = _BV(TXEN);
  UCSRC = _BV(UCSZ1) | _BV(UCSZ0);

  stdout = &mystdout;

  read_idx  = 0;
  write_idx = 0;
}
