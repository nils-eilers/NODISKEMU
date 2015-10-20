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

// LCD menu system

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "lcd.h"
#include "menu.h"
#include "timer.h"
#include "errormsg.h"
#include "bus.h"


static tick_t lcd_timeout;
static bool lcd_timer;
static uint16_t lcd_current_screen;



static inline uint8_t min(uint8_t a, uint8_t b) {
  if (a < b) return a;
  return b;
}


void lcd_update_device_addr(void) {
  if (lcd_current_screen == SCRN_STATUS) {
    lcd_home();
    lcd_printf("#%d ", device_address);
  }
}


void lcd_update_disk_status(void) {
  bool visible = true;

  if (lcd_current_screen == SCRN_STATUS) {
    lcd_locate(0, 1);
    for (uint8_t i = 0;
         i < min(CONFIG_ERROR_BUFFER_SIZE, LCD_COLS * (LCD_LINES > 3 ? 3: 1));
         i++)
    {
      if (error_buffer[i] == 13) visible = false;
      lcd_putc(visible ? error_buffer[i] : ' ');
    }
  }
}


void lcd_draw_screen(uint16_t screen) {
  extern const char PROGMEM versionstr[];

  lcd_current_screen = screen;
  lcd_clear();

  switch (screen) {
  case SCRN_SPLASH:
    lcd_puts_P(versionstr);
    lcd_locate(0,3); lcd_puts_P(PSTR(HWNAME));
    // TODO: if available, print serial number here
    break;

  case SCRN_STATUS:
    lcd_update_device_addr();
    lcd_update_disk_status();
    break;

  default:
    break;
  }
}


void lcd_splashscreen(void) {
  lcd_timeout = getticks() + MS_TO_TICKS(1000 * 5);
  lcd_timer = true;
}


void handle_lcd(void) {
  tick_t ticks;

  if (lcd_timer) {
    ticks = getticks();
    if (time_before(lcd_timeout, ticks)) {
      lcd_draw_screen(SCRN_STATUS);
      lcd_timer = false;
    }
  }
}


void menu(void) {
  lcd_clear();
  lcd_printf("Insert fancy\nmenu system here!");
  while(!get_key_press(KEY_ANY));
  lcd_draw_screen(SCRN_STATUS);
}

