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

#define LCD_DELAY_MS_POWERON   500
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
#define modesafe_delay_ms(x) _delay_ms(x)
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

#ifdef CONFIG_MULTIDRIVE
/* The HD44780 shows an arrow instead of a tilde. Character 0 of the  */
/* CGRAM becomes a tilde, as in short FAT names like ARCHON~1.D64.    */
/* The splash logo uses the same CGRAM slots, so the glyph is loaded  */
/* on first use and again after the logo has replaced it.             */
#define LCD_CHAR_TILDE 0

static const PROGMEM uint8_t lcd_tilde[8] = {
  0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00
};

static bool lcd_tilde_loaded;

static void lcd_define_tilde(void) {
  lcd_send_command(0x40 | (LCD_CHAR_TILDE << 3));   // CGRAM address
  lcd_set_data_mode();
  for (uint8_t i = 0; i < sizeof(lcd_tilde); i++)
    lcd_write(pgm_read_byte(lcd_tilde + i));
  lcd_tilde_loaded = true;
}
#endif


void lcd_load_custom_chars_P(const uint8_t *glyphs) {
  uint8_t x = lcd_x, y = lcd_y;

#ifdef CONFIG_MULTIDRIVE
  lcd_tilde_loaded = false;
#endif

  lcd_send_command(0x40); // CGRAM address zero
  lcd_set_data_mode();
  for (uint8_t i = 0; i < 64; i++)
    lcd_write(pgm_read_byte(glyphs + i) & 0x1f);
  lcd_locate(x, y);       // Restore DDRAM addressing and cursor position
}


void lcd_cursor(bool on) {
  lcd_send_command(on ? 0x0F : 0x0C);
}


/* Drive the display lines low before enabling their output drivers. */
void lcd_lines_init(void) {
  LCD_PORT_E &= ~_BV(LCD_PIN_E);
  LCD_DDR_E |= _BV(LCD_PIN_E);
  LCD_PORT_RS &= ~_BV(LCD_PIN_RS);
  LCD_DDR_RS |= _BV(LCD_PIN_RS);
  LCD_PORT_DATA &= 0xF0;
  LCD_DDR_DATA |= 0x0F;
}


void lcd_init(void) {
  lcd_lines_init();
  /* WS0010 specifies 500 ms for power stabilization; HD44780 is faster. */
  modesafe_delay_ms(LCD_DELAY_MS_POWERON);

  /* A processor reset leaves the display powered and may interrupt a
   * byte. Establish the HD44780 4-bit interface by instruction, retaining
   * the repeated startup sequence for modules that miss an early nibble.
   * The WS0010 synchronizer below then establishes its byte alignment.
   */
  for (uint8_t pass = 0; pass < 2; pass++) {
    LCD_PORT_DATA &= 0xF0;
    LCD_PORT_DATA |= 0x03;     // send init value 0x30 three times
    lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_LONG);
    lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_LONG);
    lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_LONG);

    LCD_PORT_DATA &= 0xF2;     // select bus width: 4-bit
    lcd_pulse_e(); modesafe_delay_ms(LCD_DELAY_MS_LONG);
  }

  /* WS0010 has a separate nibble synchronizer: five zero nibbles make
   * the next transfer the LOW half of a command. HD44780 sees two NOPs
   * and also waits for a low nibble. Complete 0x02 (return home) on both
   * before sending full bytes. This keeps the same firmware usable with
   * either controller, without a display-type setting or a readback pin.
   * See WS0010 vB, "Repeated procedures for an 4-bit bus interface".
   */
  LCD_PORT_DATA &= 0xF0;
  for (uint8_t nibble = 0; nibble < 5; nibble++) {
    lcd_pulse_e();
    modesafe_delay_ms(LCD_DELAY_MS_LONG);
  }
  LCD_PORT_DATA |= 0x02;
  lcd_pulse_e();
  modesafe_delay_ms(LCD_DELAY_MS_LONG);

  lcd_set_command_mode();
  lcd_write(0x28);             // 4 bit, 2 line, 5x7 dots
  modesafe_delay_ms(LCD_DELAY_MS_SHORT);
  /* WS0010: character mode and internal power on. HD44780 decodes this
   * as cursor-right (the low two bits are ignored); lcd_clear() below
   * restores the address and display shift, so it is safe for LCDs too.
   */
  lcd_write(0x17);
  modesafe_delay_ms(LCD_DELAY_MS_SHORT);
  lcd_write(0x08);             // Display off while it is cleared
  modesafe_delay_ms(LCD_DELAY_MS_SHORT);
  lcd_clear();
  lcd_write(0x06);             // Automatic increment, no shift
  modesafe_delay_ms(LCD_DELAY_MS_SHORT);
  lcd_write(0x0C);             // Display on, cursor off
  modesafe_delay_ms(LCD_DELAY_MS_SHORT);

  lcd_stdout = &lcd_stream;
}


void lcd_putc(char c) {
  if (c == '\n')
  {
    lcd_x = 0;
    lcd_y++;
  }

#ifdef CONFIG_MULTIDRIVE
  if (c == '~' && !lcd_tilde_loaded)
    lcd_define_tilde();             // lcd_locate restores DDRAM addressing
#endif

  lcd_locate(lcd_x, lcd_y);
  if ((uint8_t)c < 8 || (uint8_t)c >= ' ')
  {
    lcd_set_data_mode();
#ifdef CONFIG_MULTIDRIVE
    if (c == '~') c = LCD_CHAR_TILDE;
#endif
    lcd_write(c);
    lcd_x++;
  }
}


static int lcd_putchar(char c, FILE *stream) {
  (void)stream;

  lcd_putc(c);
  return 0;
}


void lcd_printf_P(const char *fmt, ...) {
  va_list args;
  /* Initialization can be skipped by a board or not have run yet. */
  if (lcd_stdout == NULL)
    return;
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
