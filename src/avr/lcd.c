/*-
 * Copyright (c) 2015 Nils Eilers. All rights reserved.
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


#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "config.h"
#include "lcd.h"
#include "timer.h"
#include "errormsg.h"
#include "bus.h"
#include "arch-timer.h"
#include "i2c.h"


#define LCD_DELAY_US_DATA       92
#define LCD_DELAY_US_E          40

#define LCD_DELAY_MS_POWERON    30
#define LCD_DELAY_MS_CLEAR      20
#define LCD_DELAY_MS_LONG       10
#define LCD_DELAY_MS_SHORT       2

#define LCD_DDRAM 128

uint8_t lcd_x;                  // 0..LCD_LINES-1
uint8_t lcd_y;                  // 0..LCD_COLS-1
uint8_t lcd_contrast = 2;       // Default values which work for me
uint8_t lcd_brightness = 255;


static int lcd_putchar(char c, FILE *stream);
static FILE lcd_stream = FDEV_SETUP_STREAM(lcd_putchar, NULL, _FDEV_SETUP_WRITE);
static FILE *lcd_stdout;


// avr-libc requires constant delays so we're doing some macro voodoo here

#ifdef IEC_SLOW_IEEE_FAST
#define modesafe_delay_us(x)            \
  if (active_bus == IEC)                \
    _delay_us(x);                       \
  else                                  \
    _delay_us(x*2);
#else
#define modesafe_delay_us(x) _delay_us(x)
#endif

#ifdef IEC_SLOW_IEEE_FAST
#define modesafe_delay_ms(x)            \
  if (active_bus == IEC)                \
    _delay_ms(x);                       \
  else                                  \
    _delay_ms(x*2);
#else
#define modesafe_delay_us(x) _delay_us(x)
#endif



static inline void lcd_set_data_mode(void) {
  LCD_PORT_RS |= _BV(LCD_PIN_RS);
}


static inline void lcd_set_command_mode(void) {
  LCD_PORT_RS &= ~_BV(LCD_PIN_RS);
}


static void lcd_pulse_e(void) {
  LCD_PORT_E |= _BV(LCD_PIN_E);                // E high
  modesafe_delay_us(LCD_DELAY_US_E);
  LCD_PORT_E &= ~_BV(LCD_PIN_E);               // E low
}


static void lcd_write(uint8_t v) {
  LCD_PORT_DATA &= 0xF0;
  LCD_PORT_DATA |= ((v >> 4) & 0x0F);          // high nibble
  lcd_pulse_e();
  modesafe_delay_us(LCD_DELAY_US_DATA);
  LCD_PORT_DATA &= 0xF0;
  LCD_PORT_DATA |= ( v       & 0x0F);          // low  nibble
  lcd_pulse_e();
  modesafe_delay_us(LCD_DELAY_US_DATA);
}


void lcd_send_command(uint8_t cmd) {
  lcd_set_command_mode();
  lcd_write(cmd);
}


void lcd_locate(uint8_t x, uint8_t y) {
  if (x >= LCD_COLS) {
    x = 0;
    y++;
  }

  if (y >= LCD_LINES) {
    lcd_clear();
    y = 0;
  }

  lcd_set_command_mode();
  uint8_t StartAddress = 0;
  switch (y)
  {
    case 0: StartAddress = LCD_ADDR_LINE1; break;
    case 1: StartAddress = LCD_ADDR_LINE2; break;
    case 2: StartAddress = LCD_ADDR_LINE3; break;
    case 3: StartAddress = LCD_ADDR_LINE4; break;
    default: StartAddress = 0;
  }
  lcd_write(LCD_DDRAM + StartAddress + x);
  lcd_x = x;
  lcd_y = y;
}


void lcd_clear(void) {
  lcd_send_command(0x01);
  lcd_x = lcd_y = 0;
  modesafe_delay_ms(LCD_DELAY_MS_CLEAR);
}


void lcd_home(void) {
  lcd_locate(0,0);
}


void lcd_cursor(bool on) {
  lcd_send_command(on ? 0x0F : 0x0C);
}


void lcd_init(void) {
  // LCD ports as output
  LCD_DDR_E  |= _BV(LCD_PIN_E);
  LCD_DDR_RS |= _BV(LCD_PIN_RS);
  LCD_DDR_DATA |= 0x0F;

  // Start with all outputs low
  LCD_PORT_E &= ~_BV(LCD_PIN_E);
  LCD_PORT_RS &= ~_BV(LCD_PIN_RS);
  LCD_PORT_DATA &= 0xF0;


  modesafe_delay_ms(LCD_DELAY_MS_POWERON);

  LCD_PORT_DATA &= 0xF0;
  LCD_PORT_DATA |= 0x03;       // send init value 0x30 three times
  lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_LONG);
  lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_SHORT);
  lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_SHORT);

  LCD_PORT_DATA &= 0xF2;       // select bus width: 4-bit
  lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_LONG);

  lcd_write(0x28);             // 4 bit, 2 line, 5x7 dots
  lcd_write(0x0C);             // Display on, cursor off
  lcd_write(0x06);             // Automatic increment, no shift
  lcd_clear();

  lcd_stdout = &lcd_stream;
}


void lcd_putc(char c) {
  if (c == '\n')
  {
    lcd_x = 0;
    lcd_y++;
  }

  lcd_locate(lcd_x, lcd_y);
  if (c >= ' ')
  {
    lcd_set_data_mode();
    lcd_write(c);
    lcd_x++;
  }
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


void lcd_puts(const char *s) {
  while (*s)
    lcd_putc(*s++);
}


void lcd_puts_P(const char *s) {
  char c;

  while ((c = pgm_read_byte(s++)))
    lcd_putc(c);
}


void lcd_clrlines(uint8_t from, uint8_t to) {
  lcd_locate(0, from);
  for (uint8_t i = from; i <= to; i++)
    for (uint8_t j = 0; j < LCD_COLS; j++)
      lcd_putc(' ');
}

uint8_t lcd_set_contrast(uint8_t contrast) {
  return i2c_write_register(I2C_SLAVE_ADDRESS, PWM_CONTRAST, contrast);
}

uint8_t lcd_set_brightness(uint8_t brightness) {
  return i2c_write_register(I2C_SLAVE_ADDRESS, PWM_BRIGHTNESS, 255 - brightness);
}
