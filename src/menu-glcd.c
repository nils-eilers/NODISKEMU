/*-
 * Copyright (c) 2016 Nils Eilers. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// TODO: implement menu system for graphic LCD display

#include "config.h"
#include "menu-glcd.h"
#include "i2c.h"

static int lcd_putchar(char c, FILE *stream);
static FILE lcd_stream = FDEV_SETUP_STREAM(lcd_putchar, NULL, _FDEV_SETUP_WRITE);
static FILE *lcd_stdout;

void lcd_init(void) {
  lcd_clear();
  lcd_stdout = &lcd_stream;
}

void lcd_putc(char c) {
  i2c_write_register(SLAVE_ADDR, 1, c);
}

static int lcd_putchar(char c, FILE *stream) {
  lcd_putc(c);
  return 0;
}

void lcd_printf_P(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf_P(lcd_stdout, fmt, args);
  va_end(args);
}

#ifdef CONFIG_DEBUG_MSGS_TO_LCD
// redirect stdout to LCD
#endif
