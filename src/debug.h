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

*/

/* Debug output can be sent to
     - the UART if CONFIG_UART_DEBUG=y is given
     - the LCD if CONFIG_DEBUG_MSGS_TO_LCD=y is given
     - or both if both options are enabled

   All higher level debug output functions are based on two core functions
   debug_putc() and debug_flush() so the former is used for distribution.
*/

#include "config.h"
#include "uart.h"
#include "lcd.h"

static inline void debug_putc(char c);
static inline void debug_flush(void);

void debug_init(void);
void debug_puts_P(const char *text);
void debug_putcrlf(void);
void debug_puthex(uint8_t num);
void debug_trace(void *ptr, uint16_t start, uint16_t len);

// If debug output could harm timing sensitive processes, use debug_flush()
// first to ensure an empty buffer. The LCD doesn't need buffering.
#ifdef CONFIG_UART_DEBUG
static inline void debug_flush(void) { uart_flush(); }
#else
static inline void debug_flush(void) {}
#endif

// Debug output to both UART and LCD
#if defined(CONFIG_UART_DEBUG) && defined(CONFIG_DEBUG_MSGS_TO_LCD)
static inline void debug_putc(char c) {
  if (c == '\n') {
    uart_putc('\r');
    lcd_putc('\r');
  }
  uart_putc(c);
  lcd_putc(c);
}
#endif

// Debug output to UART only
#if defined(CONFIG_UART_DEBUG) && !defined(CONFIG_DEBUG_MSGS_TO_LCD)
static inline void debug_putc(char c) {
  if (c == '\n') uart_putc('\r');
  uart_putc(c);
}
#endif

// Debug output to LCD only
#if !defined(CONFIG_UART_DEBUG) && defined(CONFIG_DEBUG_MSGS_TO_LCD)
static inline void debug_putc(char c) {
  if (c == '\n') lcd_putc('\r');
  lcd_putc(c);
}
#endif

// No debug output
#if !defined(CONFIG_UART_DEBUG) && !defined(CONFIG_DEBUG_MSGS_TO_LCD)
static inline void debug_putc(char c) {}
#endif


