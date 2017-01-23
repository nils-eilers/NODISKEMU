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


   uart.h: Definitions for the UART access routines

*/

#ifndef UART_H
#define UART_H

#if defined(CONFIG_UART_DEBUG) || defined(CONFIG_SPSP)

#include "progmem.h"

void uart_init(void);
void uart_putc(char c);
void uart_puts_P(const char *s);
void uart_flush(void);

/* non-blocking, returns '\0' if no character is available */
char uart_getc(void);

/* blocking, waits until character is available */
char uart_getch(void);

#ifdef CONFIG_SPSP
bool uart_rxbuf_empty(void);
#endif

#ifdef __AVR__
#  include <stdio.h>
#  define printf(str,...) printf_P(PSTR(str), ##__VA_ARGS__)
#endif

#else

#define uart_init()    do {} while(0)
#define uart_getc()    0
#define uart_putc(x)   do {} while(0)
#define uart_puthex(x) do {} while(0)
#define uart_flush()   do {} while(0)
#define uart_puts_P(x) do {} while(0)
#define uart_putcrlf() do {} while(0)
#define uart_trace(a,b,c) do {} while(0)
#define uart_rxbuf_empty() do {} while(0)

#endif

#endif
